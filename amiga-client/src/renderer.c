#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <graphics/gfx.h>
#include <graphics/composite.h>
#include <graphics/blitattr.h>
#include <gadgets/space.h>
#include <liblz4/lz4.h>
#include <jpeglib.h>
#include <setjmp.h>
#include <altivec.h>

#include "renderer.h"
#include "net.h"

/* Fast AltiVec-accelerated 128-bit SIMD memory copy for 16-byte aligned buffers */
static inline void altivec_memcpy_fast(void * restrict dst, const void * restrict src, size_t n)
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

void renderer_init(VortexApp *app)
{
    memset(&app->renderer, 0, sizeof(RendererState));
    app->renderer.src_width   = VORTEXBROWSE_DEFAULT_WIDTH;
    app->renderer.src_height  = VORTEXBROWSE_DEFAULT_HEIGHT;
    app->renderer.src_format  = FMT_RGB565;
}

static void free_source_bitmaps(RendererState *r)
{
    for (int i = 0; i < 2; i++) {
        if (r->src_bm[i]) {
            IGraphics->FreeBitMap(r->src_bm[i]);
            r->src_bm[i] = NULL;
        }
    }
}

static void free_comp_bitmap(RendererState *r)
{
    if (r->comp_bm) {
        IGraphics->FreeBitMap(r->comp_bm);
        r->comp_bm = NULL;
    }
    r->comp_width  = 0;
    r->comp_height = 0;
}

void renderer_cleanup(VortexApp *app)
{
    RendererState *r = &app->renderer;
    free_source_bitmaps(r);
    free_comp_bitmap(r);

    if (r->lz4_comp_buf) {
        free(r->lz4_comp_buf);
        r->lz4_comp_buf = NULL;
    }
    r->lz4_comp_cap = 0;
    if (r->raw_frame_buf) {
        free(r->raw_frame_buf);
        r->raw_frame_buf = NULL;
    }
    r->raw_frame_cap = 0;
    if (r->conv_argb_buf) {
        free(r->conv_argb_buf);
        r->conv_argb_buf = NULL;
    }
    r->conv_argb_cap = 0;
}

static BOOL ensure_comp_bitmap(VortexApp *app, uint16_t dw, uint16_t dh);

static BOOL ensure_source_bitmaps(VortexApp *app, uint16_t w, uint16_t h, uint8_t fmt)
{
    RendererState *r = &app->renderer;
    w &= ~1;
    h &= ~1;
    if (w < 16 || h < 16) return FALSE;

    if (r->src_bm[0] && r->src_width == w && r->src_height == h && r->src_format == fmt) {
        return TRUE;
    }

    free_source_bitmaps(r);
    r->src_width  = w;
    r->src_height = h;
    r->src_format = fmt;
    r->front_idx  = 0;
    r->src_bm_is_yuv   = FALSE;
    r->src_bm_is_16bit = FALSE;

    struct BitMap *friend_bm = (app->win && app->win->RPort) ? app->win->RPort->BitMap : NULL;
    uint32 screen_depth = friend_bm ? IGraphics->GetBitMapAttr(friend_bm, BMA_DEPTH) : 32;

    /* 1. Planar YUV420p (PIXF_YUV420P, 12 bpp) via GPU CompositeTags */
    if (fmt == FMT_YUV420P && !r->yuv_hw_disabled) {
        r->src_bm[0] = IGraphics->AllocBitMapTags(
            w, h, 32,
            BMATags_PixelFormat, PIXF_YUV420P,
            BMATags_Displayable, TRUE,
            TAG_DONE);
        if (r->src_bm[0]) {
            r->src_bm_is_yuv   = TRUE;
            r->src_bm_is_16bit = FALSE;
            return TRUE;
        }
        DBG_LOG("[Renderer] RTG driver does not support PIXF_YUV420P, falling back to RGB\n");
    }

    /* 2. 16-bit PIXF_R5G6B5 BitMap fallback */
    if (screen_depth <= 16 || fmt == FMT_RGB565) {
        r->src_bm[0] = IGraphics->AllocBitMapTags(
            w, h, 16,
            BMATags_PixelFormat, PIXF_R5G6B5,
            BMATags_Displayable, TRUE,
            BMATags_Friend,      friend_bm,
            TAG_DONE);
        if (!r->src_bm[0]) {
            r->src_bm[0] = IGraphics->AllocBitMapTags(
                w, h, 16,
                BMATags_PixelFormat, PIXF_R5G6B5,
                BMATags_Displayable, TRUE,
                TAG_DONE);
        }
        r->src_bm_is_16bit = (r->src_bm[0] != NULL);
    }

    /* 3. 32-bit PIXF_A8R8G8B8 BitMap fallback */
    if (!r->src_bm[0]) {
        r->src_bm[0] = IGraphics->AllocBitMapTags(
            w, h, 32,
            BMATags_PixelFormat, PIXF_A8R8G8B8,
            BMATags_Displayable, TRUE,
            BMATags_Friend,      friend_bm,
            TAG_DONE);
        if (!r->src_bm[0]) {
            r->src_bm[0] = IGraphics->AllocBitMapTags(
                w, h, 32,
                BMATags_PixelFormat, PIXF_A8R8G8B8,
                BMATags_Displayable, TRUE,
                TAG_DONE);
        }
        r->src_bm_is_16bit = FALSE;
    }

    if (!r->src_bm[0]) {
        return FALSE;
    }

    /* Initialize BitMap with clean background (RGB only - never run RectFill on YUV) */
    struct RastPort tmp_rp;
    IGraphics->InitRastPort(&tmp_rp);
    tmp_rp.BitMap = r->src_bm[0];
    IGraphics->SetAPen(&tmp_rp, 0);
    IGraphics->RectFill(&tmp_rp, 0, 0, w - 1, h - 1);

    return TRUE;
}

