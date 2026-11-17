/* Issue #44: Vulkan rendering backend for the PC build (GE_RENDERER=vk; GL
 * stays the default until this backend matches it).
 *
 * A GfxRenderingAPI that the frame replay (gfx_frame.cpp) drives exactly like
 * the GL backend: same call order, same state semantics (gfx_opengl.cpp is the
 * reference for every mapping). The structure follows gfx_deko3d.cpp (D973),
 * which already solves the same problems in explicit-API terms: frames in
 * flight with a fence each, the swapchain owned by the backend and presented
 * through SDL's external-present hook (gfx_sdl_set_external_present).
 *
 * Loader: volk (port/fast3d/volk/, MIT) opens the Vulkan loader at run time,
 * so the binary does not link it; without a usable driver gfx_vulkan_probe
 * fails and video.c falls back to GL.
 *
 * Target: Vulkan 1.3 core (dynamic rendering, synchronization2, extended
 * dynamic state), so later pipelines key on program x blend x MSAA x format
 * only. MoltenVK: portability enumeration / subset are enabled when offered.
 *
 * Validation: VK_LAYER_KHRONOS_validation is enabled whenever it is installed
 * (Vulkan SDK), unless GE_VKVALIDATE=0; every message goes to the log and the
 * error count is reported at exit.
 *
 * Status (step 1 of the issue): window, instance, device and swapchain; each
 * present clears the swapchain image to a fixed colour. The rendering API
 * calls are accepted and tracked (programs, textures and framebuffers get
 * ids) but draw nothing yet. */

#ifdef GE_VULKAN

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <vector>
#include <utility>

#include "volk/volk.h"
#include <SDL.h>
#include <SDL_vulkan.h>

#include "system.h"
#include "gfx_cc.h"
#include "gfx_rendering_api.h"
#include "gfx_vulkan.h"
#include "gfx_sdl.h"

extern "C" volatile int gfx_skyplus_failed;   /* gfx_opengl.cpp: 1 = skyplus.c draws GE's own sky */

struct ShaderProgram {
    uint64_t id0;
    uint32_t id1;
    uint8_t num_inputs;
    bool used_textures[2];
};

namespace {

const int FRAMES_IN_FLIGHT = 2;

#define VK_CHECK(call)                                                                      \
    do {                                                                                    \
        VkResult r_ = (call);                                                               \
        if (r_ != VK_SUCCESS) {                                                             \
            sysFatalError("Vulkan: %s failed (VkResult %d)\n%s:%d", #call, (int)r_,         \
                          __FILE__, __LINE__);                                              \
        }                                                                                   \
    } while (0)

/* ---------------- instance, device ---------------- */

bool s_loader_ok = false;
bool s_up = false, s_parked = false;
VkInstance s_instance = VK_NULL_HANDLE;
VkDebugUtilsMessengerEXT s_messenger = VK_NULL_HANDLE;
VkPhysicalDevice s_phys = VK_NULL_HANDLE;
VkDevice s_dev = VK_NULL_HANDLE;
uint32_t s_qfam = 0;
VkQueue s_queue = VK_NULL_HANDLE;
VkSurfaceKHR s_surface = VK_NULL_HANDLE;
char s_renderer_str[256] = "Vulkan";
int s_validation_errors = 0, s_validation_warnings = 0;

/* ---------------- swapchain, frames ---------------- */

VkSwapchainKHR s_swapchain = VK_NULL_HANDLE;
VkFormat s_sc_format = VK_FORMAT_B8G8R8A8_UNORM;
VkExtent2D s_sc_extent = { 0, 0 };
VkPresentModeKHR s_sc_mode = VK_PRESENT_MODE_FIFO_KHR;
std::vector<VkImage> s_sc_images;
std::vector<VkImageView> s_sc_views;
std::vector<VkSemaphore> s_sc_render_done;   /* per swapchain image: present waits on it */
bool s_sc_dirty = false;                     /* recreate before the next acquire */
int s_swap_interval = 1;

struct Frame {
    VkCommandPool pool;
    VkCommandBuffer cmd;
    VkFence fence;
    VkSemaphore acquired;
};
Frame s_frames[FRAMES_IN_FLIGHT];
uint32_t s_frame_idx = 0;

/* ---------------- rendering-API bookkeeping (step 1: no drawing) ---------------- */

std::map<std::pair<uint64_t, uint32_t>, ShaderProgram*> s_progs;
ShaderProgram* s_prg = nullptr;
uint32_t s_next_tex = 1;
uint32_t s_next_mesh = 1;

struct Fb {
    uint32_t w = 1, h = 1, msaa = 1;
    bool invert_y = false, has_depth = false;
};
std::vector<Fb> s_fbs;
int s_cur_fb = 0;
FilteringMode s_filter = FILTER_LINEAR;
int s_aniso = 1;

VKAPI_ATTR VkBool32 VKAPI_CALL debug_cb(VkDebugUtilsMessageSeverityFlagBitsEXT sev, VkDebugUtilsMessageTypeFlagsEXT,
                                        const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    const char* msg = data && data->pMessage ? data->pMessage : "(null)";
    if (sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        s_validation_errors++;
        sysLogPrintf(LOG_ERROR, "VK validation: %s", msg);
    } else if (sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        s_validation_warnings++;
        sysLogPrintf(LOG_WARNING, "VK validation: %s", msg);
    } else {
        sysLogPrintf(LOG_NOTE, "VK: %s", msg);
    }
    return VK_FALSE;
}

bool has_layer(const char* name) {
    uint32_t n = 0;
    vkEnumerateInstanceLayerProperties(&n, nullptr);
    std::vector<VkLayerProperties> v(n);
    vkEnumerateInstanceLayerProperties(&n, v.data());
    for (auto& l : v) {
        if (strcmp(l.layerName, name) == 0) return true;
    }
    return false;
}

bool has_instance_ext(const char* name) {
    uint32_t n = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> v(n);
    vkEnumerateInstanceExtensionProperties(nullptr, &n, v.data());
    for (auto& e : v) {
        if (strcmp(e.extensionName, name) == 0) return true;
    }
    return false;
}

bool has_device_ext(VkPhysicalDevice pd, const char* name) {
    uint32_t n = 0;
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> v(n);
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &n, v.data());
    for (auto& e : v) {
        if (strcmp(e.extensionName, name) == 0) return true;
    }
    return false;
}

