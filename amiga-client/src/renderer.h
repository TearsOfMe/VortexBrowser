#ifndef VORTEXBROWSE_RENDERER_H
#define VORTEXBROWSE_RENDERER_H

#include "app.h"

void renderer_init(VortexApp *app);
void renderer_cleanup(VortexApp *app);

/* Decode LZ4 full frame or dirty scanline slice into offscreen BitMap */
BOOL renderer_decode_and_present(
    VortexApp      *app,
    const MsgFrame *hdr,
    const uint8_t  *lz4_data,
    uint32_t        lz4_len
);

/* Flush accumulated dirty scanlines to window RastPort (called once after socket drain) */
void renderer_flush_dirty(VortexApp *app);

/* Composite & blit full current frame into the Reaction space.gadget area */
void renderer_present(VortexApp *app, struct RastPort *rp, struct IBox *area);
void renderer_present_clipped(VortexApp *app, struct RastPort *rp, struct IBox *area, const struct Rectangle *clip_rect);

/* Notify server if the Amiga window's space.gadget dimensions changed for 1:1 native rendering */
void renderer_sync_viewport_size(VortexApp *app);

/* Translate window mouse coordinates to remote browser coordinates (returns TRUE if inside viewport) */
BOOL renderer_map_window_to_remote(
    VortexApp *app,
    int16_t    win_mouse_x,
    int16_t    win_mouse_y,
    int16_t   *out_remote_x,
    int16_t   *out_remote_y
);

#endif /* VORTEXBROWSE_RENDERER_H */