static BOOL ensure_comp_bitmap(VortexApp *app, uint16_t dw, uint16_t dh)
{
    RendererState *r = &app->renderer;
    dw &= ~1;
    dh &= ~1;
    if (dw < 16 || dh < 16) {
        return FALSE;
    }

    if (r->comp_bm && r->comp_width == dw && r->comp_height == dh) {
        return TRUE;
    }

    free_comp_bitmap(r);
    struct BitMap *friend_bm = (app->win && app->win->RPort) ? app->win->RPort->BitMap : NULL;
    uint32 depth = friend_bm ? IGraphics->GetBitMapAttr(friend_bm, BMA_DEPTH) : 32;
    if (depth < 16) depth = 16;
    uint32 pix_fmt = (depth <= 16) ? PIXF_R5G6B5 : PIXF_A8R8G8B8;

    r->comp_bm = IGraphics->AllocBitMapTags(
        dw, dh, depth,
        BMATags_Displayable, TRUE,
        BMATags_Friend,      friend_bm,
        TAG_DONE);
    if (!r->comp_bm) {
        r->comp_bm = IGraphics->AllocBitMapTags(
            dw, dh, depth,
            BMATags_PixelFormat, pix_fmt,
            BMATags_Displayable, TRUE,
            TAG_DONE);
    }
    if (!r->comp_bm) {
        r->comp_bm = IGraphics->AllocBitMapTags(
            dw, dh, 16,
            BMATags_PixelFormat, PIXF_R5G6B5,
            BMATags_Displayable, TRUE,
            TAG_DONE);
    }
    if (!r->comp_bm) {
        return FALSE;
    }
    r->comp_width  = dw;
    r->comp_height = dh;
    return TRUE;
}