bool load_loader(void) {
    if (!s_loader_ok) {
        if (volkInitialize() != VK_SUCCESS) {
            sysLogPrintf(LOG_WARNING, "VK: no Vulkan loader found");
            return false;
        }
        if (volkGetInstanceVersion() < VK_API_VERSION_1_3) {
            const uint32_t v = volkGetInstanceVersion();
            sysLogPrintf(LOG_WARNING, "VK: loader is Vulkan %u.%u, 1.3 needed", VK_API_VERSION_MAJOR(v),
                         VK_API_VERSION_MINOR(v));
            return false;
        }
        s_loader_ok = true;
    }
    return true;
}

/* A device the backend can use: Vulkan 1.3, dynamic rendering, synchronization2,
 * swapchain, and (when a surface is given) a graphics queue that presents to it.
 * Returns the queue family, or -1. */
int usable_queue_family(VkPhysicalDevice pd, VkSurfaceKHR surface) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(pd, &props);
    if (props.apiVersion < VK_API_VERSION_1_3) return -1;
    VkPhysicalDeviceVulkan13Features f13 = {};
    f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceFeatures2 f2 = {};
    f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    f2.pNext = &f13;
    vkGetPhysicalDeviceFeatures2(pd, &f2);
    if (!f13.dynamicRendering || !f13.synchronization2) return -1;
    if (!has_device_ext(pd, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) return -1;
    uint32_t nq = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, nullptr);
    std::vector<VkQueueFamilyProperties> q(nq);
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, q.data());
    for (uint32_t i = 0; i < nq; i++) {
        if (!(q[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
        if (surface != VK_NULL_HANDLE) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface, &present);
            if (!present) continue;
        }
        return (int)i;
    }
    return -1;
}

