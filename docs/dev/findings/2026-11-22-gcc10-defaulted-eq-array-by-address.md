# GCC 10 compares an array member of a defaulted operator== by address

**Trap.** `TextureCacheKey` (`port/fast3d/gfx_pc.h`) had `bool operator==(const TextureCacheKey&) const noexcept = default;` and an array member `palette_addrs[2]`. GCC 10 (focal's `g++-10-aarch64-linux-gnu`, which the arm64 handheld build moved to with its Ubuntu 20.04 base) compiles the array comparison to a compare of the two objects' addresses (`cmp x2, x1` in `-O2 -S` output). Two keys built separately are therefore never equal: every texture-cache lookup missed and the 1024-entry cache churned.

**Symptom.** After the toolchain change, Dam on an RG34XX fell from 60 to 8.6 fps with nothing else changed: `GE_PERFSTAT` showed texture import 25 ms/frame, 282 uploads (~1.9 MiB) and the mesh cache's `tex-evicted` 74 per frame. The same source built with GCC 12 (Debian bookworm) had 0 uploads per frame.

**Fix.** `operator==` written out member by member. Any other key with an array member would need the same; `ColorCombinerKey` (`operator<=>`, no array) is fine.

**How to catch it.** A cache hit rate that drops to ~0 right after a compiler change: compile a two-line repro with `-S` before suspecting the cache logic.