/* Convert a row of Big-Endian RGB565 pixels to 32-bit ARGB32 using auto-vectorized AltiVec SIMD */
static inline void convert_rgb565_row_to_argb32(
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

static void upload_slice_to_bitmap(
    RendererState *r,
    struct BitMap *bm,
    const uint8_t *pixels,
    uint16_t       w,
    uint16_t       y_offset,
    uint16_t       slice_h,
    uint8_t        fmt
)
{
    BOOL is_16bit = r->src_bm_is_16bit;
    uint32_t bpp  = is_16bit ? 2U : 4U;
    uint32_t dst_row_bytes = (uint32_t)w * bpp;

    uint8_t *base_addr = NULL;
    uint32   dst_bpr   = 0;
    APTR lock = IGraphics->LockBitMapTags(
        bm,
        LBM_BaseAddress, &base_addr,
        LBM_BytesPerRow, &dst_bpr,
        TAG_DONE);

    if (lock && base_addr && dst_bpr >= dst_row_bytes) {
        uint8_t *dst_row = base_addr + (size_t)y_offset * (size_t)dst_bpr;
        if (is_16bit) {
            /* 16-Bit VRAM Destination */
            if (fmt == FMT_RGB565) {
                /* Fast-Path: 16-bit RGB565 -> 16-bit RGB565 BitMap: ZERO-CONVERSION ALTIVEC DIRECT MEMCPY! */
                const uint8_t *src_row = pixels;
                if (dst_bpr == dst_row_bytes) {
                    altivec_memcpy_fast(dst_row, src_row, (size_t)dst_row_bytes * slice_h);
                } else {
                    for (uint16_t y = 0; y < slice_h; y++) {
                        altivec_memcpy_fast(dst_row, src_row, dst_row_bytes);
                        src_row += dst_row_bytes;
                        dst_row += dst_bpr;
                    }
                }
            } else {
                /* 32-bit ARGB32 -> 16-bit RGB565 */
                const uint32_t * restrict src32 = (const uint32_t *)pixels;
                for (uint16_t y = 0; y < slice_h; y++) {
                    uint16_t * restrict dst16 = (uint16_t *)dst_row;
                    for (uint16_t x = 0; x < w; x++) {
                        uint32_t c = src32[x];
                        uint16_t r5 = (uint16_t)((c >> 19) & 0x1FU);
                        uint16_t g6 = (uint16_t)((c >> 10) & 0x3FU);
                        uint16_t b5 = (uint16_t)((c >> 3)  & 0x1FU);
                        dst16[x] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
                    }
                    src32 += w;
                    dst_row += dst_bpr;
                }
            }
        } else {
            /* 32-Bit VRAM Destination (Fallback) */
            if (fmt == FMT_RGB565) {
                const uint16_t * restrict src16 = (const uint16_t *)pixels;
                for (uint16_t y = 0; y < slice_h; y++) {
                    convert_rgb565_row_to_argb32(src16, (uint32_t *)dst_row, w);
                    src16   += w;
                    dst_row += dst_bpr;
                }
            } else {
                const uint8_t *src_row = pixels;
                if (dst_bpr == dst_row_bytes) {
                    altivec_memcpy_fast(dst_row, src_row, (size_t)dst_row_bytes * slice_h);
                } else {
                    for (uint16_t y = 0; y < slice_h; y++) {
                        altivec_memcpy_fast(dst_row, src_row, dst_row_bytes);
                        src_row += dst_row_bytes;
                        dst_row += dst_bpr;
                    }
                }
            }
        }
        IGraphics->UnlockBitMap(lock);
        return;
    }
    if (lock) {
        IGraphics->UnlockBitMap(lock);
    }

    /* Fallback via BltBitMapTags if direct VRAM lock is unavailable */
    if (fmt == FMT_RGB565) {
        uint32_t argb_row_bytes = (uint32_t)w * 4U;
        uint32_t needed = argb_row_bytes * (uint32_t)slice_h;
        if (r->conv_argb_cap < needed) {
            uint32_t *nb = (uint32_t *)realloc(r->conv_argb_buf, needed);
            if (!nb) return;
            r->conv_argb_buf = nb;
            r->conv_argb_cap = needed;
        }
        const uint16_t *src16 = (const uint16_t *)pixels;
        uint32_t       *dst32 = r->conv_argb_buf;
        for (uint16_t y = 0; y < slice_h; y++) {
            convert_rgb565_row_to_argb32(src16, dst32, w);
            src16 += w;
            dst32 += w;
        }
        IGraphics->BltBitMapTags(
            BLITA_Source,         (APTR)r->conv_argb_buf,
            BLITA_SrcType,        BLITT_ARGB32,
            BLITA_SrcBytesPerRow, argb_row_bytes,
            BLITA_Dest,           bm,
            BLITA_DestType,       BLITT_BITMAP,
            BLITA_DestX,          0,
            BLITA_DestY,          y_offset,
            BLITA_Width,          w,
            BLITA_Height,         slice_h,
            TAG_DONE);
    } else {
        IGraphics->BltBitMapTags(
            BLITA_Source,         (APTR)pixels,
            BLITA_SrcType,        BLITT_ARGB32,
            BLITA_SrcBytesPerRow, dst_row_bytes,
            BLITA_Dest,           bm,
            BLITA_DestType,       BLITT_BITMAP,
            BLITA_DestX,          0,
            BLITA_DestY,          y_offset,
            BLITA_Width,          w,
            BLITA_Height,         slice_h,
            TAG_DONE);
    }
}

static void upload_rect_to_bitmap(
    RendererState *r,
    struct BitMap *bm,
    const uint8_t *pixels,
    uint16_t       full_w,
    uint16_t       rx,
    uint16_t       ry,
    uint16_t       rw,
    uint16_t       rh,
    uint8_t        fmt
)
{
    BOOL is_16bit = r->src_bm_is_16bit;
    uint32_t dst_bpp = is_16bit ? 2U : 4U;
    uint32_t src_bpp = (fmt == FMT_RGB565) ? 2U : 4U;
    uint32_t src_row_bytes = (uint32_t)rw * src_bpp;
    uint32_t dst_row_bytes = (uint32_t)rw * dst_bpp;

    uint8_t *base_addr = NULL;
    uint32   dst_bpr   = 0;
    APTR lock = IGraphics->LockBitMapTags(
        bm,
        LBM_BaseAddress, &base_addr,
        LBM_BytesPerRow, &dst_bpr,
        TAG_DONE);

    if (lock && base_addr) {
        uint8_t *dst_start = base_addr + (size_t)ry * (size_t)dst_bpr + (size_t)rx * dst_bpp;
        const uint8_t *src_row = pixels;

        if (is_16bit && fmt == FMT_RGB565) {
            /* 16-bit to 16-bit: AltiVec fast memcpy */
            for (uint16_t y = 0; y < rh; y++) {
                altivec_memcpy_fast(dst_start + (size_t)y * dst_bpr, src_row, dst_row_bytes);
                src_row += src_row_bytes;
            }
        } else if (!is_16bit && fmt == FMT_ARGB32) {
            /* 32-bit to 32-bit: AltiVec fast memcpy */
            for (uint16_t y = 0; y < rh; y++) {
                altivec_memcpy_fast(dst_start + (size_t)y * dst_bpr, src_row, dst_row_bytes);
                src_row += src_row_bytes;
            }
        } else if (!is_16bit && fmt == FMT_RGB565) {
            /* 16-bit to 32-bit: Convert per row via AltiVec SIMD */
            for (uint16_t y = 0; y < rh; y++) {
                const uint16_t * restrict src16 = (const uint16_t *)src_row;
                uint32_t * restrict dst32 = (uint32_t *)(dst_start + (size_t)y * dst_bpr);
                convert_rgb565_row_to_argb32(src16, dst32, rw);
                src_row += src_row_bytes;
            }
        } else if (is_16bit && fmt == FMT_ARGB32) {
            /* 32-bit to 16-bit: Convert per row via AltiVec SIMD */
            for (uint16_t y = 0; y < rh; y++) {
                const uint32_t * restrict src32 = (const uint32_t *)src_row;
                uint16_t * restrict dst16 = (uint16_t *)(dst_start + (size_t)y * dst_bpr);
                for (uint16_t x = 0; x < rw; x++) {
                    uint32_t c = src32[x];
                    uint16_t r5 = (uint16_t)((c >> 19) & 0x1FU);
                    uint16_t g6 = (uint16_t)((c >> 10) & 0x3FU);
                    uint16_t b5 = (uint16_t)((c >> 3)  & 0x1FU);
                    dst16[x] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
                }
                src_row += src_row_bytes;
            }
        }
        IGraphics->UnlockBitMap(lock);
        return;
    }
    if (lock) {
        IGraphics->UnlockBitMap(lock);
    }
}

static void upload_yuv420p_to_bitmap(
    RendererState *r,
    struct BitMap *bm,
    const uint8_t *yuv_data,
    uint16_t       w,
    uint16_t       h
)
{
    (void)r;
    struct PlanarYUVInfo yuvInfo;
    memset(&yuvInfo, 0, sizeof(yuvInfo));

    APTR lock = IGraphics->LockBitMapTags(
        bm,
        LBM_PlanarYUVInfo, &yuvInfo,
        TAG_DONE);

    if (lock && yuvInfo.YMemory && yuvInfo.UMemory && yuvInfo.VMemory) {
        const uint8_t *y_src = yuv_data;
        const uint8_t *u_src = y_src + (size_t)w * (size_t)h;
        const uint8_t *v_src = u_src + ((size_t)w / 2) * ((size_t)h / 2);

        /* 1. Y plane: w x h (Luma, 8 bpp) */
        uint8_t *y_dst = (uint8_t *)yuvInfo.YMemory;
        if (yuvInfo.YBytesPerRow == w) {
            memcpy(y_dst, y_src, (size_t)w * h);
        } else {
            for (uint16_t y = 0; y < h; y++) {
                memcpy(y_dst, y_src, w);
                y_src += w;
                y_dst += yuvInfo.YBytesPerRow;
            }
        }

        /* 2. U plane: (w/2) x (h/2) (Chroma Blue) */
        uint16_t half_w = w / 2;
        uint16_t half_h = h / 2;
        uint8_t *u_dst = (uint8_t *)yuvInfo.UMemory;
        if (yuvInfo.UBytesPerRow == half_w) {
            memcpy(u_dst, u_src, (size_t)half_w * half_h);
        } else {
            for (uint16_t y = 0; y < half_h; y++) {
                memcpy(u_dst, u_src, half_w);
                u_src += half_w;
                u_dst += yuvInfo.UBytesPerRow;
            }
        }

        /* 3. V plane: (w/2) x (h/2) (Chroma Red) */
        uint8_t *v_dst = (uint8_t *)yuvInfo.VMemory;
        if (yuvInfo.VBytesPerRow == half_w) {
            memcpy(v_dst, v_src, (size_t)half_w * half_h);
        } else {
            for (uint16_t y = 0; y < half_h; y++) {
                memcpy(v_dst, v_src, half_w);
                v_src += half_w;
                v_dst += yuvInfo.VBytesPerRow;
            }
        }

        IGraphics->UnlockBitMap(lock);
        return;
    }
    if (lock) {
        IGraphics->UnlockBitMap(lock);
    }
}

static inline uint8_t clamp_u8_int(int v)
{
    return (v < 0) ? 0U : ((v > 255) ? 255U : (uint8_t)v);
}

/* Software fallback: convert planar YUV420P (BT.601) into RGB565 or ARGB32 BitMap */
static void upload_yuv420p_to_rgb_bitmap(
    RendererState *r,
    struct BitMap *bm,
    const uint8_t *yuv_data,
    uint16_t       src_w,
    uint16_t       src_h,
    uint16_t       dst_w,
    uint16_t       dst_h
)
{
    (void)r;
    if (!bm || !yuv_data || src_w < 2 || src_h < 2 || dst_w < 2 || dst_h < 2) {
        return;
    }

    uint16_t conv_w = (dst_w < src_w) ? dst_w : src_w;
    uint16_t conv_h = (dst_h < src_h) ? dst_h : src_h;

    const uint8_t *y_plane = yuv_data;
    const uint8_t *u_plane = y_plane + (size_t)src_w * (size_t)src_h;
    const uint8_t *v_plane = u_plane + ((size_t)src_w / 2) * ((size_t)src_h / 2);
    uint16_t half_src_w = src_w / 2;

    uint8_t *base_addr = NULL;
    uint32   dst_bpr   = 0;
    APTR lock = IGraphics->LockBitMapTags(
        bm,
        LBM_BaseAddress, &base_addr,
        LBM_BytesPerRow, &dst_bpr,
        TAG_DONE);

    if (lock && base_addr) {
        uint32 depth = IGraphics->GetBitMapAttr(bm, BMA_DEPTH);
        BOOL is_16 = (depth <= 16);

        if (is_16) {
            for (uint16_t y = 0; y < conv_h; y++) {
                const uint8_t *y_row = y_plane + (size_t)y * src_w;
                const uint8_t *u_row = u_plane + (size_t)(y >> 1) * half_src_w;
                const uint8_t *v_row = v_plane + (size_t)(y >> 1) * half_src_w;
                uint16_t *dst16 = (uint16_t *)(base_addr + (size_t)y * dst_bpr);
                for (uint16_t x = 0; x < conv_w; x++) {
                    int y_val = (int)y_row[x];
                    int u_val = (int)u_row[x >> 1] - 128;
                    int v_val = (int)v_row[x >> 1] - 128;
                    int c = y_val - 16;
                    if (c < 0) c = 0;
                    int r8 = (298 * c + 409 * v_val + 128) >> 8;
                    int g8 = (298 * c - 100 * u_val - 208 * v_val + 128) >> 8;
                    int b8 = (298 * c + 516 * u_val + 128) >> 8;
                    uint16_t r5 = (uint16_t)(clamp_u8_int(r8) >> 3);
                    uint16_t g6 = (uint16_t)(clamp_u8_int(g8) >> 2);
                    uint16_t b5 = (uint16_t)(clamp_u8_int(b8) >> 3);
                    dst16[x] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
                }
            }
        } else {
            for (uint16_t y = 0; y < conv_h; y++) {
                const uint8_t *y_row = y_plane + (size_t)y * src_w;
                const uint8_t *u_row = u_plane + (size_t)(y >> 1) * half_src_w;
                const uint8_t *v_row = v_plane + (size_t)(y >> 1) * half_src_w;
                uint32_t *dst32 = (uint32_t *)(base_addr + (size_t)y * dst_bpr);
                for (uint16_t x = 0; x < conv_w; x++) {
                    int y_val = (int)y_row[x];
                    int u_val = (int)u_row[x >> 1] - 128;
                    int v_val = (int)v_row[x >> 1] - 128;
                    int c = y_val - 16;
                    if (c < 0) c = 0;
                    uint32_t r8 = clamp_u8_int((298 * c + 409 * v_val + 128) >> 8);
                    uint32_t g8 = clamp_u8_int((298 * c - 100 * u_val - 208 * v_val + 128) >> 8);
                    uint32_t b8 = clamp_u8_int((298 * c + 516 * u_val + 128) >> 8);
                    dst32[x] = 0xFF000000U | (r8 << 16) | (g8 << 8) | b8;
                }
            }
        }
        IGraphics->UnlockBitMap(lock);
        return;
    }
    if (lock) {
        IGraphics->UnlockBitMap(lock);
    }

    /* Fallback: Convert to ARGB32 in RAM buffer and upload via BltBitMapTags if direct VRAM lock failed */
    uint32_t argb_row_bytes = (uint32_t)conv_w * 4U;
    uint32_t needed = argb_row_bytes * (uint32_t)conv_h;
    if (r->conv_argb_cap < needed) {
        uint32_t *nb = (uint32_t *)realloc(r->conv_argb_buf, needed);
        if (!nb) return;
        r->conv_argb_buf = nb;
        r->conv_argb_cap = needed;
    }

    for (uint16_t y = 0; y < conv_h; y++) {
        const uint8_t *y_row = y_plane + (size_t)y * src_w;
        const uint8_t *u_row = u_plane + (size_t)(y >> 1) * half_src_w;
        const uint8_t *v_row = v_plane + (size_t)(y >> 1) * half_src_w;
        uint32_t *dst32 = r->conv_argb_buf + (size_t)y * conv_w;
        for (uint16_t x = 0; x < conv_w; x++) {
            int y_val = (int)y_row[x];
            int u_val = (int)u_row[x >> 1] - 128;
            int v_val = (int)v_row[x >> 1] - 128;
            int c = y_val - 16;
            if (c < 0) c = 0;
            uint32_t r8 = clamp_u8_int((298 * c + 409 * v_val + 128) >> 8);
            uint32_t g8 = clamp_u8_int((298 * c - 100 * u_val - 208 * v_val + 128) >> 8);
            uint32_t b8 = clamp_u8_int((298 * c + 516 * u_val + 128) >> 8);
            dst32[x] = 0xFF000000U | (r8 << 16) | (g8 << 8) | b8;
        }
    }

    IGraphics->BltBitMapTags(
        BLITA_Source,         (APTR)r->conv_argb_buf,
        BLITA_SrcType,        BLITT_ARGB32,
        BLITA_SrcBytesPerRow, argb_row_bytes,
        BLITA_Dest,           bm,
        BLITA_DestType,       BLITT_BITMAP,
        BLITA_DestX,          0,
        BLITA_DestY,          0,
        BLITA_Width,          conv_w,
        BLITA_Height,         conv_h,
        TAG_DONE);
}

struct vb_jpeg_error_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

static void vb_jpeg_error_exit(j_common_ptr cinfo)
{
    struct vb_jpeg_error_mgr *myerr = (struct vb_jpeg_error_mgr *)cinfo->err;
    longjmp(myerr->setjmp_buffer, 1);
}

static BOOL renderer_decode_jpeg(
    VortexApp     *app,
    const uint8_t *jpeg_data,
    uint32_t       jpeg_len,
    uint16_t       w,
    uint16_t       h
)
{
    if (!app || !jpeg_data || jpeg_len == 0) {
        return FALSE;
    }

    struct jpeg_decompress_struct cinfo;
    struct vb_jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = vb_jpeg_error_exit;

    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        return FALSE;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, (unsigned char *)jpeg_data, (unsigned long)jpeg_len);

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return FALSE;
    }

    cinfo.out_color_space     = JCS_RGB;
    cinfo.dct_method          = JDCT_FASTEST;
    cinfo.do_fancy_upsampling = FALSE;
    cinfo.two_pass_quantize   = FALSE;
    cinfo.dither_mode         = JDITHER_NONE;

    jpeg_start_decompress(&cinfo);

    uint16 out_w = (uint16)cinfo.output_width;
    uint16 out_h = (uint16)cinfo.output_height;

    if (!ensure_source_bitmaps(app, out_w, out_h, FMT_ARGB32)) {
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        return FALSE;
    }

    RendererState *r = &app->renderer;
    r->src_format = FMT_ARGB32;

    uint8_t *base_addr = NULL;
    uint32   dst_bpr   = 0;
    APTR lock = IGraphics->LockBitMapTags(
        r->src_bm[0],
        LBM_BaseAddress, &base_addr,
        LBM_BytesPerRow, &dst_bpr,
        TAG_DONE);

    JSAMPROW row_buf = NULL;
    uint32 row_stride = (uint32)out_w * 3U;

    if (r->conv_argb_cap < row_stride) {
        uint32_t *nb = (uint32_t *)realloc(r->conv_argb_buf, row_stride + 16);
        if (nb) {
            r->conv_argb_buf = nb;
            r->conv_argb_cap = row_stride + 16;
        }
    }
    row_buf = (JSAMPROW)r->conv_argb_buf;

    if (lock && base_addr && row_buf) {
        while (cinfo.output_scanline < cinfo.output_height) {
            uint32 y = cinfo.output_scanline;
            jpeg_read_scanlines(&cinfo, &row_buf, 1);
            const uint8_t *s = (const uint8_t *)row_buf;
            if (r->src_bm_is_16bit) {
                uint16_t *dst16 = (uint16_t *)(base_addr + y * dst_bpr);
                uint32 x = 0;
                for (; x + 3 < out_w; x += 4) {
                    dst16[x + 0] = ((uint16_t)(s[0] >> 3) << 11) | ((uint16_t)(s[1] >> 2) << 5) | (uint16_t)(s[2] >> 3);
                    dst16[x + 1] = ((uint16_t)(s[3] >> 3) << 11) | ((uint16_t)(s[4] >> 2) << 5) | (uint16_t)(s[5] >> 3);
                    dst16[x + 2] = ((uint16_t)(s[6] >> 3) << 11) | ((uint16_t)(s[7] >> 2) << 5) | (uint16_t)(s[8] >> 3);
                    dst16[x + 3] = ((uint16_t)(s[9] >> 3) << 11) | ((uint16_t)(s[10] >> 2) << 5) | (uint16_t)(s[11] >> 3);
                    s += 12;
                }
                for (; x < out_w; x++) {
                    dst16[x] = ((uint16_t)(s[0] >> 3) << 11) | ((uint16_t)(s[1] >> 2) << 5) | (uint16_t)(s[2] >> 3);
                    s += 3;
                }
            } else {
                uint32_t *dst32 = (uint32_t *)(base_addr + y * dst_bpr);
                uint32 x = 0;
                for (; x + 3 < out_w; x += 4) {
                    dst32[x + 0] = (0xFF000000U) | ((uint32_t)s[0]  << 16) | ((uint32_t)s[1]  << 8) | (uint32_t)s[2];
                    dst32[x + 1] = (0xFF000000U) | ((uint32_t)s[3]  << 16) | ((uint32_t)s[4]  << 8) | (uint32_t)s[5];
                    dst32[x + 2] = (0xFF000000U) | ((uint32_t)s[6]  << 16) | ((uint32_t)s[7]  << 8) | (uint32_t)s[8];
                    dst32[x + 3] = (0xFF000000U) | ((uint32_t)s[9]  << 16) | ((uint32_t)s[10] << 8) | (uint32_t)s[11];
                    s += 12;
                }
                for (; x < out_w; x++) {
                    dst32[x] = (0xFF000000U) | ((uint32_t)s[0] << 16) | ((uint32_t)s[1] << 8) | (uint32_t)s[2];
                    s += 3;
                }
            }
        }
        IGraphics->UnlockBitMap(lock);
    } else {
        if (lock) IGraphics->UnlockBitMap(lock);
        while (cinfo.output_scanline < cinfo.output_height) {
            jpeg_read_scanlines(&cinfo, &row_buf, 1);
        }
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    r->has_frame = TRUE;
    r->frames_decoded++;

    r->dirty    = TRUE;
    r->dirty_y1 = 0;
    r->dirty_y2 = out_h;

    return TRUE;
}

