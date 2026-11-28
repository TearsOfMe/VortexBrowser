/*
 * playerintern - Self-contained FFmpeg + VA-API + AHI Media Player for VideoVortex (AmigaOS 4.1)
 *
 * Built-in libavformat (MP4/MKV/HLS/HTTP/HTTPS via OpenSSL),
 * libavcodec (H.264 VA-API hardware decode, VP9/H.264 software fallback, AAC/MP3/Opus/FLAC/PCM audio),
 * libswresample -> AHI 48kHz stereo PCM, zero-copy vaPutSurface rendering,
 * and auto-hiding ReAction control bar at the bottom of the window.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/timer.h>

#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>
#include <stdarg.h>
#include <graphics/gfx.h>
#include <graphics/layers.h>
#include <graphics/composite.h>
#include <devices/ahi.h>
#include <devices/timer.h>

#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/slider.h>
#include <images/bevel.h>

#include <interfaces/VA.h>
#include <va/va_amigaos.h>

#include <openssl/rand.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vaapi.h>
#include <libavutil/opt.h>
#include <libavutil/log.h>
#include <libavutil/channel_layout.h>

static const char *VERSION_TAG __attribute__((used)) = "\0$VER: playerintern 2.0 (24.11.26)";
static const char *STACK_COOKIE __attribute__((used)) = "\0$STACK:1048576";
uint32 __stack = 1048576;

/* Satisfies sdk_va/va_stubs.c without linking libva.a's fatal autoinit constructors */
struct VAIFace *IVA = NULL;

static FILE *g_log_fp = NULL;
static void plog(const char *fmt, ...)
{
    if (!g_log_fp) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log_fp, fmt, ap);
    va_end(ap);
    fputc('\n', g_log_fp);
    fflush(g_log_fp);
}

static struct ClassLibrary *g_cls_window = NULL;
static struct ClassLibrary *g_cls_layout = NULL;
static struct ClassLibrary *g_cls_button = NULL;
static struct ClassLibrary *g_cls_slider = NULL;

static BOOL open_ctrl_classes(void)
{
    Class *dummy = NULL;
    g_cls_window = IIntuition->OpenClass("window.class", 0, &dummy);
    g_cls_layout = IIntuition->OpenClass("gadgets/layout.gadget", 0, &dummy);
    g_cls_button = IIntuition->OpenClass("gadgets/button.gadget", 0, &dummy);
    g_cls_slider = IIntuition->OpenClass("gadgets/slider.gadget", 0, &dummy);
    return (g_cls_window && g_cls_layout && g_cls_button && g_cls_slider);
}

static void close_ctrl_classes(void)
{
    if (g_cls_slider) { IIntuition->CloseClass(g_cls_slider); g_cls_slider = NULL; }
    if (g_cls_button) { IIntuition->CloseClass(g_cls_button); g_cls_button = NULL; }
    if (g_cls_layout) { IIntuition->CloseClass(g_cls_layout); g_cls_layout = NULL; }
    if (g_cls_window) { IIntuition->CloseClass(g_cls_window); g_cls_window = NULL; }
}

#define AUDIO_RING_SIZE (512 * 1024)
#define AUDIO_BUF_BYTES 16384
#define CTRL_BAR_HEIGHT 38
#define CTRL_HOVER_ZONE 72
#define CTRL_HIDE_MS    2500

#define PPC_SYNC() __asm__ volatile("sync" ::: "memory")

enum {
    GID_PLAYPAUSE = 1,
    GID_STOP,
    GID_SEEK_BACK,
    GID_SEEK_FWD,
    GID_SEEK_SLIDER,
    GID_TIME_LABEL,
    GID_VOL_SLIDER,
    GID_FULLSCREEN
};

typedef struct PlayerState {
    const char *video_url;
    const char *audio_url;
    const char *title;
    BOOL        force_sw;
    BOOL        start_fs;
    BOOL        quiet;

    /* VA-API */
    struct Library  *va_lib;
    struct VAIFace  *iva;
    VADisplay        va_display;
    BOOL             has_va;

    /* Demuxers & Decoders */
    AVFormatContext *vid_fmt;
    AVFormatContext *aud_fmt;
    int              vid_stream_idx;
    int              aud_stream_idx;
    AVCodecContext  *vid_ctx;
    AVCodecContext  *aud_ctx;
    AVBufferRef     *hw_device_ctx;
    SwrContext      *swr;
    int64_t          duration_ms;

    /* Main Window */
    struct Window *win;
    BOOL           fullscreen;
    int            vid_w;
    int            vid_h;
    int            sar_num;
    int            sar_den;
    BOOL           audio_only;

    /* ReAction Floating Control Bar */
    Object        *ctrl_obj;
    struct Window *ctrl_win;
    Object        *gad_playpause;
    Object        *gad_seek_slider;
    Object        *gad_time_label;
    Object        *gad_vol_slider;
    Object        *gad_fullscreen;
    BOOL           ctrl_visible;
    uint64_t       ctrl_last_hover_ms;
    uint64_t       ctrl_last_ui_ms;
    char           time_buf[40];

    /* Control & Sync */
    volatile BOOL     quit;
    volatile BOOL     paused;
    volatile BOOL     audio_running;
    volatile uint64_t audio_bytes_played;
    volatile uint32   ahi_volume; /* 0..0x10000 */
    int64_t           clock_offset_ms;
    int64_t           first_pts_ms;
    int64_t           cur_pos_ms;
    uint64_t          start_ms;
    uint64_t          pause_start_ms;
    BOOL              seek_pending;
    BOOL              seek_dragging;
    BOOL              vol_dragging;
    BOOL              just_seeked;
    BOOL              seek_show_frame;
    int64_t           seek_target_ms;

    /* Audio Ring Buffer (single producer, single consumer) */
    uint8_t         *aring;
    volatile uint32  aring_r;
    volatile uint32  aring_w;
    BOOL             has_audio;

    /* Software fallback bitmap */
    struct BitMap *sw_bm;
    int            sw_bm_w;
    int            sw_bm_h;
} PlayerState;

static PlayerState g_ps;

static uint64_t get_time_ms(void)
{
    struct TimeVal tv;
    if (ITimer) {
        ITimer->GetSysTime(&tv);
        return (uint64_t)tv.Seconds * 1000ULL + (uint64_t)(tv.Microseconds / 1000UL);
    }
    return 0;
}

static int av_interrupt_cb(void *opaque)
{
    PlayerState *ps = (PlayerState *)opaque;
    return ps->quit ? 1 : 0;
}

static enum AVPixelFormat get_vaapi_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts)
{
    (void)ctx;
    const enum AVPixelFormat *first = pix_fmts;
    while (*pix_fmts != AV_PIX_FMT_NONE) {
        if (*pix_fmts == AV_PIX_FMT_VAAPI)
            return AV_PIX_FMT_VAAPI;
        pix_fmts++;
    }
    return *first;
}

static BOOL init_va(PlayerState *ps)
{
    if (ps->force_sw) return FALSE;
    ps->va_lib = IExec->OpenLibrary("va.library", 0L);
    if (!ps->va_lib) return FALSE;

    ps->iva = (struct VAIFace *)IExec->GetInterface(ps->va_lib, "main", 1, NULL);
    if (!ps->iva) {
        IExec->CloseLibrary(ps->va_lib);
        ps->va_lib = NULL;
        return FALSE;
    }

    ps->va_display = ps->iva->vaGetDisplay(0);
    if (ps->va_display) {
        int maj = 0, min = 0;
        if (ps->iva->vaInitialize(ps->va_display, &maj, &min) == VA_STATUS_SUCCESS) {
            IVA = ps->iva;
            ps->has_va = TRUE;
            return TRUE;
        }
        ps->va_display = NULL;
    }

    IExec->DropInterface((struct Interface *)ps->iva);
    ps->iva = NULL;
    IExec->CloseLibrary(ps->va_lib);
    ps->va_lib = NULL;
    return FALSE;
}