/* Discrete over integrated over the rest; GE_VKDEVICE=<index> forces one. */
VkPhysicalDevice pick_device(VkInstance inst, VkSurfaceKHR surface, uint32_t* qfam) {
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst, &n, nullptr);
    std::vector<VkPhysicalDevice> v(n);
    vkEnumeratePhysicalDevices(inst, &n, v.data());
    const char* force = getenv("GE_VKDEVICE");
    VkPhysicalDevice best = VK_NULL_HANDLE;
    int best_score = -1;
    for (uint32_t i = 0; i < n; i++) {
        const int fam = usable_queue_family(v[i], surface);
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(v[i], &props);
        if (surface != VK_NULL_HANDLE) {
            sysLogPrintf(LOG_NOTE, "VK: device %u: %s (Vulkan %u.%u.%u)%s", i, props.deviceName,
                         VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion),
                         VK_API_VERSION_PATCH(props.apiVersion), fam < 0 ? " - not usable" : "");
        }
        if (fam < 0) continue;
        int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3
                  : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
        if (force && force[0] && atoi(force) == (int)i) score = 100;
        if (score > best_score) {
            best_score = score;
            best = v[i];
            *qfam = (uint32_t)fam;
        }
    }
    return best;
}

void create_instance(bool with_window) {
    std::vector<const char*> exts;
    std::vector<const char*> layers;
    if (with_window) {
        SDL_Window* w = (SDL_Window*)gfx_sdl_get_sdl_window();
        unsigned int n = 0;
        if (!SDL_Vulkan_GetInstanceExtensions(w, &n, nullptr)) {
            sysFatalError("Vulkan: SDL_Vulkan_GetInstanceExtensions failed:\n%s", SDL_GetError());
        }
        exts.resize(n);
        SDL_Vulkan_GetInstanceExtensions(w, &n, exts.data());
    }
    VkInstanceCreateFlags flags = 0;
    if (has_instance_ext(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {   /* MoltenVK */
        exts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    bool validate = false;
    if (with_window) {
        const char* e = getenv("GE_VKVALIDATE");
        const bool want = !(e && e[0] == '0');
        if (want && has_layer("VK_LAYER_KHRONOS_validation") && has_instance_ext(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            validate = true;
        }
        sysLogPrintf(LOG_NOTE, "VK: validation layer %s", validate ? "on (GE_VKVALIDATE=0 turns it off)"
                                                         : want ? "not installed (Vulkan SDK)" : "off (GE_VKVALIDATE=0)");
    }

    VkApplicationInfo app = {};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "ge007";
    app.applicationVersion = 1;
    app.pEngineName = "fast3d";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_3;

    VkDebugUtilsMessengerCreateInfoEXT dbg = {};
    dbg.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    dbg.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    dbg.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    dbg.pfnUserCallback = debug_cb;

    VkInstanceCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pNext = validate ? &dbg : nullptr;   /* also covers vkCreateInstance / vkDestroyInstance */
    ci.flags = flags;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = (uint32_t)exts.size();
    ci.ppEnabledExtensionNames = exts.data();
    ci.enabledLayerCount = (uint32_t)layers.size();
    ci.ppEnabledLayerNames = layers.data();
    VkResult r = vkCreateInstance(&ci, nullptr, &s_instance);
    if (r != VK_SUCCESS && validate) {
        /* The layer's manifest was found but its DLL did not load (missing
         * dependency, broken build): run without validation rather than die. */
        sysLogPrintf(LOG_WARNING, "VK: instance with the validation layer failed (VkResult %d); retrying without it", (int)r);
        validate = false;
        layers.clear();
        exts.pop_back();   /* VK_EXT_debug_utils, pushed last */
        ci.pNext = nullptr;
        ci.enabledExtensionCount = (uint32_t)exts.size();
        ci.enabledLayerCount = 0;
        ci.ppEnabledLayerNames = nullptr;
        s_validation_errors = s_validation_warnings = 0;
        r = vkCreateInstance(&ci, nullptr, &s_instance);
    }
    VK_CHECK(r);
    volkLoadInstanceOnly(s_instance);
    if (validate) {
        VK_CHECK(vkCreateDebugUtilsMessengerEXT(s_instance, &dbg, nullptr, &s_messenger));
    }
}

void create_device(void) {
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = s_qfam;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    std::vector<const char*> exts = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    if (has_device_ext(s_phys, "VK_KHR_portability_subset")) exts.push_back("VK_KHR_portability_subset");

    VkPhysicalDeviceVulkan13Features f13 = {};
    f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceFeatures2 f2 = {};
    f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    f2.pNext = &f13;
    {
        VkPhysicalDeviceFeatures avail;
        vkGetPhysicalDeviceFeatures(s_phys, &avail);
        f2.features.samplerAnisotropy = avail.samplerAnisotropy;
        f2.features.depthClamp = avail.depthClamp;   /* D579: GL runs with depth clamp */
    }

    VkDeviceCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.pNext = &f2;
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qci;
    ci.enabledExtensionCount = (uint32_t)exts.size();
    ci.ppEnabledExtensionNames = exts.data();
    VK_CHECK(vkCreateDevice(s_phys, &ci, nullptr, &s_dev));
    volkLoadDevice(s_dev);
    vkGetDeviceQueue(s_dev, s_qfam, 0, &s_queue);
}

/* ---------------- swapchain ---------------- */

void window_drawable_size(uint32_t* w, uint32_t* h) {
    int iw = 0, ih = 0;
    SDL_Window* win = (SDL_Window*)gfx_sdl_get_sdl_window();
    if (win) SDL_Vulkan_GetDrawableSize(win, &iw, &ih);
    *w = iw > 0 ? (uint32_t)iw : 0;
    *h = ih > 0 ? (uint32_t)ih : 0;
}

VkPresentModeKHR choose_present_mode(void) {
    if (s_swap_interval != 0) return VK_PRESENT_MODE_FIFO_KHR;   /* VSync; always supported */
    uint32_t n = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(s_phys, s_surface, &n, nullptr);
    std::vector<VkPresentModeKHR> v(n);
    vkGetPhysicalDeviceSurfacePresentModesKHR(s_phys, s_surface, &n, v.data());
    bool mailbox = false, immediate = false;
    for (auto m : v) {
        mailbox |= m == VK_PRESENT_MODE_MAILBOX_KHR;
        immediate |= m == VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    return immediate ? VK_PRESENT_MODE_IMMEDIATE_KHR : mailbox ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_FIFO_KHR;
}

void destroy_swapchain_views(void) {
    for (auto v : s_sc_views) vkDestroyImageView(s_dev, v, nullptr);
    s_sc_views.clear();
    for (auto s : s_sc_render_done) vkDestroySemaphore(s_dev, s, nullptr);
    s_sc_render_done.clear();
    s_sc_images.clear();
}

/* Returns false when the window has no area (minimised): nothing to present. */
bool create_swapchain(void) {
    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(s_phys, s_surface, &caps));
    VkExtent2D ext = caps.currentExtent;
    if (ext.width == 0xFFFFFFFFu) {   /* the surface takes the swapchain's size (Wayland) */
        uint32_t w, h;
        window_drawable_size(&w, &h);
        ext.width = w < caps.minImageExtent.width ? caps.minImageExtent.width
                  : w > caps.maxImageExtent.width ? caps.maxImageExtent.width : w;
        ext.height = h < caps.minImageExtent.height ? caps.minImageExtent.height
                   : h > caps.maxImageExtent.height ? caps.maxImageExtent.height : h;
    }
    if (ext.width == 0 || ext.height == 0) return false;

    {
        uint32_t n = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(s_phys, s_surface, &n, nullptr);
        std::vector<VkSurfaceFormatKHR> v(n);
        vkGetPhysicalDeviceSurfaceFormatsKHR(s_phys, s_surface, &n, v.data());
        /* UNORM, like GL's default framebuffer (the shaders write display values) */
        VkSurfaceFormatKHR pick = v.empty() ? VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } : v[0];
        for (auto& f : v) {
            if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                pick = f;
                break;
            }
        }
        s_sc_format = pick.format;

        uint32_t count = caps.minImageCount + 1;
        if (caps.maxImageCount != 0 && count > caps.maxImageCount) count = caps.maxImageCount;
        s_sc_mode = choose_present_mode();

        VkSwapchainKHR old = s_swapchain;
        VkSwapchainCreateInfoKHR ci = {};
        ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        ci.surface = s_surface;
        ci.minImageCount = count;
        ci.imageFormat = pick.format;
        ci.imageColorSpace = pick.colorSpace;
        ci.imageExtent = ext;
        ci.imageArrayLayers = 1;
        /* transfer src: GE_PCDUMP / screenshot readback; transfer dst: blits and clears */
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                        (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.preTransform = caps.currentTransform;
        ci.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                                ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                                : (VkCompositeAlphaFlagBitsKHR)(caps.supportedCompositeAlpha & -caps.supportedCompositeAlpha);
        ci.presentMode = s_sc_mode;
        ci.clipped = VK_TRUE;
        ci.oldSwapchain = old;
        VK_CHECK(vkCreateSwapchainKHR(s_dev, &ci, nullptr, &s_swapchain));
        if (old != VK_NULL_HANDLE) vkDestroySwapchainKHR(s_dev, old, nullptr);
    }

    uint32_t n = 0;
    vkGetSwapchainImagesKHR(s_dev, s_swapchain, &n, nullptr);
    s_sc_images.resize(n);
    vkGetSwapchainImagesKHR(s_dev, s_swapchain, &n, s_sc_images.data());
    s_sc_views.resize(n);
    s_sc_render_done.resize(n);
    for (uint32_t i = 0; i < n; i++) {
        VkImageViewCreateInfo vi = {};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = s_sc_images[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = s_sc_format;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(s_dev, &vi, nullptr, &s_sc_views[i]));
        VkSemaphoreCreateInfo si = {};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VK_CHECK(vkCreateSemaphore(s_dev, &si, nullptr, &s_sc_render_done[i]));
    }
    s_sc_extent = ext;
    s_sc_dirty = false;
    static const char* const mode_names[] = { "IMMEDIATE", "MAILBOX", "FIFO", "FIFO_RELAXED" };
    sysLogPrintf(LOG_NOTE, "VK: swapchain %ux%u, %u images, format %d, %s", ext.width, ext.height, n, (int)s_sc_format,
                 (int)s_sc_mode < 4 ? mode_names[s_sc_mode] : "?");
    return true;
}

bool recreate_swapchain(void) {
    vkDeviceWaitIdle(s_dev);
    destroy_swapchain_views();
    return create_swapchain();
}

void create_frames(void) {
    for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
        Frame& f = s_frames[i];
        VkCommandPoolCreateInfo pci = {};
        pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pci.queueFamilyIndex = s_qfam;
        VK_CHECK(vkCreateCommandPool(s_dev, &pci, nullptr, &f.pool));
        VkCommandBufferAllocateInfo ai = {};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = f.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(s_dev, &ai, &f.cmd));
        VkFenceCreateInfo fci = {};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(s_dev, &fci, nullptr, &f.fence));
        VkSemaphoreCreateInfo si = {};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VK_CHECK(vkCreateSemaphore(s_dev, &si, nullptr, &f.acquired));
    }
}