BOOL renderer_decode_and_present(
    VortexApp      *app,
    const MsgFrame *hdr,
    const uint8_t  *lz4_data,
    uint32_t        lz4_len
)
{
    RendererState *r = &app->renderer;
    uint16_t w     = VB_BE16(hdr->width);
    uint16_t h     = VB_BE16(hdr->height);
    uint8_t  fmt   = hdr->format;
    uint8_t  codec = hdr->codec;
    uint32_t bpp   = (fmt == FMT_RGB565) ? 2U : (fmt == FMT_YUV420P) ? 1U : 4U;

    if (codec == CODEC_JPEG) {
        return renderer_decode_jpeg(app, lz4_data, lz4_len, w, h);
    }

    /* CODEC_LZ4_RECT (4): 2D Dirty Bounding Box [MsgRectHdr: x, y, w, h] + LZ4 */
    if (codec == CODEC_LZ4_RECT) {
        if (lz4_len < sizeof(MsgRectHdr)) {
            return FALSE;
        }
        const MsgRectHdr *rh_hdr = (const MsgRectHdr *)lz4_data;
        uint16_t rx = VB_BE16(rh_hdr->x);
        uint16_t ry = VB_BE16(rh_hdr->y);
        uint16_t rw = VB_BE16(rh_hdr->w);
        uint16_t rh = VB_BE16(rh_hdr->h);
        const uint8_t *comp_ptr = lz4_data + sizeof(MsgRectHdr);
        uint32_t       comp_len = lz4_len - sizeof(MsgRectHdr);

        if (rw == 0 || rh == 0 || (uint32_t)rx + (uint32_t)rw > (uint32_t)w || (uint32_t)ry + (uint32_t)rh > (uint32_t)h) {
            return FALSE;
        }

        uint32_t expected_raw_len = (uint32_t)rw * (uint32_t)rh * bpp;
        if (expected_raw_len == 0 || expected_raw_len > (32 * 1024 * 1024)) {
            return FALSE;
        }

        if (r->raw_frame_cap < expected_raw_len) {
            uint32_t alloc_cap = expected_raw_len;
            uint32_t min_fullhd = 1920U * 1080U * 2U;
            if (alloc_cap < min_fullhd) alloc_cap = min_fullhd;
            uint8_t *new_buf = (uint8_t *)realloc(r->raw_frame_buf, alloc_cap);
            if (!new_buf) return FALSE;
            r->raw_frame_buf = new_buf;
            r->raw_frame_cap = alloc_cap;
        }

        int dec_bytes = LZ4_decompress_safe(
            (const char *)comp_ptr,
            (char *)r->raw_frame_buf,
            (int)comp_len,
            (int)expected_raw_len
        );
        if (dec_bytes < 0 || (uint32_t)dec_bytes != expected_raw_len) {
            return FALSE;
        }

        if (!ensure_source_bitmaps(app, w, h, fmt)) {
            return FALSE;
        }

        r->src_format = fmt;
        upload_rect_to_bitmap(r, r->src_bm[0], r->raw_frame_buf, w, rx, ry, rw, rh, fmt);
        r->has_frame = TRUE;
        r->frames_decoded++;

        /* Accumulate dirty 2D bounding box */
        uint16_t x_end = (uint16_t)(rx + rw);
        uint16_t y_end = (uint16_t)(ry + rh);
        if (!r->dirty) {
            r->dirty    = TRUE;
            r->dirty_x1 = rx;
            r->dirty_y1 = ry;
            r->dirty_x2 = x_end;
            r->dirty_y2 = y_end;
        } else {
            if (rx    < r->dirty_x1) r->dirty_x1 = rx;
            if (ry    < r->dirty_y1) r->dirty_y1 = ry;
            if (x_end > r->dirty_x2) r->dirty_x2 = x_end;
            if (y_end > r->dirty_y2) r->dirty_y2 = y_end;
        }

        return TRUE;
    }

    uint16_t y_offset = 0;
    uint16_t slice_h  = h;
    const uint8_t *comp_ptr = lz4_data;
    uint32_t       comp_len = lz4_len;

    if (codec == CODEC_LZ4_SLICE) {
        if (lz4_len <= 4) {
            return FALSE;
        }
        y_offset = (uint16_t)(((uint16_t)lz4_data[0] << 8) | (uint16_t)lz4_data[1]);
        slice_h  = (uint16_t)(((uint16_t)lz4_data[2] << 8) | (uint16_t)lz4_data[3]);
        comp_ptr = lz4_data + 4;
        comp_len = lz4_len - 4;
        if (slice_h == 0 || (uint32_t)y_offset + (uint32_t)slice_h > (uint32_t)h) {
            return FALSE;
        }
    }

    uint32_t expected_raw_len;
    if (fmt == FMT_YUV420P) {
        expected_raw_len = (uint32_t)w * (uint32_t)h * 3U / 2U;
    } else {
        expected_raw_len = (uint32_t)w * (uint32_t)slice_h * bpp;
    }

    if (w == 0 || h == 0 || expected_raw_len == 0 || expected_raw_len > (32 * 1024 * 1024)) {
        return FALSE;
    }

    uint32_t needed_cap = expected_raw_len + 65536U;
    if (r->raw_frame_cap < needed_cap) {
        uint32_t alloc_cap = needed_cap;
        uint32_t min_fullhd = 1920U * 1080U * 2U;
        if (alloc_cap < min_fullhd) alloc_cap = min_fullhd;
        uint8_t *new_buf = (uint8_t *)realloc(r->raw_frame_buf, alloc_cap);
        if (!new_buf) {
            return FALSE;
        }
        r->raw_frame_buf = new_buf;
        r->raw_frame_cap = alloc_cap;
    }

    int dec_bytes = LZ4_decompress_safe(
        (const char *)comp_ptr,
        (char *)r->raw_frame_buf,
        (int)comp_len,
        (int)r->raw_frame_cap
    );
    if (dec_bytes < 0 || (uint32_t)dec_bytes < expected_raw_len) {
        DBG_LOG("[Renderer] LZ4_decompress_safe error: got %d, expected >= %u\n",
               dec_bytes, (unsigned)expected_raw_len);
        return FALSE;
    }

    if (!ensure_source_bitmaps(app, w, h, fmt)) {
        return FALSE;
    }

    r->src_format = fmt;
    if (fmt == FMT_YUV420P) {
        if (r->src_bm_is_yuv) {
            upload_yuv420p_to_bitmap(r, r->src_bm[0], r->raw_frame_buf, w, h);
        } else {
            upload_yuv420p_to_rgb_bitmap(r, r->src_bm[0], r->raw_frame_buf, w, h, w, h);
        }
    } else {
        upload_slice_to_bitmap(r, r->src_bm[0], r->raw_frame_buf, w, y_offset, slice_h, r->src_format);
    }
    r->has_frame = TRUE;
    r->frames_decoded++;

    /* Accumulate dirty scanline interval for coalesced screen presentation */
    uint16_t y_start = (fmt == FMT_YUV420P) ? 0 : y_offset;
    uint16_t y_end   = (fmt == FMT_YUV420P) ? h : (uint16_t)(y_offset + slice_h);
    if (!r->dirty) {
        r->dirty    = TRUE;
        r->dirty_x1 = 0;
        r->dirty_y1 = y_start;
        r->dirty_x2 = w;
        r->dirty_y2 = y_end;
    } else {
        r->dirty_x1 = 0;
        r->dirty_x2 = w;
        if (y_start < r->dirty_y1) r->dirty_y1 = y_start;
        if (y_end   > r->dirty_y2) r->dirty_y2 = y_end;
    }

    return TRUE;
}