static void cleanup_va(PlayerState *ps)
{
    if (ps->has_va && ps->iva && ps->va_display) {
        ps->iva->vaTerminate(ps->va_display);
        ps->va_display = NULL;
        ps->has_va = FALSE;
    }
    IVA = NULL;
    if (ps->iva) {
        IExec->DropInterface((struct Interface *)ps->iva);
        ps->iva = NULL;
    }
    if (ps->va_lib) {
        IExec->CloseLibrary(ps->va_lib);
        ps->va_lib = NULL;
    }
}

static uint32 aring_avail_read(const PlayerState *ps)
{
    uint32 w = ps->aring_w;
    uint32 r = ps->aring_r;
    return (w >= r) ? (w - r) : (AUDIO_RING_SIZE - r + w);
}

static uint32 aring_avail_write(const PlayerState *ps)
{
    return AUDIO_RING_SIZE - 1 - aring_avail_read(ps);
}

static void aring_write(PlayerState *ps, const uint8_t *data, uint32 len)
{
    while (len > 0 && !ps->quit && !ps->seek_pending) {
        uint32 avail = aring_avail_write(ps);
        if (avail == 0) {
            IDOS->Delay(1);
            continue;
        }
        uint32 chunk = (len < avail) ? len : avail;
        uint32 w = ps->aring_w;
        uint32 first = AUDIO_RING_SIZE - w;
        if (first > chunk) first = chunk;
        memcpy(ps->aring + w, data, first);
        if (chunk > first)
            memcpy(ps->aring, data + first, chunk - first);
        PPC_SYNC();
        ps->aring_w = (w + chunk) % AUDIO_RING_SIZE;
        data += chunk;
        len  -= chunk;
    }
}

static uint32 aring_read(PlayerState *ps, uint8_t *dst, uint32 len)
{
    uint32 avail = aring_avail_read(ps);
    if (avail == 0) return 0;
    PPC_SYNC();
    if (len > avail) len = avail;
    uint32 r = ps->aring_r;
    uint32 first = AUDIO_RING_SIZE - r;
    if (first > len) first = len;
    memcpy(dst, ps->aring + r, first);
    if (len > first)
        memcpy(dst + first, ps->aring, len - first);
    PPC_SYNC();
    ps->aring_r = (r + len) % AUDIO_RING_SIZE;
    return len;
}

static void format_time(int64_t ms, char *out, size_t sz)
{
    if (ms < 0) ms = 0;
    int total_s = (int)(ms / 1000);
    int h = total_s / 3600;
    int m = (total_s % 3600) / 60;
    int s = total_s % 60;
    if (h > 0) snprintf(out, sz, "%d:%02d:%02d", h, m, s);
    else       snprintf(out, sz, "%02d:%02d", m, s);
}

static void fill_black_bg(PlayerState *ps)
{
    if (!ps->win || !ps->win->RPort) return;
    int x1 = ps->win->BorderLeft;
    int y1 = ps->win->BorderTop;
    int x2 = ps->win->Width - ps->win->BorderRight - 1;
    int y2 = ps->win->Height - ps->win->BorderBottom - 1;
    if (x2 >= x1 && y2 >= y1) {
        IGraphics->RectFillColor(ps->win->RPort, x1, y1, x2, y2, 0xFF000000U);
    }
}

static void get_ctrl_bar_rect(PlayerState *ps, int *cx, int *cy, int *cw, int *ch)
{
    struct Window *w = ps->win;
    int ix = w->LeftEdge + w->BorderLeft;
    int iw = w->Width - w->BorderLeft - w->BorderRight;
    int ib = w->TopEdge + w->Height - w->BorderBottom;
    if (iw < 320) iw = 320;
    *cx = ix + 8;
    *cw = iw - 16;
    *ch = CTRL_BAR_HEIGHT;
    *cy = ib - CTRL_BAR_HEIGHT - 8;
}

static void create_ctrl_bar(PlayerState *ps)
{
    if (ps->ctrl_obj) return;
    if (!open_ctrl_classes()) {
        plog("ReAction control classes unavailable, skipping control bar");
        return;
    }

    snprintf(ps->time_buf, sizeof(ps->time_buf), "00:00 / 00:00");

    ps->gad_playpause = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID, GID_PLAYPAUSE,
        GA_RelVerify, TRUE,
        GA_Text, (ULONG)(ps->paused ? ">" : "||"),
        BUTTON_DomainString, (ULONG)"||",
        TAG_DONE);

    Object *btn_stop = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID, GID_STOP,
        GA_RelVerify, TRUE,
        GA_Text, (ULONG)"Stop",
        TAG_DONE);

    Object *btn_back = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID, GID_SEEK_BACK,
        GA_RelVerify, TRUE,
        GA_Text, (ULONG)"<<",
        TAG_DONE);

    Object *btn_fwd = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID, GID_SEEK_FWD,
        GA_RelVerify, TRUE,
        GA_Text, (ULONG)">>",
        TAG_DONE);

    ps->gad_seek_slider = IIntuition->NewObject(NULL, "slider.gadget",
        GA_ID, GID_SEEK_SLIDER,
        GA_Immediate, TRUE,
        GA_RelVerify, TRUE,
        ICA_TARGET, ICTARGET_IDCMP,
        SLIDER_Orientation, SORIENT_HORIZ,
        SLIDER_Min, 0,
        SLIDER_Max, 1000,
        SLIDER_Level, 0,
        TAG_DONE);

    ps->gad_time_label = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID, GID_TIME_LABEL,
        GA_ReadOnly, TRUE,
        GA_Text, (ULONG)ps->time_buf,
        BUTTON_BevelStyle, BVS_NONE,
        BUTTON_Transparent, TRUE,
        BUTTON_DomainString, (ULONG)"00:00:00 / 00:00:00",
        TAG_DONE);

    ps->gad_vol_slider = IIntuition->NewObject(NULL, "slider.gadget",
        GA_ID, GID_VOL_SLIDER,
        GA_Immediate, TRUE,
        GA_RelVerify, TRUE,
        ICA_TARGET, ICTARGET_IDCMP,
        SLIDER_Orientation, SORIENT_HORIZ,
        SLIDER_Min, 0,
        SLIDER_Max, 100,
        SLIDER_Level, 100,
        TAG_DONE);

    ps->gad_fullscreen = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID, GID_FULLSCREEN,
        GA_RelVerify, TRUE,
        GA_Text, (ULONG)(ps->fullscreen ? "Win" : "Full"),
        BUTTON_DomainString, (ULONG)"Full",
        TAG_DONE);

    if (!ps->gad_playpause || !btn_stop || !btn_back || !btn_fwd ||
        !ps->gad_seek_slider || !ps->gad_time_label || !ps->gad_vol_slider || !ps->gad_fullscreen) {
        plog("One or more ReAction gadgets failed to allocate");
        return;
    }

    Object *layout = IIntuition->NewObject(NULL, "layout.gadget",
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_SpaceInner, TRUE,
        LAYOUT_BevelStyle, BVS_THIN,
        LAYOUT_AddChild, (ULONG)ps->gad_playpause,
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild, (ULONG)btn_stop,
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild, (ULONG)btn_back,
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild, (ULONG)btn_fwd,
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild, (ULONG)ps->gad_seek_slider,
        CHILD_WeightedWidth, 65,
        LAYOUT_AddChild, (ULONG)ps->gad_time_label,
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild, (ULONG)ps->gad_vol_slider,
        CHILD_WeightedWidth, 20,
        LAYOUT_AddChild, (ULONG)ps->gad_fullscreen,
        CHILD_WeightedWidth, 0,
        TAG_DONE);

    ps->ctrl_obj = IIntuition->NewObject(NULL, "window.class",
        WA_Borderless, TRUE,
        WA_Activate, FALSE,
        WA_ReportMouse, TRUE,
        WA_IDCMP, IDCMP_GADGETDOWN | IDCMP_GADGETUP | IDCMP_MOUSEMOVE | IDCMP_RAWKEY | IDCMP_VANILLAKEY,
        WINDOW_Layout, (ULONG)layout,
        TAG_DONE);
}