void image_barrier(VkCommandBuffer cmd, VkImage img, VkImageLayout from, VkImageLayout to,
                   VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                   VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
    VkImageMemoryBarrier2 b = {};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.srcStageMask = src_stage;
    b.srcAccessMask = src_access;
    b.dstStageMask = dst_stage;
    b.dstAccessMask = dst_access;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    VkDependencyInfo di = {};
    di.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    di.imageMemoryBarrierCount = 1;
    di.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &di);
}

/* ---------------- GfxRenderingAPI ---------------- */

const char* vk_get_name(void) { return "Vulkan"; }
int vk_get_max_texture_size(void) { return 16384; }
struct GfxClipParameters vk_get_clip_parameters(void) {
    return { false, s_cur_fb < (int)s_fbs.size() ? s_fbs[s_cur_fb].invert_y : false };
}

void vk_unload_shader(struct ShaderProgram*) {}
void vk_load_shader_with(struct ShaderProgram* p, uint32_t, float, bool, bool) {
    if (p) s_prg = p;
}
void vk_load_shader(struct ShaderProgram* p) {
    vk_load_shader_with(p, 0, 1.0f, false, false);
}

struct ShaderProgram* vk_create_and_load_new_shader(uint64_t id0, uint32_t id1) {
    struct CCFeatures f;
    memset(&f, 0, sizeof f);
    gfx_cc_get_features(id0, id1, &f);
    ShaderProgram* p = new ShaderProgram();
    p->id0 = id0;
    p->id1 = id1;
    p->num_inputs = (uint8_t)f.num_inputs;
    p->used_textures[0] = f.used_textures[0];
    p->used_textures[1] = f.used_textures[1];
    s_progs[std::make_pair(id0, id1)] = p;
    vk_load_shader(p);
    return p;
}