void renderer_flush_dirty(VortexApp *app)
{
    RendererState *r = &app->renderer;
    if (!r->dirty || !r->has_frame || !r->src_bm[0]) {
        return;
    }
    if (!app->win || !app->win->RPort || !app->gad_space) {
        return;
    }

    struct IBox *area = NULL;
    struct IBox fallback_area;
    IIntuition->GetAttr(SPACE_AreaBox, app->gad_space, (uint32 *)&area);
    if (!area || area->Width <= 2 || area->Height <= 2) {
        fallback_area.Left   = app->win->BorderLeft;
        fallback_area.Top    = app->win->BorderTop + 56;
        fallback_area.Width  = app->win->Width - app->win->BorderLeft - app->win->BorderRight;
        fallback_area.Height = app->win->Height - app->win->BorderTop - app->win->BorderBottom - 60;
        if (app->prefs.show_status_bar) fallback_area.Height -= 24;
        if (fallback_area.Width <= 2 || fallback_area.Height <= 2) {
            return;
        }
        area = &fallback_area;
    }

    uint16_t dw = (uint16_t)area->Width;
    uint16_t dh = (uint16_t)area->Height;
    uint16_t copy_w = (dw < r->src_width)  ? dw : r->src_width;
    uint16_t copy_h = (dh < r->src_height) ? dh : r->src_height;
    int diff_w = (int)dw - (int)r->src_width;
    int diff_h = (int)dh - (int)r->src_height;
    BOOL is_1to1 = (diff_w >= -2 && diff_w <= 2 && diff_h >= -2 && diff_h <= 2);

    /* For YUV420P (always full frames), or on initial startup frames (frames_decoded <= 2),
     * or when scaling is involved, always do a full present so the web page is completely displayed on screen */
    if (r->src_format == FMT_YUV420P || r->frames_decoded <= 2 || !is_1to1 || r->src_bm_is_yuv) {
        renderer_present(app, app->win->RPort, area);
        r->dirty = FALSE;
        return;
    }

    /* 1:1 Partial Dirty 2D-Box Blit Fast Path (RGB bitmaps).
     * Blit through the window's LAYERED RastPort: Intuition clips against
     * window borders, tabs/toolbars and overlapping windows automatically.
     * Never composite directly into the screen bitmap (bypasses clipping and
     * races with the AmigaOS compositor -> overlapping tabs, flicker). */
    uint16_t x1 = r->dirty_x1;
    uint16_t y1 = r->dirty_y1;
    uint16_t x2 = (r->dirty_x2 <= copy_w) ? r->dirty_x2 : copy_w;
    uint16_t y2 = (r->dirty_y2 <= copy_h) ? r->dirty_y2 : copy_h;
    if (x2 > x1 && y2 > y1) {
        IGraphics->BltBitMapRastPort(
            r->src_bm[0], x1, y1,
            app->win->RPort, area->Left + x1, area->Top + y1,
            (uint16_t)(x2 - x1), (uint16_t)(y2 - y1), 0xC0);
    }
    r->dirty = FALSE;
}

