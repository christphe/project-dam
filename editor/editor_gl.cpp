#include "editor_gl.h"

#include <cstring>

#include "glad/glad.h"

void editorGlClear(int w, int h, float r, float g, float b)
{
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void editorGlScissor(int x, int y, int w, int h)
{
    glViewport(x, y, w, h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, w, h);
}

void editorGlScissorOff() { glDisable(GL_SCISSOR_TEST); }

bool editorGlReadWindow(int w, int h, std::vector<uint8_t> &rgba)
{
    std::vector<uint8_t> raw((size_t)w * h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
    if (glGetError() != GL_NO_ERROR) return false;
    rgba.resize(raw.size());
    for (int y = 0; y < h; y++)
        memcpy(&rgba[(size_t)y * w * 4], &raw[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
    return true;
}

bool editorGlReadRect(int winH, int x, int y, int w, int h, std::vector<uint8_t> &rgba)
{
    std::vector<uint8_t> raw((size_t)w * h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, winH - (y + h), w, h, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
    if (glGetError() != GL_NO_ERROR) return false;
    rgba.resize(raw.size());
    for (int r = 0; r < h; r++)
        memcpy(&rgba[(size_t)r * w * 4], &raw[(size_t)(h - 1 - r) * w * 4], (size_t)w * 4);
    return true;
}

uint32_t editorGlUploadTexture(int w, int h, const uint8_t *rgba)
{
    GLuint t = 0;
    glGenTextures(1, &t);
    GLint prev = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prev);
    return t;
}

void editorGlDeleteTexture(uint32_t id)
{
    GLuint t = id;
    glDeleteTextures(1, &t);
}