struct ShaderProgram* vk_lookup_shader(uint64_t id0, uint32_t id1) {
    auto it = s_progs.find(std::make_pair(id0, id1));
    return it == s_progs.end() ? nullptr : it->second;
}

void vk_shader_get_info(struct ShaderProgram* p, uint8_t* num_inputs, bool used_textures[2]) {
    *num_inputs = p->num_inputs;
    used_textures[0] = p->used_textures[0];
    used_textures[1] = p->used_textures[1];
}

void vk_clear_shaders(void) {
    for (auto& kv : s_progs) delete kv.second;
    s_progs.clear();
    s_prg = nullptr;
}

uint32_t vk_new_texture(void) { return s_next_tex++; }
void vk_select_texture(int, uint32_t, bool) {}
void vk_upload_texture(const uint8_t*, uint32_t, uint32_t, bool) {}
void vk_set_sampler_parameters(int, bool, uint32_t, uint32_t, bool) {}
void vk_set_depth_mode(bool, bool, bool, bool, uint16_t) {}
void vk_set_depth_range(float, float) {}
void vk_set_viewport(int, int, int, int) {}
void vk_set_scissor(int, int, int, int) {}
void vk_set_use_alpha(bool, bool) {}
void vk_draw_triangles(float*, size_t, size_t) {}