void renderer_present(VortexApp *app, struct RastPort *rp, struct IBox *area)
{
    RendererState *r = &app->renderer;
    if (!rp || !area || area->Width <= 2 || area->Height <= 2) {
        return;
    }

    uint16_t dw = (uint16_t)(area->Width & ~1);
    uint16_t dh = (uint16_t)(area->Height & ~1);

    if (!r->has_frame || !r->src_bm[0] || dw < 16 || dh < 16 || r->src_width < 16 || r->src_height < 16) {
        return;
    }

    struct BitMap *front_bm = r->src_bm[0];
    uint16_t copy_w = (dw < r->src_width)  ? dw : r->src_width;
    uint16_t copy_h = (dh < r->src_height) ? dh : r->src_height;
    int diff_w = (int)dw - (int)r->src_width;
    int diff_h = (int)dh - (int)r->src_height;
    BOOL is_1to1 = (diff_w >= -2 && diff_w <= 2 && diff_h >= -2 && diff_h <= 2);

    /* 1:1 RGB BitMap fast-path: direct hardware blit into the window layer */
    if (is_1to1 && !r->src_bm_is_yuv) {
        IGraphics->BltBitMapRastPort(
            front_bm, 0, 0,
            rp, area->Left, area->Top,
            copy_w, copy_h, 0xC0);
        return;
    }

    /* Scaled or YUV BitMap: GPU-composite offscreen into comp_bm (VRAM->VRAM),
     * then blit the result through the layered RastPort.
     * This guarantees 100% correct window layer positioning, clipping, and hardware color conversion! */
    if (ensure_comp_bitmap(app, dw, dh)) {
        int32 scale_x = is_1to1 ? COMP_FLOAT_TO_FIX(1.0f) : COMP_FLOAT_TO_FIX((float)dw / (float)r->src_width);
        int32 scale_y = is_1to1 ? COMP_FLOAT_TO_FIX(1.0f) : COMP_FLOAT_TO_FIX((float)dh / (float)r->src_height);
        uint32 flags = COMPFLAG_IgnoreDestAlpha | (is_1to1 ? 0 : COMPFLAG_SrcFilter);
        uint32 yuv_tag = r->src_bm_is_yuv ? COMPTAG_SrcYUVStandard : TAG_IGNORE;
        uint32 yuv_val = r->src_bm_is_yuv ? COMPYUV_BT601 : 0;

        uint32 comp_err = IGraphics->CompositeTags(
            COMPOSITE_Src,
            front_bm,
            r->comp_bm,
            COMPTAG_SrcWidth,       (uint32)r->src_width,
            COMPTAG_SrcHeight,      (uint32)r->src_height,
            COMPTAG_ScaleX,         scale_x,
            COMPTAG_ScaleY,         scale_y,
            COMPTAG_OffsetX,        0,
            COMPTAG_OffsetY,        0,
            COMPTAG_DestX,          0,
            COMPTAG_DestY,          0,
            COMPTAG_DestWidth,      (uint32)dw,
            COMPTAG_DestHeight,     (uint32)dh,
            yuv_tag,                yuv_val,
            COMPTAG_Flags,          flags,
            TAG_DONE);

        r->last_comp_err = comp_err;
        if (comp_err == COMPERR_Success) {
            IGraphics->BltBitMapRastPort(
                r->comp_bm, 0, 0,
                rp, area->Left, area->Top,
                dw, dh, 0xC0);
            return;
        }
    }

    /* Fallback: Direct composite to screen RastPort BitMap if offscreen comp_bm fails */
    if (rp->BitMap) {
        int32 scale_x = is_1to1 ? COMP_FLOAT_TO_FIX(1.0f) : COMP_FLOAT_TO_FIX((float)dw / (float)r->src_width);
        int32 scale_y = is_1to1 ? COMP_FLOAT_TO_FIX(1.0f) : COMP_FLOAT_TO_FIX((float)dh / (float)r->src_height);
        uint32 flags = COMPFLAG_IgnoreDestAlpha | (is_1to1 ? 0 : COMPFLAG_SrcFilter);
        uint32 yuv_tag = r->src_bm_is_yuv ? COMPTAG_SrcYUVStandard : TAG_IGNORE;
        uint32 yuv_val = r->src_bm_is_yuv ? COMPYUV_BT601 : 0;

        uint32 comp_err = IGraphics->CompositeTags(
            COMPOSITE_Src,
            front_bm,
            rp->BitMap,
            COMPTAG_SrcX,           0,
            COMPTAG_SrcY,           0,
            COMPTAG_SrcWidth,       (uint32)r->src_width,
            COMPTAG_SrcHeight,      (uint32)r->src_height,
            COMPTAG_ScaleX,         scale_x,
            COMPTAG_ScaleY,         scale_y,
            COMPTAG_OffsetX,        (int32)area->Left,
            COMPTAG_OffsetY,        (int32)area->Top,
            COMPTAG_DestX,          (uint32)area->Left,
            COMPTAG_DestY,          (uint32)area->Top,
            COMPTAG_DestWidth,      (uint32)dw,
            COMPTAG_DestHeight,     (uint32)dh,
            yuv_tag,                yuv_val,
            COMPTAG_Flags,          flags,
            TAG_DONE);

        if (comp_err == COMPERR_Success) {
            return;
        }
    }

    /* Last resort: unscaled blit (RGB only) */
    if (!r->src_bm_is_yuv) {
        IGraphics->BltBitMapRastPort(
            front_bm, 0, 0,
            rp, area->Left, area->Top,
            copy_w, copy_h, 0xC0);
    }
}