static void show_ctrl_bar(PlayerState *ps)
{
    if (!ps->win || !ps->ctrl_obj) return;
    ps->ctrl_last_hover_ms = get_time_ms();

    int cx, cy, cw, ch;
    get_ctrl_bar_rect(ps, &cx, &cy, &cw, &ch);

    if (!ps->ctrl_visible) {
        IIntuition->SetAttrs(ps->ctrl_obj,
            WA_Left, cx,
            WA_Top, cy,
            WA_Width, cw,
            WA_Height, ch,
            TAG_DONE);
        ps->ctrl_win = (struct Window *)IIntuition->IDoMethod(ps->ctrl_obj, WM_OPEN);
        if (ps->ctrl_win) {
            ps->ctrl_visible = TRUE;
            IIntuition->WindowToFront(ps->ctrl_win);
        }
    } else if (ps->ctrl_win) {
        if (ps->ctrl_win->LeftEdge != cx || ps->ctrl_win->TopEdge != cy || ps->ctrl_win->Width != cw) {
            IIntuition->ChangeWindowBox(ps->ctrl_win, cx, cy, cw, ch);
        }
        IIntuition->WindowToFront(ps->ctrl_win);
    }
}

static void hide_ctrl_bar(PlayerState *ps)
{
    if (ps->ctrl_visible && ps->ctrl_obj) {
        ps->seek_dragging = FALSE;
        ps->vol_dragging = FALSE;
        IIntuition->IDoMethod(ps->ctrl_obj, WM_CLOSE);
        ps->ctrl_win = NULL;
        ps->ctrl_visible = FALSE;
    }
}

static void update_ctrl_ui(PlayerState *ps)
{
    if (!ps->ctrl_visible || !ps->ctrl_win) return;
    uint64_t now = get_time_ms();
    if (now - ps->ctrl_last_ui_ms < 250) return;
    ps->ctrl_last_ui_ms = now;

    if (!ps->seek_dragging) {
        char cur_s[16], tot_s[16];
        format_time(ps->cur_pos_ms, cur_s, sizeof(cur_s));
        format_time(ps->duration_ms > 0 ? ps->duration_ms : 0, tot_s, sizeof(tot_s));
        snprintf(ps->time_buf, sizeof(ps->time_buf), "%s / %s", cur_s, tot_s);

        IIntuition->SetGadgetAttrs((struct Gadget *)ps->gad_time_label, ps->ctrl_win, NULL,
            GA_Text, (ULONG)ps->time_buf,
            TAG_DONE);

        if (ps->duration_ms > 0) {
            int lvl = (int)((ps->cur_pos_ms * 1000LL) / ps->duration_ms);
            if (lvl < 0) lvl = 0;
            if (lvl > 1000) lvl = 1000;
            IIntuition->SetGadgetAttrs((struct Gadget *)ps->gad_seek_slider, ps->ctrl_win, NULL,
                SLIDER_Level, lvl,
                TAG_DONE);
        }
    }
}

static void request_seek(PlayerState *ps, int64_t target_ms)
{
    if (target_ms < 0) target_ms = 0;
    if (ps->duration_ms > 0 && target_ms > ps->duration_ms)
        target_ms = ps->duration_ms;
    ps->seek_target_ms = target_ms;
    ps->seek_pending = TRUE;
}

static void toggle_pause(PlayerState *ps)
{
    uint64_t now = get_time_ms();
    if (!ps->paused) {
        ps->pause_start_ms = now;
        ps->paused = TRUE;
    } else {
        if (ps->pause_start_ms > 0 && now > ps->pause_start_ms)
            ps->start_ms += (now - ps->pause_start_ms);
        ps->paused = FALSE;
    }
    if (ps->ctrl_win && ps->gad_playpause) {
        IIntuition->SetGadgetAttrs((struct Gadget *)ps->gad_playpause, ps->ctrl_win, NULL,
            GA_Text, (ULONG)(ps->paused ? ">" : "||"),
            TAG_DONE);
    }
}

static struct Window *open_player_window(PlayerState *ps, BOOL fs)
{
    struct Screen *scr = IIntuition->LockPubScreen(NULL);
    int sw = scr ? scr->Width : 1280;
    int sh = scr ? scr->Height : 720;

    struct Window *w = NULL;
    if (fs && !ps->audio_only) {
        w = IIntuition->OpenWindowTags(NULL,
            WA_Left, 0,
            WA_Top, 0,
            WA_Width, sw,
            WA_Height, sh,
            WA_Borderless, TRUE,
            WA_Backdrop, FALSE,
            WA_Activate, TRUE,
            WA_ReportMouse, TRUE,
            WA_RMBTrap, TRUE,
            WA_IDCMP, IDCMP_MOUSEMOVE | IDCMP_RAWKEY | IDCMP_VANILLAKEY | IDCMP_MOUSEBUTTONS,
            TAG_DONE);
    } else {
        int ww = ps->audio_only ? 520 : ((ps->vid_w >= 320 && ps->vid_w <= sw - 80) ? ps->vid_w : 854);
        int wh = ps->audio_only ? 96  : ((ps->vid_h >= 240 && ps->vid_h <= sh - 80) ? ps->vid_h : 480);
        const char *default_title = ps->audio_only
            ? "Intern Audio (FFmpeg + AHI)"
            : (ps->has_va ? "Intern (FFmpeg + VA-API)" : "Intern (FFmpeg Software)");
        w = IIntuition->OpenWindowTags(NULL,
            WA_Title, (ULONG)(ps->title ? ps->title : default_title),
            WA_Left, (sw - ww) / 2,
            WA_Top, (sh - wh) / 2,
            WA_InnerWidth, ww,
            WA_InnerHeight, wh,
            WA_DragBar, TRUE,
            WA_DepthGadget, TRUE,
            WA_CloseGadget, TRUE,
            WA_SizeGadget, !ps->audio_only,
            WA_Activate, TRUE,
            WA_ReportMouse, TRUE,
            WA_MinWidth, 340,
            WA_MinHeight, ps->audio_only ? 72 : 220,
            WA_MaxWidth, sw,
            WA_MaxHeight, sh,
            WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_NEWSIZE | IDCMP_CHANGEWINDOW | IDCMP_MOUSEMOVE | IDCMP_RAWKEY | IDCMP_VANILLAKEY,
            TAG_DONE);
    }

    if (scr) IIntuition->UnlockPubScreen(NULL, scr);
    ps->fullscreen = (fs && !ps->audio_only);
    if (w) {
        ps->win = w;
        fill_black_bg(ps);
    }
    return w;
}

static void toggle_fullscreen(PlayerState *ps)
{
    if (ps->audio_only) return;
    BOOL was_vis = ps->ctrl_visible;
    hide_ctrl_bar(ps);
    if (ps->win) {
        IIntuition->CloseWindow(ps->win);
        ps->win = NULL;
    }
    ps->win = open_player_window(ps, !ps->fullscreen);
    if (ps->gad_fullscreen) {
        IIntuition->SetAttrs(ps->gad_fullscreen,
            GA_Text, (ULONG)(ps->fullscreen ? "Win" : "Full"),
            TAG_DONE);
    }
    if (was_vis) show_ctrl_bar(ps);
}