void vk_init(void) {
    if (s_up) return;
    if (!load_loader()) sysFatalError("Vulkan: no usable Vulkan loader (GE_RENDERER=vk)");
    create_instance(true);
    SDL_Window* w = (SDL_Window*)gfx_sdl_get_sdl_window();
    if (!SDL_Vulkan_CreateSurface(w, s_instance, &s_surface)) {
        sysFatalError("Vulkan: SDL_Vulkan_CreateSurface failed:\n%s", SDL_GetError());
    }
    s_phys = pick_device(s_instance, s_surface, &s_qfam);
    if (s_phys == VK_NULL_HANDLE) sysFatalError("Vulkan: no device with Vulkan 1.3 and dynamic rendering presents to this window");
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(s_phys, &props);
        snprintf(s_renderer_str, sizeof s_renderer_str, "%s (Vulkan)", props.deviceName);
        sysLogPrintf(LOG_NOTE, "VK: using %s, driver %08x, queue family %u", props.deviceName, props.driverVersion, s_qfam);
    }
    create_device();
    create_frames();
    create_swapchain();
    s_fbs.resize(1);   /* fb 0 = the window, as gfx_opengl_init */
    gfx_skyplus_failed = 1;   /* no GE+ sky pass yet: skyplus.c draws GE's own sky */
    s_up = true;
}

void vk_on_resize(void) {}
void vk_start_frame(void) {}
void vk_end_frame(void) {}
void vk_finish_render(void) {}

int vk_create_framebuffer(void) {
    const int i = (int)s_fbs.size();
    s_fbs.resize(i + 1);
    return i;
}

void vk_update_framebuffer_parameters(int fb_id, uint32_t width, uint32_t height, uint32_t msaa_level, bool invert_y,
                                      bool, bool has_depth_buffer, bool) {
    if (fb_id < 0 || fb_id >= (int)s_fbs.size()) return;
    Fb& f = s_fbs[fb_id];
    f.w = width < 1 ? 1 : width;
    f.h = height < 1 ? 1 : height;
    f.msaa = msaa_level;
    f.has_depth = has_depth_buffer;
    f.invert_y = invert_y;
}

bool vk_start_draw_to_framebuffer(int fb_id, float) {
    if (fb_id < (int)s_fbs.size()) {
        s_cur_fb = fb_id;
        return true;
    }
    return false;
}

void vk_copy_framebuffer(int, int, int, int, bool, bool) {}
void vk_clear_framebuffer(bool, bool) {}
void vk_clear_region(bool, bool, int, int, int, int) {}
void vk_resolve_msaa_color_buffer(int, int) {}
void* vk_get_framebuffer_texture_id(int fb_id) { return (void*)(uintptr_t)fb_id; }
void vk_select_texture_fb(int) {}
void vk_delete_texture(uint32_t) {}
void vk_set_texture_filter(enum FilteringMode mode) { s_filter = mode; }
enum FilteringMode vk_get_texture_filter(void) { return s_filter; }
void vk_set_mipmap_filter(enum MipmapFilteringMode) {}
void vk_set_anisotropy_level(int level) { s_aniso = level < 1 ? 1 : level > 16 ? 16 : level; }
int vk_get_max_anisotropy_level(void) { return 16; }
float* vk_stream_begin(size_t) { return nullptr; }   /* the caller fills its own buffer */
uint32_t vk_mesh_create(const float*, size_t) { return s_next_mesh++; }
void vk_mesh_delete(uint32_t) {}
void vk_mesh_draw(uint32_t, const struct GfxMeshDraw*) {}
bool vk_mesh_supported(uint64_t, uint32_t) { return false; }   /* no GPU-transform twins yet */

} // namespace

