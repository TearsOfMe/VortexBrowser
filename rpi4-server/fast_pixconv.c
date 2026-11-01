/*
 * VortexBrowser - CachyOS HP EliteDesk 805 G6 (AMD Ryzen PRO / Radeon Vega)
 * Ultra-Fast AVX2 + OpenMP + libturbojpeg + liblz4 Native Streaming Pipeline
 *
 * Specifically tuned for AMD Zen 2 (znver2) 8-thread APU to deliver
 * Full HD (1920x1080) @ 25+ FPS to AmigaOS 4.1 PowerPC:
 *   - Direct C-level JPEG -> Planar YUV420P (tjDecompressToYUV2 SIMD)
 *   - 256-bit AVX2 vector dirty-frame & 2D dirty-bounding-box detection
 *   - Direct C-level LZ4 fast block compression into wire packet buffer
 *   - Zero intermediate Python byte allocations per frame
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#endif

#ifdef _OPENMP
#include <omp.h>
#endif

#ifdef HAVE_TURBOJPEG_LZ4
#include <turbojpeg.h>
#include <lz4.h>
#endif

#define MSG_FRAME       0x01
#define FMT_ARGB32      1
#define FMT_RGB565      2
#define FMT_YUV420P     3

#define CODEC_RAW       0
#define CODEC_LZ4       1
#define CODEC_LZ4_SLICE 2
#define CODEC_JPEG      3
#define CODEC_LZ4_RECT  4

/* Fast AVX2 256-bit memory comparison returning 0 if equal, 1 if different */
static inline int fast_memeq(const uint8_t *a, const uint8_t *b, size_t len)
{
#if defined(__AVX2__)
    size_t i = 0;
    for (; i + 64 <= len; i += 64) {
        __m256i va0 = _mm256_loadu_si256((const __m256i *)(a + i));
        __m256i vb0 = _mm256_loadu_si256((const __m256i *)(b + i));
        __m256i xor0 = _mm256_xor_si256(va0, vb0);
        __m256i va1 = _mm256_loadu_si256((const __m256i *)(a + i + 32));
        __m256i vb1 = _mm256_loadu_si256((const __m256i *)(b + i + 32));
        __m256i xor1 = _mm256_xor_si256(va1, vb1);
        __m256i comb = _mm256_or_si256(xor0, xor1);
        if (!_mm256_testz_si256(comb, comb)) {
            return 0;
        }
    }
    for (; i < len; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
#else
    return memcmp(a, b, len) == 0;
#endif
}

void rgb24_to_be_argb32(const uint8_t * restrict src_rgb, uint8_t * restrict dst_argb, size_t num_pixels)
{
    #pragma omp parallel for schedule(static) if (num_pixels >= 500000)
    for (size_t i = 0; i < num_pixels; i++) {
        dst_argb[i * 4 + 0] = 0xFF;
        dst_argb[i * 4 + 1] = src_rgb[i * 3 + 0];
        dst_argb[i * 4 + 2] = src_rgb[i * 3 + 1];
        dst_argb[i * 4 + 3] = src_rgb[i * 3 + 2];
    }
}

void rgb24_to_be_rgb565(const uint8_t * restrict src_rgb, uint8_t * restrict dst_rgb565, size_t num_pixels)
{
    #pragma omp parallel for schedule(static) if (num_pixels >= 500000)
    for (size_t i = 0; i < num_pixels; i++) {
        uint16_t r = src_rgb[i * 3 + 0];
        uint16_t g = src_rgb[i * 3 + 1];
        uint16_t b = src_rgb[i * 3 + 2];
        uint16_t v = (uint16_t)(((r & 0xF8U) << 8) | ((g & 0xFCU) << 3) | (b >> 3));
        dst_rgb565[i * 2 + 0] = (uint8_t)(v >> 8);
        dst_rgb565[i * 2 + 1] = (uint8_t)(v & 0xFFU);
    }
}

void rgb24_to_yuv420p(
    const uint8_t * restrict src_rgb,
    uint8_t       * restrict dst_yuv,
    int                      width,
    int                      height
)
{
    uint8_t *dst_y = dst_yuv;
    uint8_t *dst_u = dst_yuv + (size_t)width * (size_t)height;
    uint8_t *dst_v = dst_u   + ((size_t)width / 2) * ((size_t)height / 2);

    /* 1. Full-resolution Y (Luma) Plane (ITU-R BT.601) */
    #pragma omp parallel for schedule(static) if (width * height >= 500000)
    for (int y = 0; y < height; y++) {
        const uint8_t *rgb_row = src_rgb + (size_t)y * (size_t)width * 3;
        uint8_t       *y_row   = dst_y   + (size_t)y * (size_t)width;
        for (int x = 0; x < width; x++) {
            int r = rgb_row[x * 3 + 0];
            int g = rgb_row[x * 3 + 1];
            int b = rgb_row[x * 3 + 2];
            int y_val = (77 * r + 150 * g + 29 * b + 128) >> 8;
            y_row[x] = (uint8_t)((y_val < 0) ? 0 : ((y_val > 255) ? 255 : y_val));
        }
    }

    /* 2. 2x2 Subsampled U (Cb) and V (Cr) Planes */
    int half_w = width / 2;
    int half_h = height / 2;
    #pragma omp parallel for schedule(static) if (width * height >= 500000)
    for (int y2 = 0; y2 < half_h; y2++) {
        const uint8_t *row0 = src_rgb + (size_t)(y2 * 2)     * (size_t)width * 3;
        const uint8_t *row1 = src_rgb + (size_t)(y2 * 2 + 1) * (size_t)width * 3;
        uint8_t       *u_row = dst_u   + (size_t)y2 * (size_t)half_w;
        uint8_t       *v_row = dst_v   + (size_t)y2 * (size_t)half_w;
        for (int x2 = 0; x2 < half_w; x2++) {
            int r = (row0[x2 * 6 + 0] + row0[x2 * 6 + 3] + row1[x2 * 6 + 0] + row1[x2 * 6 + 3] + 2) >> 2;
            int g = (row0[x2 * 6 + 1] + row0[x2 * 6 + 4] + row1[x2 * 6 + 1] + row1[x2 * 6 + 4] + 2) >> 2;
            int b = (row0[x2 * 6 + 2] + row0[x2 * 6 + 5] + row1[x2 * 6 + 2] + row1[x2 * 6 + 5] + 2) >> 2;

            int u_val = ((-43 * r - 85 * g + 128 * b + 128) >> 8) + 128;
            int v_val = ((128 * r - 107 * g - 21 * b + 128) >> 8) + 128;

            u_row[x2] = (uint8_t)((u_val < 0) ? 0 : ((u_val > 255) ? 255 : u_val));
            v_row[x2] = (uint8_t)((v_val < 0) ? 0 : ((v_val > 255) ? 255 : v_val));
        }
    }
}

int find_and_update_dirty_slice(
    const uint8_t * restrict cur_buf,
    uint8_t * restrict prev_buf,
    int width,
    int height,
    int bpp,
    int *out_y_start,
    int *out_slice_h
)
{
    size_t row_bytes = (size_t)width * (size_t)bpp;

    int min_y = 0;
    while (min_y < height) {
        const uint8_t *c_row = cur_buf + (size_t)min_y * row_bytes;
        const uint8_t *p_row = prev_buf + (size_t)min_y * row_bytes;
        if (!fast_memeq(c_row, p_row, row_bytes)) {
            break;
        }
        min_y++;
    }

    if (min_y >= height) {
        *out_y_start = 0;
        *out_slice_h = 0;
        return 0;
    }

    int max_y = height - 1;
    while (max_y > min_y) {
        const uint8_t *c_row = cur_buf + (size_t)max_y * row_bytes;
        const uint8_t *p_row = prev_buf + (size_t)max_y * row_bytes;
        if (!fast_memeq(c_row, p_row, row_bytes)) {
            break;
        }
        max_y--;
    }

    int slice_h = max_y - min_y + 1;
    memcpy(
        prev_buf + (size_t)min_y * row_bytes,
        cur_buf  + (size_t)min_y * row_bytes,
        (size_t)slice_h * row_bytes
    );

    *out_y_start = min_y;
    *out_slice_h = slice_h;
    return 1;
}

int find_and_update_dirty_rect(
    const uint8_t * restrict cur_buf,
    uint8_t * restrict prev_buf,
    int width,
    int height,
    int bpp,
    int *out_x,
    int *out_y,
    int *out_w,
    int *out_h
)
{
    size_t row_bytes = (size_t)width * (size_t)bpp;

    /* 1. Find top changed row */
    int min_y = 0;
    while (min_y < height) {
        const uint8_t *c_row = cur_buf  + (size_t)min_y * row_bytes;
        const uint8_t *p_row = prev_buf + (size_t)min_y * row_bytes;
        if (!fast_memeq(c_row, p_row, row_bytes)) {
            break;
        }
        min_y++;
    }

    if (min_y >= height) {
        if (bpp == 1) {
            /* Check if U/V chroma planes changed even if Y luma plane was identical */
            size_t u_off = (size_t)width * (size_t)height;
            size_t uv_len = u_off / 2;
            if (!fast_memeq(cur_buf + u_off, prev_buf + u_off, uv_len)) {
                min_y = height - 2;
                if (min_y < 0) min_y = 0;
            } else {
                *out_x = 0; *out_y = 0; *out_w = 0; *out_h = 0;
                return 0; /* Identical frame */
            }
        } else {
            *out_x = 0; *out_y = 0; *out_w = 0; *out_h = 0;
            return 0; /* Identical frame */
        }
    }

    /* 2. Find bottom changed row */
    int max_y = height - 1;
    while (max_y > min_y) {
        const uint8_t *c_row = cur_buf  + (size_t)max_y * row_bytes;
        const uint8_t *p_row = prev_buf + (size_t)max_y * row_bytes;
        if (!fast_memeq(c_row, p_row, row_bytes)) {
            break;
        }
        max_y--;
    }

    /* 3. Find horizontal bounding box across the changed rows [min_y .. max_y] */
    int min_x = width - 1;
    int max_x = 0;

    if (bpp == 2) {
        for (int y = min_y; y <= max_y; y++) {
            const uint16_t *c = (const uint16_t *)(cur_buf  + (size_t)y * row_bytes);
            const uint16_t *p = (const uint16_t *)(prev_buf + (size_t)y * row_bytes);

            int x = 0;
            while (x + 4 <= min_x) {
                if (*(const uint64_t *)(c + x) != *(const uint64_t *)(p + x)) break;
                x += 4;
            }
            for (; x < min_x; x++) {
                if (c[x] != p[x]) { min_x = x; break; }
            }

            int xr = width - 1;
            while (xr - 4 >= max_x) {
                if (*(const uint64_t *)(c + xr - 3) != *(const uint64_t *)(p + xr - 3)) break;
                xr -= 4;
            }
            for (; xr > max_x; xr--) {
                if (c[xr] != p[xr]) { max_x = xr; break; }
            }
        }
    } else if (bpp == 4) {
        for (int y = min_y; y <= max_y; y++) {
            const uint32_t *c = (const uint32_t *)(cur_buf  + (size_t)y * row_bytes);
            const uint32_t *p = (const uint32_t *)(prev_buf + (size_t)y * row_bytes);

            int x = 0;
            while (x + 2 <= min_x) {
                if (*(const uint64_t *)(c + x) != *(const uint64_t *)(p + x)) break;
                x += 2;
            }
            for (; x < min_x; x++) {
                if (c[x] != p[x]) { min_x = x; break; }
            }

            int xr = width - 1;
            while (xr - 2 >= max_x) {
                if (*(const uint64_t *)(c + xr - 1) != *(const uint64_t *)(p + xr - 1)) break;
                xr -= 2;
            }
            for (; xr > max_x; xr--) {
                if (c[xr] != p[xr]) { max_x = xr; break; }
            }
        }
    } else {
        /* bpp == 1 (YUV420P): compare 8 bytes at a time, align to 2px for clean chroma */
        for (int y = min_y; y <= max_y; y++) {
            const uint8_t *c = cur_buf  + (size_t)y * row_bytes;
            const uint8_t *p = prev_buf + (size_t)y * row_bytes;

            int x = 0;
            while (x + 8 <= min_x) {
                if (*(const uint64_t *)(c + x) != *(const uint64_t *)(p + x)) break;
                x += 8;
            }
            for (; x < min_x; x++) {
                if (c[x] != p[x]) { min_x = x; break; }
            }

            int xr = width - 1;
            while (xr - 8 >= max_x) {
                if (*(const uint64_t *)(c + xr - 7) != *(const uint64_t *)(p + xr - 7)) break;
                xr -= 8;
            }
            for (; xr > max_x; xr--) {
                if (c[xr] != p[xr]) { max_x = xr; break; }
            }
        }
        /* Align Y coordinates to even pixel boundaries too for 4:2:0 U/V chroma planes */
        min_y &= ~1;
        if (max_y < min_y) max_y = min_y;
        if (!((max_y - min_y + 1) & 1)) {
            if (max_y + 1 < height) max_y++;
        }
    }

    if (bpp == 1) {
        /* Also check U and V chroma planes for any rows not already inside [min_y..max_y, min_x..max_x] */
        int half_w = width / 2;
        int half_h = height / 2;
        const uint8_t *cu = cur_buf  + (size_t)width * (size_t)height;
        const uint8_t *pu = prev_buf + (size_t)width * (size_t)height;
        const uint8_t *cv = cu + (size_t)half_w * (size_t)half_h;
        const uint8_t *pv = pu + (size_t)half_w * (size_t)half_h;

        int c_min_y = min_y / 2;
        int c_max_y = max_y / 2;
        for (int cy = 0; cy < c_min_y; cy++) {
            const uint8_t *ur_c = cu + (size_t)cy * half_w;
            const uint8_t *ur_p = pu + (size_t)cy * half_w;
            const uint8_t *vr_c = cv + (size_t)cy * half_w;
            const uint8_t *vr_p = pv + (size_t)cy * half_w;
            if (!fast_memeq(ur_c, ur_p, half_w) || !fast_memeq(vr_c, vr_p, half_w)) {
                min_y = cy * 2;
                break;
            }
        }
        for (int cy = half_h - 1; cy > c_max_y; cy--) {
            const uint8_t *ur_c = cu + (size_t)cy * half_w;
            const uint8_t *ur_p = pu + (size_t)cy * half_w;
            const uint8_t *vr_c = cv + (size_t)cy * half_w;
            const uint8_t *vr_p = pv + (size_t)cy * half_w;
            if (!fast_memeq(ur_c, ur_p, half_w) || !fast_memeq(vr_c, vr_p, half_w)) {
                max_y = cy * 2 + 1;
                break;
            }
        }
        for (int cy = min_y / 2; cy <= max_y / 2; cy++) {
            const uint8_t *ur_c = cu + (size_t)cy * half_w;
            const uint8_t *ur_p = pu + (size_t)cy * half_w;
            const uint8_t *vr_c = cv + (size_t)cy * half_w;
            const uint8_t *vr_p = pv + (size_t)cy * half_w;
            int lim_l = min_x / 2;
            for (int cx = 0; cx < lim_l; cx++) {
                if (ur_c[cx] != ur_p[cx] || vr_c[cx] != vr_p[cx]) {
                    min_x = cx * 2;
                    break;
                }
            }
            int lim_r = max_x / 2;
            for (int cx = half_w - 1; cx > lim_r; cx--) {
                if (ur_c[cx] != ur_p[cx] || vr_c[cx] != vr_p[cx]) {
                    max_x = cx * 2 + 1;
                    break;
                }
            }
        }
    }

    /* Align X & Y coordinates and dimensions to even pixel boundaries (required for 4:2:0 chroma) */
    min_x &= ~1;
    if (max_x < min_x) max_x = min_x;
    if (!((max_x - min_x + 1) & 1)) {
        if (max_x + 1 < width) max_x++;
    }

    min_y &= ~1;
    if (max_y < min_y) max_y = min_y;
    if (!((max_y - min_y + 1) & 1)) {
        if (max_y + 1 < height) max_y++;
    }

    int rw = (max_x - min_x + 2) & ~1;
    int rh = (max_y - min_y + 2) & ~1;
    if (min_x + rw > width)  rw = (width - min_x) & ~1;
    if (min_y + rh > height) rh = (height - min_y) & ~1;

    /* Update prev_buf with the dirty rows */
    memcpy(
        prev_buf + (size_t)min_y * row_bytes,
        cur_buf  + (size_t)min_y * row_bytes,
        (size_t)rh * row_bytes
    );

    if (bpp == 1) {
        /* Also update U and V chroma planes in prev_buf for YUV420P */
        size_t half_w = (size_t)width / 2;
        size_t u_off  = (size_t)width * (size_t)height;
        size_t v_off  = u_off + half_w * ((size_t)height / 2);
        size_t half_min_y = (size_t)min_y / 2;
        size_t half_rh    = ((size_t)rh + 1) / 2;

        memcpy(
            prev_buf + u_off + half_min_y * half_w,
            cur_buf  + u_off + half_min_y * half_w,
            half_rh * half_w
        );
        memcpy(
            prev_buf + v_off + half_min_y * half_w,
            cur_buf  + v_off + half_min_y * half_w,
            half_rh * half_w
        );
    }

    *out_x = min_x;
    *out_y = min_y;
    *out_w = rw;
    *out_h = rh;
    return 1;
}

void pack_sub_rect(
    const uint8_t * restrict src_buf,
    uint8_t * restrict dst_buf,
    int src_width,
    int bpp,
    int rx,
    int ry,
    int rw,
    int rh
)
{
    size_t src_stride     = (size_t)src_width * (size_t)bpp;
    size_t rect_row_bytes = (size_t)rw * (size_t)bpp;
    for (int y = 0; y < rh; y++) {
        const uint8_t *s = src_buf + ((size_t)(ry + y) * src_stride) + ((size_t)rx * (size_t)bpp);
        uint8_t       *d = dst_buf + ((size_t)y * rect_row_bytes);
        memcpy(d, s, rect_row_bytes);
    }
}

void pack_sub_rect_yuv420p(
    const uint8_t * restrict src_yuv,
    uint8_t       * restrict dst_buf,
    int                      src_width,
    int                      src_height,
    int                      rx,
    int                      ry,
    int                      rw,
    int                      rh
)
{
    /* 1. Y plane: rw x rh bytes */
    size_t y_stride = (size_t)src_width;
    for (int y = 0; y < rh; y++) {
        const uint8_t *s = src_yuv + (size_t)(ry + y) * y_stride + (size_t)rx;
        uint8_t       *d = dst_buf + (size_t)y * (size_t)rw;
        memcpy(d, s, rw);
    }

    /* 2. U and V planes in source */
    size_t half_w = (size_t)src_width / 2;
    size_t half_h = (size_t)src_height / 2;
    size_t u_plane_offset = (size_t)src_width * (size_t)src_height;
    size_t v_plane_offset = u_plane_offset + half_w * half_h;

    const uint8_t *src_u = src_yuv + u_plane_offset;
    const uint8_t *src_v = src_yuv + v_plane_offset;

    /* 3. U and V planes in destination */
    size_t sub_half_w = (size_t)rw / 2;
    size_t sub_half_h = (size_t)rh / 2;
    uint8_t *dst_u = dst_buf + (size_t)rw * (size_t)rh;
    uint8_t *dst_v = dst_u   + sub_half_w * sub_half_h;

    size_t half_rx = (size_t)rx / 2;
    size_t half_ry = (size_t)ry / 2;

    for (int y2 = 0; y2 < (int)sub_half_h; y2++) {
        const uint8_t *su = src_u + (half_ry + (size_t)y2) * half_w + half_rx;
        const uint8_t *sv = src_v + (half_ry + (size_t)y2) * half_w + half_rx;
        uint8_t       *du = dst_u + (size_t)y2 * sub_half_w;
        uint8_t       *dv = dst_v + (size_t)y2 * sub_half_w;
        memcpy(du, su, sub_half_w);
        memcpy(dv, sv, sub_half_w);
    }
}

int pcm_s16be_is_silent(const uint8_t *pcm, size_t byte_len, int threshold)
{
    size_t num_samples = byte_len / 2;
    for (size_t i = 0; i < num_samples; i++) {
        int16_t s = (int16_t)(((uint16_t)pcm[i * 2] << 8) | (uint16_t)pcm[i * 2 + 1]);
        int v = (s < 0) ? -(int)s : (int)s;
        if (v > threshold) {
            return 0; /* Not silent */
        }
    }
    return 1; /* Silent */
}

#ifdef HAVE_TURBOJPEG_LZ4
static __thread tjhandle s_tls_tjd = NULL;
static __thread uint8_t *s_tls_rgb_scratch = NULL;
static __thread size_t   s_tls_rgb_cap = 0;
static __thread uint8_t *s_tls_rect_scratch = NULL;
static __thread size_t   s_tls_rect_cap = 0;

static inline void write_msg_frame_hdr(
    uint8_t *dst,
    uint16_t w,
    uint16_t h,
    uint8_t  fmt,
    uint8_t  codec,
    uint32_t payload_len
)
{
    dst[0]  = MSG_FRAME;
    dst[1]  = (uint8_t)(w >> 8);
    dst[2]  = (uint8_t)(w & 0xFF);
    dst[3]  = (uint8_t)(h >> 8);
    dst[4]  = (uint8_t)(h & 0xFF);
    dst[5]  = fmt;
    dst[6]  = codec;
    dst[7]  = (uint8_t)((payload_len >> 24) & 0xFF);
    dst[8]  = (uint8_t)((payload_len >> 16) & 0xFF);
    dst[9]  = (uint8_t)((payload_len >> 8)  & 0xFF);
    dst[10] = (uint8_t)(payload_len & 0xFF);
}

static inline void write_rect_hdr(
    uint8_t *dst,
    uint16_t rx,
    uint16_t ry,
    uint16_t rw,
    uint16_t rh
)
{
    dst[0] = (uint8_t)(rx >> 8);
    dst[1] = (uint8_t)(rx & 0xFF);
    dst[2] = (uint8_t)(ry >> 8);
    dst[3] = (uint8_t)(ry & 0xFF);
    dst[4] = (uint8_t)(rw >> 8);
    dst[5] = (uint8_t)(rw & 0xFF);
    dst[6] = (uint8_t)(rh >> 8);
    dst[7] = (uint8_t)(rh & 0xFF);
}

/*
 * Single-call C-native Full HD frame decoder, dirty-rect differ, and LZ4 packet encoder.
 * Returns:
 *   > 0 : Total wire packet length written to dst_pkt (including 11-byte MsgFrame header)
 *     0 : Frame is identical to prev_buf (skip sending)
 *   < 0 : Error / dimension mismatch (caller can fallback)
 */
int encode_jpeg_to_frame_packet(
    const uint8_t *jpeg_buf,
    size_t         jpeg_len,
    int            width,
    int            height,
    int            pixel_format,
    int            force_full,
    int            allow_yuv_rect,
    int            lz4_accel,
    uint8_t       *prev_buf,
    uint8_t       *work_buf,
    uint8_t       *dst_pkt,
    int            dst_pkt_cap
)
{
    if (!jpeg_buf || jpeg_len == 0 || !prev_buf || !work_buf || !dst_pkt || dst_pkt_cap <= 20) {
        return -1;
    }

    if (!s_tls_tjd) {
        s_tls_tjd = tjInitDecompress();
        if (!s_tls_tjd) return -2;
    }

    int jw = 0, jh = 0, subsamp = 0, colorspace = 0;
    if (tjDecompressHeader3(s_tls_tjd, jpeg_buf, (unsigned long)jpeg_len, &jw, &jh, &subsamp, &colorspace) != 0) {
        return -3;
    }
    if (jw != width || jh != height) {
        return -4;
    }

    size_t num_pixels = (size_t)width * (size_t)height;
    size_t raw_frame_bytes = 0;
    int bpp = 1;

    if (pixel_format == FMT_YUV420P) {
        bpp = 1;
        raw_frame_bytes = (num_pixels * 3) / 2;
        if (subsamp == TJSAMP_420) {
            if (tjDecompressToYUV2(
                    s_tls_tjd,
                    jpeg_buf,
                    (unsigned long)jpeg_len,
                    work_buf,
                    width,
                    1,
                    height,
                    TJFLAG_FASTDCT | TJFLAG_FASTUPSAMPLE) != 0) {
                return -5;
            }
        } else {
            size_t needed_rgb = num_pixels * 3;
            if (s_tls_rgb_cap < needed_rgb) {
                uint8_t *nb = (uint8_t *)realloc(s_tls_rgb_scratch, needed_rgb);
                if (!nb) return -6;
                s_tls_rgb_scratch = nb;
                s_tls_rgb_cap = needed_rgb;
            }
            if (tjDecompress2(
                    s_tls_tjd,
                    jpeg_buf,
                    (unsigned long)jpeg_len,
                    s_tls_rgb_scratch,
                    width,
                    0,
                    height,
                    TJPF_RGB,
                    TJFLAG_FASTDCT | TJFLAG_FASTUPSAMPLE) != 0) {
                return -7;
            }
            rgb24_to_yuv420p(s_tls_rgb_scratch, work_buf, width, height);
        }
    } else {
        size_t needed_rgb = num_pixels * 3;
        if (s_tls_rgb_cap < needed_rgb) {
            uint8_t *nb = (uint8_t *)realloc(s_tls_rgb_scratch, needed_rgb);
            if (!nb) return -6;
            s_tls_rgb_scratch = nb;
            s_tls_rgb_cap = needed_rgb;
        }
        if (tjDecompress2(
                s_tls_tjd,
                jpeg_buf,
                (unsigned long)jpeg_len,
                s_tls_rgb_scratch,
                width,
                0,
                height,
                TJPF_RGB,
                TJFLAG_FASTDCT | TJFLAG_FASTUPSAMPLE) != 0) {
            return -7;
        }
        if (pixel_format == FMT_RGB565) {
            bpp = 2;
            raw_frame_bytes = num_pixels * 2;
            rgb24_to_be_rgb565(s_tls_rgb_scratch, work_buf, num_pixels);
        } else {
            bpp = 4;
            raw_frame_bytes = num_pixels * 4;
            rgb24_to_be_argb32(s_tls_rgb_scratch, work_buf, num_pixels);
        }
    }

    if (lz4_accel < 1) lz4_accel = 1;

    /* Check for dirty sub-rectangle if not forced full frame */
    if (!force_full) {
        if (pixel_format == FMT_YUV420P && !allow_yuv_rect) {
            if (fast_memeq(work_buf, prev_buf, raw_frame_bytes)) {
                return 0; /* Frame unchanged */
            }
            memcpy(prev_buf, work_buf, raw_frame_bytes);
        } else {
            int rx = 0, ry = 0, rw = 0, rh = 0;
            int changed = find_and_update_dirty_rect(work_buf, prev_buf, width, height, bpp, &rx, &ry, &rw, &rh);
            if (changed == 0) {
                return 0; /* Frame unchanged */
            }
            if (rw > 0 && rh > 0 && ((size_t)rw * (size_t)rh < (num_pixels * 85) / 100)) {
                size_t sub_bytes = (pixel_format == FMT_YUV420P)
                    ? (((size_t)rw * (size_t)rh * 3) / 2)
                    : ((size_t)rw * (size_t)rh * (size_t)bpp);
                if (s_tls_rect_cap < sub_bytes) {
                    uint8_t *nb = (uint8_t *)realloc(s_tls_rect_scratch, sub_bytes);
                    if (!nb) return -8;
                    s_tls_rect_scratch = nb;
                    s_tls_rect_cap = sub_bytes;
                }
                if (pixel_format == FMT_YUV420P) {
                    pack_sub_rect_yuv420p(work_buf, s_tls_rect_scratch, width, height, rx, ry, rw, rh);
                } else {
                    pack_sub_rect(work_buf, s_tls_rect_scratch, width, bpp, rx, ry, rw, rh);
                }
                int comp_len = LZ4_compress_fast(
                    (const char *)s_tls_rect_scratch,
                    (char *)(dst_pkt + 11 + 8),
                    (int)sub_bytes,
                    dst_pkt_cap - 19,
                    lz4_accel
                );
                if (comp_len > 0) {
                    write_rect_hdr(dst_pkt + 11, (uint16_t)rx, (uint16_t)ry, (uint16_t)rw, (uint16_t)rh);
                    write_msg_frame_hdr(dst_pkt, (uint16_t)width, (uint16_t)height, (uint8_t)pixel_format, CODEC_LZ4_RECT, (uint32_t)(8 + comp_len));
                    return 19 + comp_len;
                }
            }
        }
    } else {
        memcpy(prev_buf, work_buf, raw_frame_bytes);
    }

    int comp_len = LZ4_compress_fast(
        (const char *)work_buf,
        (char *)(dst_pkt + 11),
        (int)raw_frame_bytes,
        dst_pkt_cap - 11,
        lz4_accel
    );
    if (comp_len <= 0) {
        return -9;
    }

    write_msg_frame_hdr(dst_pkt, (uint16_t)width, (uint16_t)height, (uint8_t)pixel_format, CODEC_LZ4, (uint32_t)comp_len);
    return 11 + comp_len;
}
#endif