static void calc_aspect_rect(PlayerState *ps, int *dx, int *dy, int *dw, int *dh)
{
    struct Window *w = ps->win;
    int bx = w->BorderLeft;
    int by = w->BorderTop;
    int bw = w->Width - w->BorderLeft - w->BorderRight;
    int bh = w->Height - w->BorderTop - w->BorderBottom;
    if (bw < 16) bw = 16;
    if (bh < 16) bh = 16;

    int vw = (ps->vid_w > 0) ? ps->vid_w : bw;
    int vh = (ps->vid_h > 0) ? ps->vid_h : bh;
    if (ps->sar_num > 0 && ps->sar_den > 0 && ps->sar_num != ps->sar_den) {
        vw = (int)(((int64_t)vw * ps->sar_num) / ps->sar_den);
        if (vw < 16) vw = 16;
    }

    if ((int64_t)bw * vh > (int64_t)bh * vw) {
        *dh = bh & ~1;
        *dw = (int)(((int64_t)bh * vw) / vh) & ~1;
    } else {
        *dw = bw & ~1;
        *dh = (int)(((int64_t)bw * vh) / vw) & ~1;
    }
    *dx = bx + (bw - *dw) / 2;
    *dy = by + (bh - *dh) / 2;
}

typedef struct SwCompHookData {
    struct BitMap *srcBitMap;
    uint32         srcWidth;
    uint32         srcHeight;
    int32          offsetX;
    int32          offsetY;
    int32          scaleX;
    int32          scaleY;
    uint32         retCode;
} SwCompHookData;

static ULONG sw_composite_hook(struct Hook *hook, struct RastPort *rp, struct BackFillMessage *msg)
{
    SwCompHookData *hd = (SwCompHookData *)hook->h_Data;
    if (!hd || !rp || !rp->BitMap || !msg) return 0;

    hd->retCode = IGraphics->CompositeTags(
        COMPOSITE_Src_Over_Dest,
        hd->srcBitMap,
        rp->BitMap,
        COMPTAG_SrcWidth,   hd->srcWidth,
        COMPTAG_SrcHeight,  hd->srcHeight,
        COMPTAG_ScaleX,     hd->scaleX,
        COMPTAG_ScaleY,     hd->scaleY,
        COMPTAG_OffsetX,    msg->Bounds.MinX - (msg->OffsetX - hd->offsetX),
        COMPTAG_OffsetY,    msg->Bounds.MinY - (msg->OffsetY - hd->offsetY),
        COMPTAG_DestX,      msg->Bounds.MinX,
        COMPTAG_DestY,      msg->Bounds.MinY,
        COMPTAG_DestWidth,  msg->Bounds.MaxX - msg->Bounds.MinX + 1,
        COMPTAG_DestHeight, msg->Bounds.MaxY - msg->Bounds.MinY + 1,
        COMPTAG_Flags,      COMPFLAG_IgnoreDestAlpha | COMPFLAG_SrcFilter,
        TAG_DONE);
    return 0;
}

static void render_sw_frame(PlayerState *ps, AVFrame *f)
{
    if (!ps->win || !ps->win->RPort) return;
    int w = f->width;
    int h = f->height;
    if (w < 16 || h < 16) return;

    if (!ps->sw_bm || ps->sw_bm_w != w || ps->sw_bm_h != h) {
        if (ps->sw_bm) {
            IGraphics->FreeBitMap(ps->sw_bm);
            ps->sw_bm = NULL;
        }
        ps->sw_bm = IGraphics->AllocBitMapTags(w, h, 32,
            BMATags_PixelFormat, PIXF_A8R8G8B8,
            BMATags_Displayable, TRUE,
            TAG_DONE);
        ps->sw_bm_w = w;
        ps->sw_bm_h = h;
    }
    if (!ps->sw_bm) return;

    APTR base = NULL;
    uint32 bpr = 0;
    APTR lock = IGraphics->LockBitMapTags(ps->sw_bm,
        LBM_BaseAddress, &base,
        LBM_BytesPerRow, &bpr,
        TAG_DONE);
    if (lock && base) {
        const uint8_t *yp = f->data[0];
        const uint8_t *up = f->data[1];
        const uint8_t *vp = f->data[2];
        BOOL is_nv12 = (f->format == AV_PIX_FMT_NV12);
        for (int y = 0; y < h; y++) {
            uint32 *dst = (uint32 *)((uint8_t *)base + (size_t)y * bpr);
            const uint8_t *yr = yp + (size_t)y * f->linesize[0];
            const uint8_t *ur = up + (size_t)(y >> 1) * f->linesize[1];
            const uint8_t *vr = is_nv12 ? ur : (vp + (size_t)(y >> 1) * f->linesize[2]);
            for (int x = 0; x < w; x++) {
                int Y = (int)yr[x] - 16;
                int U = (int)(is_nv12 ? ur[(x & ~1)] : ur[x >> 1]) - 128;
                int V = (int)(is_nv12 ? vr[(x & ~1) + 1] : vr[x >> 1]) - 128;
                int C = 298 * Y;
                int r = (C + 409 * V + 128) >> 8;
                int g = (C - 100 * U - 208 * V + 128) >> 8;
                int b = (C + 516 * U + 128) >> 8;
                if (r < 0) r = 0; else if (r > 255) r = 255;
                if (g < 0) g = 0; else if (g > 255) g = 255;
                if (b < 0) b = 0; else if (b > 255) b = 255;
                dst[x] = 0xFF000000U | ((uint32)r << 16) | ((uint32)g << 8) | (uint32)b;
            }
        }
        IGraphics->UnlockBitMap(lock);
    }

    int dx, dy, dw, dh;
    calc_aspect_rect(ps, &dx, &dy, &dw, &dh);

    BOOL comp_ok = FALSE;
    if (ps->win->RPort->Layer && ps->win->RPort->BitMap) {
        struct Rectangle rect;
        rect.MinX = dx;
        rect.MinY = dy;
        rect.MaxX = dx + dw - 1;
        rect.MaxY = dy + dh - 1;

        SwCompHookData hd;
        hd.srcBitMap = ps->sw_bm;
        hd.srcWidth  = (uint32)w;
        hd.srcHeight = (uint32)h;
        hd.offsetX   = dx;
        hd.offsetY   = dy;
        hd.scaleX    = COMP_FLOAT_TO_FIX((float)dw / (float)w);
        hd.scaleY    = COMP_FLOAT_TO_FIX((float)dh / (float)h);
        hd.retCode   = COMPERR_Success;

        struct Hook hook;
        memset(&hook, 0, sizeof(hook));
        hook.h_Entry = (HOOKFUNC)sw_composite_hook;
        hook.h_Data  = &hd;

        ILayers->DoHookClipRects(&hook, ps->win->RPort, &rect);
        comp_ok = (hd.retCode == COMPERR_Success);
    }

    if (!comp_ok) {
        int bw = (w < dw) ? w : dw;
        int bh = (h < dh) ? h : dh;
        IGraphics->BltBitMapRastPort(ps->sw_bm, 0, 0, ps->win->RPort, dx, dy, bw, bh, 0xC0);
    }
}

static void render_frame(PlayerState *ps, AVFrame *f)
{
    if (!ps->win || !ps->win->RPort || !ps->win->WLayer) return;

    if (f->width >= 16 && f->height >= 16) {
        ps->vid_w = f->width;
        ps->vid_h = f->height;
    }

    if (f->format == AV_PIX_FMT_VAAPI && ps->has_va && ps->iva && ps->va_display) {
        VASurfaceID surf = (VASurfaceID)(uintptr_t)f->data[3];
        int dx, dy, dw, dh;
        calc_aspect_rect(ps, &dx, &dy, &dw, &dh);
        ILayers->LockLayer(0, ps->win->WLayer);
        VAStatus st = ps->iva->vaPutSurface(
            ps->va_display,
            surf,
            VADT_RastPort,
            ps->win->RPort,
            0, 0,
            ps->vid_w, ps->vid_h,
            dx, dy,
            dw, dh,
            VA_FRAME_PICTURE
        );
        ILayers->UnlockLayer(ps->win->WLayer);
        if (st == VA_STATUS_SUCCESS) return;
    }

    render_sw_frame(ps, f);
}