struct GfxRenderingAPI gfx_vulkan_api = {
    vk_get_name,
    vk_get_max_texture_size,
    vk_get_clip_parameters,
    vk_unload_shader,
    vk_load_shader,
    vk_create_and_load_new_shader,
    vk_lookup_shader,
    vk_shader_get_info,
    vk_clear_shaders,
    vk_new_texture,
    vk_select_texture,
    vk_upload_texture,
    vk_set_sampler_parameters,
    vk_set_depth_mode,
    vk_set_depth_range,
    vk_set_viewport,
    vk_set_scissor,
    vk_set_use_alpha,
    vk_draw_triangles,
    vk_init,
    vk_on_resize,
    vk_start_frame,
    vk_end_frame,
    vk_finish_render,
    vk_create_framebuffer,
    vk_update_framebuffer_parameters,
    vk_start_draw_to_framebuffer,
    vk_copy_framebuffer,
    vk_clear_framebuffer,
    vk_clear_region,
    vk_resolve_msaa_color_buffer,
    vk_get_framebuffer_texture_id,
    vk_select_texture_fb,
    vk_delete_texture,
    vk_set_texture_filter,
    vk_get_texture_filter,
    vk_set_mipmap_filter,
    vk_set_anisotropy_level,
    vk_get_max_anisotropy_level,
    vk_stream_begin,
    vk_load_shader_with,
    vk_mesh_create,
    vk_mesh_delete,
    vk_mesh_draw,
    vk_mesh_supported,
};

extern "C" {

bool gfx_vulkan_wanted(void) {
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("GE_RENDERER");
        on = (e != NULL && strcmp(e, "vk") == 0) ? 1 : 0;   /* GL stays the default */
    }
    return on != 0;
}

bool gfx_vulkan_probe(void) {
    if (!load_loader()) return false;
    /* a throwaway instance: is there a Vulkan 1.3 device with dynamic rendering? */
    VkApplicationInfo app = {};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    const char* port_ext = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    if (has_instance_ext(port_ext)) {
        ci.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        ci.enabledExtensionCount = 1;
        ci.ppEnabledExtensionNames = &port_ext;
    }
    VkInstance inst = VK_NULL_HANDLE;
    if (vkCreateInstance(&ci, nullptr, &inst) != VK_SUCCESS) {
        sysLogPrintf(LOG_WARNING, "VK: vkCreateInstance failed");
        return false;
    }
    volkLoadInstanceOnly(inst);
    uint32_t qfam = 0;
    const bool ok = pick_device(inst, VK_NULL_HANDLE, &qfam) != VK_NULL_HANDLE;
    vkDestroyInstance(inst, nullptr);
    if (!ok) sysLogPrintf(LOG_WARNING, "VK: no device with Vulkan 1.3, dynamic rendering and synchronization2");
    return ok;
}

const char* gfx_vulkan_renderer_string(void) {
    return s_renderer_str;
}

void gfx_vulkan_get_dimensions(uint32_t* w, uint32_t* h) {
    window_drawable_size(w, h);
    if (*w == 0 || *h == 0) {   /* minimised: keep the last size */
        *w = s_sc_extent.width ? s_sc_extent.width : 640;
        *h = s_sc_extent.height ? s_sc_extent.height : 480;
    }
}

void gfx_vulkan_set_swap_interval(int interval) {
    interval = interval < 0 ? 0 : interval;
    if (interval != s_swap_interval) {
        s_swap_interval = interval;
        s_sc_dirty = true;   /* the present mode belongs to the swapchain */
    }
}