void renderer_sync_viewport_size(VortexApp *app)
{
    if (!app->connected || !app->win || !app->gad_space) {
        return;
    }

    struct IBox *area = NULL;
    IIntuition->GetAttr(SPACE_AreaBox, app->gad_space, (uint32 *)&area);

    uint16_t w = 0;
    uint16_t h = 0;

    if (area && area->Width >= 320 && area->Height >= 200) {
        w = (uint16_t)(area->Width  & ~1);
        h = (uint16_t)(area->Height & ~1);
    } else if (app->win) {
        /* Accurate fallback calculation from actual window geometry before Reaction first layout pass */
        int16 calc_w = (int16)app->win->Width - (int16)app->win->BorderLeft - (int16)app->win->BorderRight;
        int16 calc_h = (int16)app->win->Height - (int16)app->win->BorderTop - (int16)app->win->BorderBottom - 110;
        if (app->prefs.show_status_bar) calc_h -= 24;
        if (calc_w >= 320) w = (uint16_t)(calc_w & ~1);
        if (calc_h >= 200) h = (uint16_t)(calc_h & ~1);
    }

    /* Fallback to user resolution preference if window geometry not yet available */
    if (w < 320 || h < 200) {
        if (app->prefs.resolution.width >= 320 && app->prefs.resolution.height >= 200) {
            w = app->prefs.resolution.width & ~1;
            h = app->prefs.resolution.height & ~1;
        } else {
            w = 1024;
            h = 768;
        }
    }

    if (w > 1920) w = 1920;
    if (h > 1080) h = 1080;

    RendererState *r = &app->renderer;

    if (!r->format_sent) {
        net_send_url(app, "@FORMAT:yuv420");
        r->format_sent = TRUE;
    }

    if (w != r->reported_w || h != r->reported_h) {
        uint32 secs = 0, micros = 0;
        IIntuition->CurrentTime(&secs, &micros);
        int32 diff_s  = (int32)secs - (int32)r->last_sync_secs;
        int32 diff_us = diff_s * 1000000 + ((int32)micros - (int32)r->last_sync_micros);

        /* Rate limit @RESIZE to max 5 Hz (200ms) during active interactive mouse drag */
        if (r->last_sync_secs != 0 && diff_us >= 0 && diff_us < 200000) {
            r->resize_pending = TRUE;
            return;
        }

        r->last_sync_secs   = secs;
        r->last_sync_micros = micros;
        r->resize_pending   = FALSE;

        r->reported_w = w;
        r->reported_h = h;
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "@RESIZE:%ux%u", (unsigned)w, (unsigned)h);
        net_send_url(app, cmd);
        DBG_LOG("[Renderer] Synced 1:1 pixel viewport to server: %ux%u\n", (unsigned)w, (unsigned)h);
    } else {
        r->resize_pending = FALSE;
    }
}