static void handle_ctrl_events(PlayerState *ps)
{
    if (!ps->ctrl_visible || !ps->ctrl_obj) return;
    uint32 res;
    int16 code = 0;
    while ((res = IIntuition->IDoMethod(ps->ctrl_obj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
        uint32 cls = res & WMHI_CLASSMASK;
        uint32 gid = res & WMHI_GADGETMASK;
        ps->ctrl_last_hover_ms = get_time_ms();

        if (cls == WMHI_GADGETDOWN) {
            if (gid == GID_SEEK_SLIDER) ps->seek_dragging = TRUE;
            else if (gid == GID_VOL_SLIDER) ps->vol_dragging = TRUE;
        } else if (cls == WMHI_MOUSEMOVE) {
            if (ps->seek_dragging && ps->duration_ms > 0 && ps->ctrl_win) {
                uint32 lvl = 0;
                IIntuition->GetAttr(SLIDER_Level, ps->gad_seek_slider, &lvl);
                int64_t preview_ms = (ps->duration_ms * (int64_t)lvl) / 1000LL;
                char cur_s[16], tot_s[16];
                format_time(preview_ms, cur_s, sizeof(cur_s));
                format_time(ps->duration_ms, tot_s, sizeof(tot_s));
                snprintf(ps->time_buf, sizeof(ps->time_buf), "%s / %s", cur_s, tot_s);
                IIntuition->SetGadgetAttrs((struct Gadget *)ps->gad_time_label, ps->ctrl_win, NULL,
                    GA_Text, (ULONG)ps->time_buf, TAG_DONE);
            }
            if (ps->vol_dragging && ps->ctrl_win) {
                uint32 vol = 100;
                IIntuition->GetAttr(SLIDER_Level, ps->gad_vol_slider, &vol);
                ps->ahi_volume = (vol * 0x10000U) / 100U;
            }
        } else if (cls == WMHI_GADGETUP) {
            switch (gid) {
                case GID_PLAYPAUSE:
                    toggle_pause(ps);
                    break;
                case GID_STOP:
                    ps->quit = TRUE;
                    break;
                case GID_SEEK_BACK:
                    request_seek(ps, ps->cur_pos_ms - 10000);
                    break;
                case GID_SEEK_FWD:
                    request_seek(ps, ps->cur_pos_ms + 10000);
                    break;
                case GID_SEEK_SLIDER:
                    ps->seek_dragging = FALSE;
                    if (ps->duration_ms > 0 && ps->ctrl_win) {
                        uint32 lvl = 0;
                        IIntuition->GetAttr(SLIDER_Level, ps->gad_seek_slider, &lvl);
                        request_seek(ps, (ps->duration_ms * (int64_t)lvl) / 1000LL);
                    }
                    break;
                case GID_VOL_SLIDER:
                    ps->vol_dragging = FALSE;
                    if (ps->ctrl_win) {
                        uint32 vol = 100;
                        IIntuition->GetAttr(SLIDER_Level, ps->gad_vol_slider, &vol);
                        ps->ahi_volume = (vol * 0x10000U) / 100U;
                    }
                    break;
                case GID_FULLSCREEN:
                    toggle_fullscreen(ps);
                    break;
            }
        } else if (cls == WMHI_RAWKEY) {
            if ((code & 0x7F) == 0x45) ps->quit = TRUE;
        }
    }
}

static void handle_window_events(PlayerState *ps)
{
    if (!ps->win) return;
    struct IntuiMessage *msg;
    while ((msg = (struct IntuiMessage *)IExec->GetMsg(ps->win->UserPort)) != NULL) {
        ULONG cls  = msg->Class;
        UWORD code = msg->Code;
        WORD  my   = msg->MouseY;
        IExec->ReplyMsg((struct Message *)msg);

        if (cls == IDCMP_CLOSEWINDOW) {
            ps->quit = TRUE;
        } else if (cls == IDCMP_NEWSIZE || cls == IDCMP_CHANGEWINDOW) {
            if (cls == IDCMP_NEWSIZE) fill_black_bg(ps);
            if (ps->ctrl_visible) show_ctrl_bar(ps);
        } else if (cls == IDCMP_MOUSEMOVE) {
            int bottom_y = ps->win->Height - ps->win->BorderBottom;
            if (ps->audio_only || (my >= bottom_y - CTRL_HOVER_ZONE && my <= bottom_y)) {
                show_ctrl_bar(ps);
            }
        } else if (cls == IDCMP_RAWKEY) {
            if (code == 0x4F) { request_seek(ps, ps->cur_pos_ms - 10000); show_ctrl_bar(ps); }
            else if (code == 0x4E) { request_seek(ps, ps->cur_pos_ms + 10000); show_ctrl_bar(ps); }
        } else if (cls == IDCMP_VANILLAKEY) {
            if (code == 27 || code == 'q' || code == 'Q') {
                ps->quit = TRUE;
            } else if (code == ' ') {
                toggle_pause(ps);
                show_ctrl_bar(ps);
            } else if (code == 'f' || code == 'F') {
                toggle_fullscreen(ps);
            }
        }
    }

    handle_ctrl_events(ps);

    /* Auto-hide control bar when mouse is away, not dragging, and timeout expired */
    if (!ps->audio_only && ps->ctrl_visible && ps->ctrl_win) {
        struct Screen *scr = ps->win->WScreen;
        BOOL over_ctrl = FALSE;
        if (scr) {
            int sx = scr->MouseX;
            int sy = scr->MouseY;
            if (sx >= ps->ctrl_win->LeftEdge && sx <= ps->ctrl_win->LeftEdge + ps->ctrl_win->Width &&
                sy >= ps->ctrl_win->TopEdge  && sy <= ps->ctrl_win->TopEdge + ps->ctrl_win->Height) {
                over_ctrl = TRUE;
                ps->ctrl_last_hover_ms = get_time_ms();
            }
        }
        if (!over_ctrl && !ps->seek_dragging && !ps->vol_dragging &&
            (get_time_ms() - ps->ctrl_last_hover_ms > CTRL_HIDE_MS)) {
            hide_ctrl_bar(ps);
        }
    }

    update_ctrl_ui(ps);
}

static void audio_task_entry(void)
{
    PlayerState *ps = &g_ps;
    ps->audio_running = TRUE;

    struct MsgPort *mp = IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
    struct AHIRequest *req[2] = {NULL, NULL};
    uint8_t *buf[2] = {NULL, NULL};

    if (mp) {
        req[0] = (struct AHIRequest *)IExec->AllocSysObjectTags(ASOT_IOREQUEST,
            ASOIOR_Size, sizeof(struct AHIRequest),
            ASOIOR_ReplyPort, mp,
            TAG_DONE);
        if (req[0]) {
            req[0]->ahir_Version = 4;
            if (IExec->OpenDevice(AHINAME, AHI_DEFAULT_UNIT, (struct IORequest *)req[0], 0) == 0) {
                req[1] = (struct AHIRequest *)IExec->AllocSysObjectTags(ASOT_IOREQUEST,
                    ASOIOR_Duplicate, req[0],
                    TAG_DONE);
                buf[0] = (uint8_t *)IExec->AllocVecTags(AUDIO_BUF_BYTES, AVT_Type, MEMF_SHARED, TAG_DONE);
                buf[1] = (uint8_t *)IExec->AllocVecTags(AUDIO_BUF_BYTES, AVT_Type, MEMF_SHARED, TAG_DONE);
            }
        }
    }

    if (req[0] && req[1] && buf[0] && buf[1]) {
        struct AHIRequest *link = NULL;
        int cur = 0;
        while (!ps->quit) {
            if (ps->paused || ps->seek_pending) {
                IDOS->Delay(1);
                continue;
            }
            uint32 got = aring_read(ps, buf[cur], AUDIO_BUF_BYTES);
            if (got == 0) {
                IDOS->Delay(1);
                continue;
            }

            req[cur]->ahir_Std.io_Message.mn_Node.ln_Pri = 0;
            req[cur]->ahir_Std.io_Command = CMD_WRITE;
            req[cur]->ahir_Std.io_Data    = buf[cur];
            req[cur]->ahir_Std.io_Length  = (ULONG)got;
            req[cur]->ahir_Std.io_Offset  = 0;
            req[cur]->ahir_Frequency      = 48000;
            req[cur]->ahir_Type           = AHIST_S16S;
            req[cur]->ahir_Volume         = ps->ahi_volume;
            req[cur]->ahir_Position       = 0x8000;
            req[cur]->ahir_Link           = link;
            IExec->SendIO((struct IORequest *)req[cur]);

            if (link) {
                IExec->WaitIO((struct IORequest *)link);
                ps->audio_bytes_played += link->ahir_Std.io_Actual;
            }
            link = req[cur];
            cur ^= 1;
        }
        if (link) {
            IExec->WaitIO((struct IORequest *)link);
            ps->audio_bytes_played += link->ahir_Std.io_Actual;
        }
    }

    if (buf[0]) IExec->FreeVec(buf[0]);
    if (buf[1]) IExec->FreeVec(buf[1]);
    if (req[1]) IExec->FreeSysObject(ASOT_IOREQUEST, req[1]);
    if (req[0]) {
        if (req[0]->ahir_Std.io_Device)
            IExec->CloseDevice((struct IORequest *)req[0]);
        IExec->FreeSysObject(ASOT_IOREQUEST, req[0]);
    }
    if (mp) IExec->FreeSysObject(ASOT_PORT, mp);
    ps->audio_running = FALSE;
}

static AVFormatContext *open_av_input(PlayerState *ps, const char *url)
{
    AVFormatContext *fmt = avformat_alloc_context();
    if (!fmt) return NULL;
    fmt->interrupt_callback.callback = av_interrupt_cb;
    fmt->interrupt_callback.opaque   = ps;

    AVDictionary *opts = NULL;
    av_dict_set(&opts, "user_agent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36", 0);
    av_dict_set(&opts, "tls_verify", "0", 0);
    av_dict_set(&opts, "protocol_whitelist", "file,http,https,tls,tcp,crypto,data", 0);
    av_dict_set(&opts, "reconnect", "1", 0);
    av_dict_set(&opts, "reconnect_streamed", "1", 0);
    av_dict_set(&opts, "timeout", "15000000", 0);

    int err = avformat_open_input(&fmt, url, NULL, &opts);
    av_dict_free(&opts);
    if (err < 0) {
        char errbuf[128] = {0};
        av_strerror(err, errbuf, sizeof(errbuf));
        plog("avformat_open_input failed (%d: %s) for: %.180s", err, errbuf, url ? url : "(null)");
        return NULL;
    }

    err = avformat_find_stream_info(fmt, NULL);
    if (err < 0) {
        char errbuf[128] = {0};
        av_strerror(err, errbuf, sizeof(errbuf));
        plog("avformat_find_stream_info failed (%d: %s)", err, errbuf);
        avformat_close_input(&fmt);
        return NULL;
    }
    return fmt;
}

static void decode_audio_packet(PlayerState *ps, AVPacket *pkt, AVFrame *aframe)
{
    if (!ps->aud_ctx || !ps->swr) return;
    if (avcodec_send_packet(ps->aud_ctx, pkt) < 0) return;

    while (avcodec_receive_frame(ps->aud_ctx, aframe) == 0) {
        int out_samples = swr_get_out_samples(ps->swr, aframe->nb_samples);
        if (out_samples <= 0) continue;
        uint8_t *out_buf = NULL;
        int out_linesize = 0;
        if (av_samples_alloc(&out_buf, &out_linesize, 2, out_samples, AV_SAMPLE_FMT_S16, 0) >= 0) {
            int conv = swr_convert(ps->swr, &out_buf, out_samples,
                (const uint8_t **)aframe->data, aframe->nb_samples);
            if (conv > 0) {
                aring_write(ps, out_buf, (uint32)(conv * 4));
            }
            av_freep(&out_buf);
        }
    }
}

static void perform_seek(PlayerState *ps, AVStream *vst)
{
    int64_t target_ms = ps->seek_target_ms;

    if (vst && ps->vid_stream_idx >= 0 && ps->vid_ctx) {
        int64_t ts = (int64_t)(((double)target_ms / 1000.0) / av_q2d(vst->time_base));
        if (av_seek_frame(ps->vid_fmt, ps->vid_stream_idx, ts, AVSEEK_FLAG_BACKWARD) < 0) {
            avformat_seek_file(ps->vid_fmt, -1, INT64_MIN, target_ms * 1000LL, target_ms * 1000LL, AVSEEK_FLAG_BACKWARD);
        }
        avcodec_flush_buffers(ps->vid_ctx);
    } else if (ps->aud_stream_idx >= 0) {
        AVFormatContext *af = ps->aud_fmt ? ps->aud_fmt : ps->vid_fmt;
        AVStream *ast = af->streams[ps->aud_stream_idx];
        int64_t ats = (int64_t)(((double)target_ms / 1000.0) / av_q2d(ast->time_base));
        if (av_seek_frame(af, ps->aud_stream_idx, ats, AVSEEK_FLAG_BACKWARD) < 0) {
            avformat_seek_file(af, -1, INT64_MIN, target_ms * 1000LL, target_ms * 1000LL, AVSEEK_FLAG_BACKWARD);
        }
    }

    if (ps->aud_fmt && ps->aud_stream_idx >= 0) {
        AVStream *ast = ps->aud_fmt->streams[ps->aud_stream_idx];
        int64_t ats = (int64_t)(((double)target_ms / 1000.0) / av_q2d(ast->time_base));
        if (av_seek_frame(ps->aud_fmt, ps->aud_stream_idx, ats, AVSEEK_FLAG_BACKWARD) < 0) {
            avformat_seek_file(ps->aud_fmt, -1, INT64_MIN, target_ms * 1000LL, target_ms * 1000LL, AVSEEK_FLAG_BACKWARD);
        }
    }
    if (ps->aud_ctx) avcodec_flush_buffers(ps->aud_ctx);

    PPC_SYNC();
    ps->aring_r = 0;
    ps->aring_w = 0;
    ps->audio_bytes_played = 0;
    ps->clock_offset_ms = target_ms;
    ps->cur_pos_ms = target_ms;
    ps->start_ms = get_time_ms();
    ps->just_seeked = TRUE;
    ps->seek_show_frame = ps->paused;
    ps->seek_pending = FALSE;
}

int main(int argc, char **argv)
{
    memset(&g_ps, 0, sizeof(g_ps));
    g_ps.vid_w = 854;
    g_ps.vid_h = 480;
    g_ps.vid_stream_idx = -1;
    g_ps.aud_stream_idx = -1;
    g_ps.ahi_volume = 0x10000U;
    g_ps.first_pts_ms = -1;

    g_log_fp = fopen("T:playerintern.log", "w");
    plog("playerintern start argc=%d", argc);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-audiofile") == 0 && i + 1 < argc) {
            g_ps.audio_url = argv[++i];
        } else if (strcmp(argv[i], "-title") == 0 && i + 1 < argc) {
            g_ps.title = argv[++i];
        } else if (strcmp(argv[i], "-vo") == 0 && i + 1 < argc) {
            const char *vo = argv[++i];
            if (strcmp(vo, "sw") == 0) g_ps.force_sw = TRUE;
        } else if (strcmp(argv[i], "-fs") == 0) {
            g_ps.start_fs = TRUE;
        } else if (strcmp(argv[i], "-quiet") == 0 ||
                   strcmp(argv[i], "-really-quiet") == 0 ||
                   strcmp(argv[i], "-cookies") == 0 ||
                   strcmp(argv[i], "-framedrop") == 0 ||
                   strcmp(argv[i], "-hardframedrop") == 0 ||
                   strcmp(argv[i], "-infbuf") == 0 ||
                   strcmp(argv[i], "-autoexit") == 0) {
            if (strcmp(argv[i], "-quiet") == 0 || strcmp(argv[i], "-really-quiet") == 0)
                g_ps.quiet = TRUE;
        } else if ((strcmp(argv[i], "-cookies-file") == 0 ||
                    strcmp(argv[i], "-sub") == 0 ||
                    strcmp(argv[i], "-cache") == 0 ||
                    strcmp(argv[i], "-cache-min") == 0 ||
                    strcmp(argv[i], "-ao") == 0 ||
                    strcmp(argv[i], "-af") == 0 ||
                    strcmp(argv[i], "-vf") == 0) && i + 1 < argc) {
            i++;
        } else if (argv[i][0] != '-') {
            if (!g_ps.video_url) g_ps.video_url = argv[i];
        }
    }

    if (!g_ps.video_url) {
        plog("No video_url given");
        if (!g_ps.quiet)
            printf("Usage: playerintern [-audiofile <url>] [-title <name>] [-vo vaapi|sw] [-fs] <url_or_file>\n");
        if (g_log_fp) { fclose(g_log_fp); g_log_fp = NULL; }
        return 5;
    }

    {
        struct TimeVal tv = {0, 0};
        if (ITimer) ITimer->GetSysTime(&tv);
        uint32 seed[4] = { tv.Seconds, tv.Microseconds, (uint32)(uintptr_t)&g_ps, (uint32)argc };
        RAND_seed(seed, sizeof(seed));
    }

    int rc = 10;
    AVPacket *pkt    = NULL;
    AVPacket *apkt   = NULL;
    AVFrame  *vframe = NULL;
    AVFrame  *aframe = NULL;

    av_log_set_level(AV_LOG_QUIET);
    avformat_network_init();
    init_va(&g_ps);
    plog("VA-API=%d audio_url=%s", (int)g_ps.has_va, g_ps.audio_url ? "yes" : "no");

    g_ps.vid_fmt = open_av_input(&g_ps, g_ps.video_url);
    if (!g_ps.vid_fmt) {
        plog("Failed to open video_url");
        if (!g_ps.quiet) printf("playerintern: Failed to open media stream\n");
        goto cleanup;
    }

    if (g_ps.vid_fmt->duration != AV_NOPTS_VALUE && g_ps.vid_fmt->duration > 0)
        g_ps.duration_ms = g_ps.vid_fmt->duration / 1000LL;

    g_ps.vid_stream_idx = av_find_best_stream(g_ps.vid_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    AVStream *vst = NULL;
    if (g_ps.vid_stream_idx >= 0) {
        vst = g_ps.vid_fmt->streams[g_ps.vid_stream_idx];
        if (g_ps.duration_ms <= 0 && vst->duration != AV_NOPTS_VALUE && vst->duration > 0)
            g_ps.duration_ms = (int64_t)(vst->duration * av_q2d(vst->time_base) * 1000.0);
        const AVCodec *vcodec = avcodec_find_decoder(vst->codecpar->codec_id);
        if (!vcodec) {
            plog("Unsupported video codec id=%d", (int)vst->codecpar->codec_id);
            if (!g_ps.quiet) printf("playerintern: Unsupported video codec\n");
            goto cleanup;
        }

        g_ps.vid_ctx = avcodec_alloc_context3(vcodec);
        avcodec_parameters_to_context(g_ps.vid_ctx, vst->codecpar);
        if (vst->codecpar->width >= 16)  g_ps.vid_w = vst->codecpar->width;
        if (vst->codecpar->height >= 16) g_ps.vid_h = vst->codecpar->height;
        if (vst->sample_aspect_ratio.num > 0 && vst->sample_aspect_ratio.den > 0) {
            g_ps.sar_num = vst->sample_aspect_ratio.num;
            g_ps.sar_den = vst->sample_aspect_ratio.den;
        }

        if (g_ps.has_va && g_ps.va_display && vst->codecpar->codec_id == AV_CODEC_ID_H264) {
            AVBufferRef *hw = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VAAPI);
            if (hw) {
                AVHWDeviceContext *dctx = (AVHWDeviceContext *)hw->data;
                AVVAAPIDeviceContext *vctx = (AVVAAPIDeviceContext *)dctx->hwctx;
                vctx->display = (VADisplay)g_ps.va_display;
                if (av_hwdevice_ctx_init(hw) >= 0) {
                    g_ps.vid_ctx->hw_device_ctx = av_buffer_ref(hw);
                    g_ps.vid_ctx->get_format = get_vaapi_format;
                    g_ps.hw_device_ctx = hw;
                } else {
                    av_buffer_unref(&hw);
                }
            }
        }

        if (avcodec_open2(g_ps.vid_ctx, vcodec, NULL) < 0) {
            if (g_ps.vid_ctx->hw_device_ctx) {
                plog("VA-API avcodec_open2 failed -> retrying software decode");
                av_buffer_unref(&g_ps.vid_ctx->hw_device_ctx);
                av_buffer_unref(&g_ps.hw_device_ctx);
                g_ps.vid_ctx->get_format = NULL;
                g_ps.has_va = FALSE;
                if (avcodec_open2(g_ps.vid_ctx, vcodec, NULL) < 0) {
                    plog("Software avcodec_open2 failed for codec %d", (int)vst->codecpar->codec_id);
                    goto cleanup;
                }
            } else {
                plog("avcodec_open2 failed for codec %d", (int)vst->codecpar->codec_id);
                goto cleanup;
            }
        }
    } else {
        g_ps.audio_only = TRUE;
    }

    AVFormatContext *afmt = g_ps.vid_fmt;
    if (g_ps.audio_url && g_ps.audio_url[0]) {
        g_ps.aud_fmt = open_av_input(&g_ps, g_ps.audio_url);
        if (g_ps.aud_fmt) {
            afmt = g_ps.aud_fmt;
            if (g_ps.duration_ms <= 0 && g_ps.aud_fmt->duration != AV_NOPTS_VALUE && g_ps.aud_fmt->duration > 0)
                g_ps.duration_ms = g_ps.aud_fmt->duration / 1000LL;
        }
    }
    g_ps.aud_stream_idx = av_find_best_stream(afmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (g_ps.aud_stream_idx >= 0) {
        AVStream *ast = afmt->streams[g_ps.aud_stream_idx];
        if (g_ps.duration_ms <= 0 && ast->duration != AV_NOPTS_VALUE && ast->duration > 0)
            g_ps.duration_ms = (int64_t)(ast->duration * av_q2d(ast->time_base) * 1000.0);
        const AVCodec *acodec = avcodec_find_decoder(ast->codecpar->codec_id);
        if (acodec) {
            g_ps.aud_ctx = avcodec_alloc_context3(acodec);
            avcodec_parameters_to_context(g_ps.aud_ctx, ast->codecpar);
            if (avcodec_open2(g_ps.aud_ctx, acodec, NULL) >= 0) {
                AVChannelLayout out_ch = AV_CHANNEL_LAYOUT_STEREO;
                if (swr_alloc_set_opts2(&g_ps.swr,
                        &out_ch, AV_SAMPLE_FMT_S16, 48000,
                        &g_ps.aud_ctx->ch_layout, g_ps.aud_ctx->sample_fmt, g_ps.aud_ctx->sample_rate,
                        0, NULL) >= 0 && swr_init(g_ps.swr) >= 0) {
                    g_ps.aring = (uint8_t *)malloc(AUDIO_RING_SIZE);
                    if (g_ps.aring) g_ps.has_audio = TRUE;
                }
            }
        }
    }

    if (g_ps.audio_only && !g_ps.has_audio) {
        plog("No playable video or audio stream found");
        if (!g_ps.quiet) printf("playerintern: No playable video or audio stream found\n");
        goto cleanup;
    }

    if (!open_player_window(&g_ps, g_ps.start_fs)) {
        plog("open_player_window failed");
        goto cleanup;
    }
    plog("Opened window (%dx%d) audio_only=%d has_audio=%d va=%d",
         g_ps.vid_w, g_ps.vid_h, (int)g_ps.audio_only, (int)g_ps.has_audio, (int)g_ps.has_va);
    create_ctrl_bar(&g_ps);
    if (g_ps.audio_only) show_ctrl_bar(&g_ps);

    if (g_ps.has_audio) {
        IDOS->CreateNewProcTags(
            NP_Entry,     (ULONG)audio_task_entry,
            NP_Name,      (ULONG)"VVPlayer_Audio",
            NP_StackSize, 65536,
            NP_Priority,  5,
            TAG_DONE);
    }

    pkt    = av_packet_alloc();
    apkt   = av_packet_alloc();
    vframe = av_frame_alloc();
    aframe = av_frame_alloc();
    if (!pkt || !apkt || !vframe || !aframe) {
        plog("Failed to allocate AVPacket/AVFrame");
        goto cleanup;
    }

    g_ps.start_ms = get_time_ms();

    while (!g_ps.quit && av_read_frame(g_ps.vid_fmt, pkt) >= 0) {
        handle_window_events(&g_ps);
        if (g_ps.quit) { av_packet_unref(pkt); break; }

        if (g_ps.seek_pending) {
            av_packet_unref(pkt);
            perform_seek(&g_ps, vst);
            continue;
        }

        while (g_ps.paused && !g_ps.seek_show_frame && !g_ps.quit) {
            handle_window_events(&g_ps);
            if (g_ps.seek_pending) {
                perform_seek(&g_ps, vst);
                break;
            }
            IDOS->Delay(1);
        }
        if (g_ps.quit) { av_packet_unref(pkt); break; }

        if (g_ps.audio_only && g_ps.has_audio && pkt->stream_index == g_ps.aud_stream_idx) {
            while (!g_ps.quit && !g_ps.seek_pending && aring_avail_read(&g_ps) >= (AUDIO_RING_SIZE / 2)) {
                g_ps.cur_pos_ms = g_ps.clock_offset_ms + (int64_t)(g_ps.audio_bytes_played / 192ULL);
                handle_window_events(&g_ps);
                if (!g_ps.quit && !g_ps.seek_pending) IDOS->Delay(1);
            }
            if (g_ps.quit) { av_packet_unref(pkt); break; }
            if (g_ps.seek_pending) { av_packet_unref(pkt); perform_seek(&g_ps, vst); continue; }
            decode_audio_packet(&g_ps, pkt, aframe);
            g_ps.cur_pos_ms = g_ps.clock_offset_ms + (int64_t)(g_ps.audio_bytes_played / 192ULL);
            g_ps.seek_show_frame = FALSE;
        } else if (!g_ps.aud_fmt && g_ps.has_audio && pkt->stream_index == g_ps.aud_stream_idx) {
            decode_audio_packet(&g_ps, pkt, aframe);
        } else if (!g_ps.audio_only && pkt->stream_index == g_ps.vid_stream_idx) {
            if (g_ps.aud_fmt && g_ps.has_audio) {
                while (!g_ps.quit && !g_ps.seek_pending && aring_avail_read(&g_ps) < (AUDIO_RING_SIZE / 4)) {
                    if (av_read_frame(g_ps.aud_fmt, apkt) < 0) break;
                    if (apkt->stream_index == g_ps.aud_stream_idx)
                        decode_audio_packet(&g_ps, apkt, aframe);
                    av_packet_unref(apkt);
                }
            }

            if (avcodec_send_packet(g_ps.vid_ctx, pkt) >= 0) {
                while (avcodec_receive_frame(g_ps.vid_ctx, vframe) == 0) {
                    handle_window_events(&g_ps);
                    if (g_ps.quit || g_ps.seek_pending) break;

                    int64_t pts = vframe->best_effort_timestamp;
                    int64_t pts_ms = (pts != AV_NOPTS_VALUE)
                        ? (int64_t)(pts * av_q2d(vst->time_base) * 1000.0)
                        : 0;
                    if (g_ps.first_pts_ms < 0) g_ps.first_pts_ms = pts_ms;
                    int64_t rel_ms = pts_ms - g_ps.first_pts_ms;
                    if (rel_ms < 0) rel_ms = 0;
                    if (g_ps.just_seeked) {
                        g_ps.clock_offset_ms = rel_ms;
                        g_ps.audio_bytes_played = 0;
                        g_ps.start_ms = get_time_ms() - (uint64_t)rel_ms;
                        g_ps.just_seeked = FALSE;
                    }
                    g_ps.cur_pos_ms = rel_ms;

                    if (!g_ps.seek_show_frame) {
                        int64_t clock_ms = (g_ps.has_audio && g_ps.audio_bytes_played > 0)
                            ? (g_ps.clock_offset_ms + (int64_t)(g_ps.audio_bytes_played / 192ULL))
                            : (int64_t)(get_time_ms() - g_ps.start_ms);

                        if (g_ps.cur_pos_ms > clock_ms + 5) {
                            int64_t wait_ms = g_ps.cur_pos_ms - clock_ms;
                            if (wait_ms > 200) wait_ms = 200;
                            ULONG ticks = (ULONG)(wait_ms / 20);
                            if (ticks > 0) IDOS->Delay(ticks);
                        }
                    }

                    render_frame(&g_ps, vframe);
                    if (g_ps.seek_show_frame) {
                        g_ps.seek_show_frame = FALSE;
                        break;
                    }
                }
            }
        }
        av_packet_unref(pkt);
    }

    rc = 0;

cleanup:
    av_packet_free(&pkt);
    av_packet_free(&apkt);
    av_frame_free(&vframe);
    av_frame_free(&aframe);
    g_ps.quit = TRUE;
    for (int w = 0; w < 50 && g_ps.audio_running; w++)
        IDOS->Delay(1);

    hide_ctrl_bar(&g_ps);
    if (g_ps.ctrl_obj) { IIntuition->DisposeObject(g_ps.ctrl_obj); g_ps.ctrl_obj = NULL; }
    close_ctrl_classes();
    if (g_ps.sw_bm) { IGraphics->FreeBitMap(g_ps.sw_bm); g_ps.sw_bm = NULL; }
    if (g_ps.win) { IIntuition->CloseWindow(g_ps.win); g_ps.win = NULL; }
    if (g_ps.aring) { free(g_ps.aring); g_ps.aring = NULL; }
    if (g_ps.swr) { swr_free(&g_ps.swr); }
    if (g_ps.aud_ctx) { avcodec_free_context(&g_ps.aud_ctx); }
    if (g_ps.vid_ctx) { avcodec_free_context(&g_ps.vid_ctx); }
    if (g_ps.hw_device_ctx) { av_buffer_unref(&g_ps.hw_device_ctx); }
    if (g_ps.aud_fmt) { avformat_close_input(&g_ps.aud_fmt); }
    if (g_ps.vid_fmt) { avformat_close_input(&g_ps.vid_fmt); }
    cleanup_va(&g_ps);
    avformat_network_deinit();
    plog("playerintern exit %d", rc);
    if (g_log_fp) { fclose(g_log_fp); g_log_fp = NULL; }
    (void)VERSION_TAG;
    (void)STACK_COOKIE;
    return rc;
}
