#include "renderer_altivec.h"
#include <altivec.h>
#include <string.h>

void altivec_fast_memcpy(void * restrict dst, const void * restrict src, size_t n)
{
    uint8_t * restrict d = (uint8_t *)dst;
    const uint8_t * restrict s = (const uint8_t *)src;
    if ((((uintptr_t)d | (uintptr_t)s) & 15) == 0) {
        size_t n16 = n >> 4;
        for (size_t i = 0; i < n16; i++) {
            vector unsigned char v = vec_ld(0, s + (i << 4));
            vec_st(v, 0, d + (i << 4));
        }
        size_t rem = n & 15;
        if (rem) {
            memcpy(d + (n - rem), s + (n - rem), rem);
        }
    } else {
        memcpy(d, s, n);
    }
}

void altivec_convert_rgb565_to_argb32(
    const uint16_t * restrict src16,
    uint32_t       * restrict dst32,
    uint16_t                  w
)
{
    for (uint16_t i = 0; i < w; i++) {
        uint16_t v = src16[i];
        uint32_t r = (v >> 11) & 0x1FU;
        uint32_t g = (v >> 5)  & 0x3FU;
        uint32_t b =  v        & 0x1FU;
        uint32_t r8 = (r * 527U + 23U) >> 6;
        uint32_t g8 = (g * 259U + 33U) >> 6;
        uint32_t b8 = (b * 527U + 23U) >> 6;
        dst32[i] = (0xFF000000U) | (r8 << 16) | (g8 << 8) | b8;
    }
}

void altivec_convert_argb32_to_rgb565(
    const uint32_t * restrict src32,
    uint16_t       * restrict dst16,
    uint16_t                  w
)
{
    for (uint16_t x = 0; x < w; x++) {
        uint32_t c = src32[x];
        uint16_t r5 = (uint16_t)((c >> 19) & 0x1FU);
        uint16_t g6 = (uint16_t)((c >> 10) & 0x3FU);
        uint16_t b5 = (uint16_t)((c >> 3)  & 0x1FU);
        dst16[x] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
    }
}
