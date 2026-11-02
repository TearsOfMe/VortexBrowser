/*
 * VortexBrowse - CachyOS Native SIMD Pixel & Audio Helper (x86_64 -O3 -march=native)
 * Converts RGB24 buffers from Headless Chromium into Big-Endian AmigaOS 4 formats:
 *   - FMT_ARGB32 (1): PIXF_A8R8G8B8 (0xFF, R, G, B in memory)
 *   - FMT_RGB565 (2): PIXF_R5G6B5   (Big-Endian 16-bit R5G6B5 in memory)
 * Also computes dirty scanline bands (CODEC_LZ4_SLICE) and checks S16BE audio silence.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

void rgb24_to_be_argb32(const uint8_t * restrict src_rgb, uint8_t * restrict dst_argb, size_t num_pixels)
{
    for (size_t i = 0; i < num_pixels; i++) {
        dst_argb[i * 4 + 0] = 0xFF;
        dst_argb[i * 4 + 1] = src_rgb[i * 3 + 0];
        dst_argb[i * 4 + 2] = src_rgb[i * 3 + 1];
        dst_argb[i * 4 + 3] = src_rgb[i * 3 + 2];
    }
}

void rgb24_to_be_rgb565(const uint8_t * restrict src_rgb, uint8_t * restrict dst_rgb565, size_t num_pixels)
{
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

    /* Scan from top down to find first changed row */
    int min_y = 0;
    while (min_y < height) {
        const uint8_t *c_row = cur_buf + (size_t)min_y * row_bytes;
        const uint8_t *p_row = prev_buf + (size_t)min_y * row_bytes;
        if (memcmp(c_row, p_row, row_bytes) != 0) {
            break;
        }
        min_y++;
    }

    /* Entire frame is identical */
    if (min_y >= height) {
        *out_y_start = 0;
        *out_slice_h = 0;
        return 0;
    }

    /* Scan from bottom up to find last changed row */
    int max_y = height - 1;
    while (max_y > min_y) {
        const uint8_t *c_row = cur_buf + (size_t)max_y * row_bytes;
        const uint8_t *p_row = prev_buf + (size_t)max_y * row_bytes;
        if (memcmp(c_row, p_row, row_bytes) != 0) {
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
        if (memcmp(c_row, p_row, row_bytes) != 0) {
            break;
        }
        min_y++;
    }

    if (min_y >= height) {
        *out_x = 0; *out_y = 0; *out_w = 0; *out_h = 0;
        return 0; /* Identical frame */
    }

    /* 2. Find bottom changed row */
    int max_y = height - 1;
    while (max_y > min_y) {
        const uint8_t *c_row = cur_buf  + (size_t)max_y * row_bytes;
        const uint8_t *p_row = prev_buf + (size_t)max_y * row_bytes;
        if (memcmp(c_row, p_row, row_bytes) != 0) {
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

            for (int x = 0; x < min_x; x++) {
                if (c[x] != p[x]) {
                    min_x = x;
                    break;
                }
            }
            for (int x = width - 1; x > max_x; x--) {
                if (c[x] != p[x]) {
                    max_x = x;
                    break;
                }
            }
        }
    } else if (bpp == 4) {
        for (int y = min_y; y <= max_y; y++) {
            const uint32_t *c = (const uint32_t *)(cur_buf  + (size_t)y * row_bytes);
            const uint32_t *p = (const uint32_t *)(prev_buf + (size_t)y * row_bytes);

            for (int x = 0; x < min_x; x++) {
                if (c[x] != p[x]) {
                    min_x = x;
                    break;
                }
            }
            for (int x = width - 1; x > max_x; x--) {
                if (c[x] != p[x]) {
                    max_x = x;
                    break;
                }
            }
        }
    } else {
        /* bpp == 1 (YUV420P): compare byte-by-byte, align to 2px for clean chroma */
        for (int y = min_y; y <= max_y; y++) {
            const uint8_t *c = cur_buf  + (size_t)y * row_bytes;
            const uint8_t *p = prev_buf + (size_t)y * row_bytes;

            for (int x = 0; x < min_x; x++) {
                if (c[x] != p[x]) {
                    min_x = x;
                    break;
                }
            }
            for (int x = width - 1; x > max_x; x--) {
                if (c[x] != p[x]) {
                    max_x = x;
                    break;
                }
            }
        }
        /* Align to even pixel boundary for chroma plane alignment */
        min_x &= ~1;
        if (max_x < min_x) max_x = min_x;
        if (!((max_x - min_x + 1) & 1)) {
            if (max_x + 1 < width) max_x++;
        }
    }

    /* Align min_x down to multiple of 2 for clean pixel alignment */
    min_x &= ~1;
    if (max_x < min_x) max_x = min_x;
    if (!((max_x - min_x + 1) & 1)) {
        if (max_x + 1 < width) max_x++;
    }

    int rw = max_x - min_x + 1;
    int rh = max_y - min_y + 1;
    if (min_x + rw > width) rw = width - min_x;

    /* Update prev_buf with the dirty rows */
    memcpy(
        prev_buf + (size_t)min_y * row_bytes,
        cur_buf  + (size_t)min_y * row_bytes,
        (size_t)rh * row_bytes
    );

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
