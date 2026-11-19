#include "renderer_altivec.h"
#include <altivec.h>
#include <stdint.h>
#include <string.h>

void altivec_fast_memcpy(void * restrict dst, const void * restrict src, size_t n)
{
    uint8_t * restrict d = (uint8_t *)dst;
    const uint8_t * restrict s = (const uint8_t *)src;
    if (n < 64) {
        memcpy(d, s, n);
        return;
    }

    uintptr_t mis_d = (uintptr_t)d & 15U;
    if (mis_d) {
        size_t pre = 16U - mis_d;
        memcpy(d, s, pre);
        d += pre;
        s += pre;
        n -= pre;
    }

    if (((uintptr_t)s & 15U) == 0U) {
        while (n >= 64) {
            vector unsigned char v0 = vec_ld(0,  s);
            vector unsigned char v1 = vec_ld(16, s);
            vector unsigned char v2 = vec_ld(32, s);
            vector unsigned char v3 = vec_ld(48, s);
            vec_st(v0, 0,  d);
            vec_st(v1, 16, d);
            vec_st(v2, 32, d);
            vec_st(v3, 48, d);
            s += 64;
            d += 64;
            n -= 64;
        }
        while (n >= 16) {
            vector unsigned char v = vec_ld(0, s);
            vec_st(v, 0, d);
            s += 16;
            d += 16;
            n -= 16;
        }
    } else if (n >= 32) {
        /* d is 16-byte aligned, s is unaligned: permute across adjacent loads.
         * Stop before the final 16 bytes so vec_ld never reads past s + n - 1. */
        vector unsigned char perm = vec_lvsl(0, s);
        vector unsigned char v0   = vec_ld(0, s);
        while (n >= 80) {
            vector unsigned char v1 = vec_ld(16, s);
            vector unsigned char v2 = vec_ld(32, s);
            vector unsigned char v3 = vec_ld(48, s);
            vector unsigned char v4 = vec_ld(64, s);
            vec_st(vec_perm(v0, v1, perm), 0,  d);
            vec_st(vec_perm(v1, v2, perm), 16, d);
            vec_st(vec_perm(v2, v3, perm), 32, d);
            vec_st(vec_perm(v3, v4, perm), 48, d);
            v0 = v4;
            s += 64;
            d += 64;
            n -= 64;
        }
        while (n >= 32) {
            vector unsigned char v1 = vec_ld(16, s);
            vec_st(vec_perm(v0, v1, perm), 0, d);
            v0 = v1;
            s += 16;
            d += 16;
            n -= 16;
        }
    }

    if (n) {
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