void gfx_vulkan_present(void) {
    if (!s_up || s_parked) return;
    {   /* resized since the swapchain was made: recreate before acquiring */
        uint32_t w, h;
        window_drawable_size(&w, &h);
        if (w == 0 || h == 0) return;   /* minimised */
        if (w != s_sc_extent.width || h != s_sc_extent.height) s_sc_dirty = true;
    }
    if (s_sc_dirty || s_swapchain == VK_NULL_HANDLE) {
        if (!recreate_swapchain()) return;
    }

    Frame& f = s_frames[s_frame_idx];
    VK_CHECK(vkWaitForFences(s_dev, 1, &f.fence, VK_TRUE, UINT64_MAX));

    uint32_t img = 0;
    VkResult r = vkAcquireNextImageKHR(s_dev, s_swapchain, UINT64_MAX, f.acquired, VK_NULL_HANDLE, &img);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        s_sc_dirty = true;
        return;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) VK_CHECK(r);
    VK_CHECK(vkResetFences(s_dev, 1, &f.fence));

    VK_CHECK(vkResetCommandPool(s_dev, f.pool, 0));
    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(f.cmd, &bi));

    image_barrier(f.cmd, s_sc_images[img], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    {   /* step 1: a fixed clear colour (no drawing yet) */
        VkRenderingAttachmentInfo ca = {};
        ca.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        ca.imageView = s_sc_views[img];
        ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        ca.clearValue.color = { { 0.10f, 0.18f, 0.32f, 1.0f } };
        VkRenderingInfo ri = {};
        ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea.extent = s_sc_extent;
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &ca;
        vkCmdBeginRendering(f.cmd, &ri);
        vkCmdEndRendering(f.cmd);
    }
    image_barrier(f.cmd, s_sc_images[img], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_NONE, 0);
    VK_CHECK(vkEndCommandBuffer(f.cmd));

    VkSemaphoreSubmitInfo wait = {};
    wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    wait.semaphore = f.acquired;
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphoreSubmitInfo signal = {};
    signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal.semaphore = s_sc_render_done[img];
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo cbi = {};
    cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cbi.commandBuffer = f.cmd;
    VkSubmitInfo2 si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    si.waitSemaphoreInfoCount = 1;
    si.pWaitSemaphoreInfos = &wait;
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &cbi;
    si.signalSemaphoreInfoCount = 1;
    si.pSignalSemaphoreInfos = &signal;
    VK_CHECK(vkQueueSubmit2(s_queue, 1, &si, f.fence));

    VkPresentInfoKHR pi = {};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &s_sc_render_done[img];
    pi.swapchainCount = 1;
    pi.pSwapchains = &s_swapchain;
    pi.pImageIndices = &img;
    r = vkQueuePresentKHR(s_queue, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
        s_sc_dirty = true;
    } else if (r != VK_SUCCESS) {
        VK_CHECK(r);
    }
    s_frame_idx = (s_frame_idx + 1) % FRAMES_IN_FLIGHT;
}

void gfx_vulkan_park(void) {
    if (!s_up || s_parked) return;
    vkDeviceWaitIdle(s_dev);
    s_parked = true;
    destroy_swapchain_views();
    if (s_swapchain) vkDestroySwapchainKHR(s_dev, s_swapchain, nullptr);
    s_swapchain = VK_NULL_HANDLE;
    for (Frame& f : s_frames) {
        vkDestroySemaphore(s_dev, f.acquired, nullptr);
        vkDestroyFence(s_dev, f.fence, nullptr);
        vkDestroyCommandPool(s_dev, f.pool, nullptr);
    }
    vkDestroyDevice(s_dev, nullptr);
    s_dev = VK_NULL_HANDLE;
    vkDestroySurfaceKHR(s_instance, s_surface, nullptr);
    if (s_messenger) vkDestroyDebugUtilsMessengerEXT(s_instance, s_messenger, nullptr);
    vkDestroyInstance(s_instance, nullptr);
    s_instance = VK_NULL_HANDLE;
    s_up = false;
    sysLogPrintf(LOG_NOTE, "VK: backend parked (device idle, swapchain, device and instance destroyed); "
                 "validation: %d error(s), %d warning(s)", s_validation_errors, s_validation_warnings);
}

/* Passes outside the rendering API: not drawn yet (step 4 of the issue). */
void gfx_vulkan_fxaa_pass(int) {}
int gfx_vulkan_skyplus_stage(const struct PortSkyPlus*, const int[4], float, float) { return 0; }
void gfx_vulkan_skyplus_pass(int) {}
void gfx_vulkan_interp_store(int, uint32_t, uint32_t) {}
void gfx_vulkan_interp_show(int) {}
void gfx_vulkan_interp_sync(void) {}
void gfx_vulkan_frame_count_rewind(void) {}
void gfx_vulkan_perf_report(double) {}

} // extern "C"

#endif /* GE_VULKAN */