BOOL renderer_map_window_to_remote(
    VortexApp *app,
    int16_t    win_mouse_x,
    int16_t    win_mouse_y,
    int16_t   *out_remote_x,
    int16_t   *out_remote_y
)
{
    if (!app->gad_space) {
        return FALSE;
    }
    struct IBox *area = NULL;
    IIntuition->GetAttr(SPACE_AreaBox, app->gad_space, (uint32 *)&area);
    if (!area || area->Width <= 0 || area->Height <= 0) {
        return FALSE;
    }

    if (win_mouse_x < area->Left || win_mouse_x >= (area->Left + area->Width) ||
        win_mouse_y < area->Top  || win_mouse_y >= (area->Top + area->Height)) {
        return FALSE;
    }

    int32_t rel_x = (int32_t)(win_mouse_x - area->Left);
    int32_t rel_y = (int32_t)(win_mouse_y - area->Top);

    uint16_t sw = app->renderer.src_width  ? app->renderer.src_width  : VORTEXBROWSE_DEFAULT_WIDTH;
    uint16_t sh = app->renderer.src_height ? app->renderer.src_height : VORTEXBROWSE_DEFAULT_HEIGHT;

    int32_t rx = (rel_x * (int32_t)sw) / (int32_t)area->Width;
    int32_t ry = (rel_y * (int32_t)sh) / (int32_t)area->Height;

    if (rx < 0) rx = 0;
    if (rx >= sw) rx = sw - 1;
    if (ry < 0) ry = 0;
    if (ry >= sh) ry = sh - 1;

    *out_remote_x = (int16_t)rx;
    *out_remote_y = (int16_t)ry;
    return TRUE;
}
