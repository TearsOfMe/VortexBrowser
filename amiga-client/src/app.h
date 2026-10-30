#ifndef VORTEXBROWSE_APP_H
#define VORTEXBROWSE_APP_H

#include <stdint.h>
#include <stdbool.h>

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/classusr.h>
#include <graphics/gfx.h>
#include <dos/dos.h>

#include "../../common/protocol.h"
#include "prefs.h"

#ifdef DEBUG
  #include <stdio.h>
  #define DBG_LOG(...) printf(__VA_ARGS__)
#else
  #define DBG_LOG(...) do { } while (0)
#endif

#define VB_APP_NAME      "VortexBrowser"
#define VB_APP_VERSION   "0.99 RC1"
#define VB_APP_DATE      "(29.10.2026)"
#define VB_APP_TITLE     "VortexBrowser 0.99 RC1"
#define VB_MAX_FAVORITES 64
#ifndef VB_MAX_URL_LEN
#define VB_MAX_URL_LEN   512
#endif
#define VB_MAX_TITLE_LEN 128
#ifndef VB_MAX_TABS
#define VB_MAX_TABS      16
#endif

typedef struct FavoriteItem {
    char title[VB_MAX_TITLE_LEN];
    char url[VB_MAX_URL_LEN];
} FavoriteItem;

typedef struct BrowserTab {
    uint32_t id;
    char     title[64];
    char     url[VB_MAX_URL_LEN];
} BrowserTab;

#define VB_MAX_DOWNLOADS 32

typedef enum {
    DL_STATUS_DOWNLOADING = 0,
    DL_STATUS_COMPLETED,
    DL_STATUS_CANCELLED,
    DL_STATUS_ERROR
} DownloadStatus;

typedef struct DownloadEntry {
    uint32_t       id;
    char           filename[256];
    char           dest_path[512];
    uint64_t       total_size;
    uint64_t       received_size;
    uint32_t       start_seconds;
    uint32_t       last_calc_secs;
    uint64_t       last_calc_bytes;
    uint32_t       speed_bps;
    char           eta_str[32];
    DownloadStatus status;
    BPTR           file_handle;
    struct Node   *lb_node;
} DownloadEntry;

typedef struct DownloadManager {
    DownloadEntry entries[VB_MAX_DOWNLOADS];
    int           count;
} DownloadManager;

typedef struct DownloadState {
    BOOL     active;
    uint32_t download_id;
    uint64_t total_size;
    uint64_t received_size;
    char     filename[256];
    char     dest_path[512];
    BPTR     file_handle;
} DownloadState;

#define VB_AUDIO_SLOTS    8
#define VB_AUDIO_BUF_SIZE 16384

struct MsgPort;
struct AHIRequest;

typedef struct AudioState {
    struct MsgPort    *ahi_mp;
    struct AHIRequest *io[2];                   /* Exactly 2 hardware AHIRequests for double-buffering */
    BOOL               io_active[2];            /* Whether io[0]/io[1] is currently in-flight in AHI */
    int                hw_oldest;               /* Index (0 or 1) of oldest active hardware request */
    int                hw_in_flight;            /* 0, 1, or 2 hardware requests in flight */

    uint8_t           *pcm_buf[VB_AUDIO_SLOTS]; /* 8 software ring-buffer slots */
    uint32_t           pcm_len[VB_AUDIO_SLOTS];
    uint16_t           pcm_rate[VB_AUDIO_SLOTS];
    uint8_t            pcm_ch[VB_AUDIO_SLOTS];
    int                ring_head;               /* Oldest slot (currently playing in hw) */
    int                ring_next_submit;        /* Next slot waiting to be sent to hw */
    int                ring_tail;               /* Next free slot to write network audio into */
    int                ring_count;              /* Total occupied slots in ring (0..8) */
    int                ring_unsubmitted;        /* Slots in ring not yet submitted to hw */

    BOOL               device_open;
    BOOL               muted;
    uint32_t           chunks_played;
} AudioState;

typedef struct RendererState {
    struct BitMap *src_bm[2];      /* Offscreen source BitMaps in VRAM/RTG */
    uint32_t       front_idx;      /* Currently active buffer index (0) */
    uint16_t       src_width;      /* Remote browser viewport width */
    uint16_t       src_height;     /* Remote browser viewport height */
    uint8_t        src_format;     /* FMT_ARGB32 (1) or FMT_RGB565 (2) */
    BOOL           has_frame;      /* TRUE once at least one frame has been decoded */

    struct BitMap *comp_bm;        /* Scaled composition target BitMap */
    uint16_t       comp_width;     /* Current scaled viewport width */
    uint16_t       comp_height;    /* Current scaled viewport height */

    uint8_t       *lz4_comp_buf;   /* Compressed LZ4 payload buffer */
    uint32_t       lz4_comp_cap;   /* Allocated capacity of lz4_comp_buf */

    uint8_t       *raw_frame_buf;  /* Decompressed pixel buffer */
    uint32_t       raw_frame_cap;  /* Allocated capacity of raw_frame_buf */

    uint32_t      *conv_argb_buf;  /* Converted 32-bit ARGB slice buffer for fast LUT upload */
    uint32_t       conv_argb_cap;  /* Capacity in bytes of conv_argb_buf */

    BOOL           has_altivec;    /* TRUE if CPU has active hardware AltiVec/VMX unit */
    BOOL           src_bm_is_16bit;/* TRUE if src_bm[0] is 16-bit PIXF_R5G6B5 */
    BOOL           src_bm_is_yuv;  /* TRUE if src_bm[0] is PIXF_YUV420P */
    BOOL           yuv_hw_disabled;/* TRUE if RTG driver does not support hardware YUV420P CompositeTags */
    BOOL           format_sent;    /* TRUE once initial @FORMAT command has been sent on connection */

    BOOL           dirty;          /* TRUE when new frame/slice awaits screen blit */
    uint16_t       dirty_x1;       /* First dirty column (2D dirty box) */
    uint16_t       dirty_y1;       /* First dirty scanline */
    uint16_t       dirty_x2;       /* Last dirty column (exclusive) */
    uint16_t       dirty_y2;       /* Last dirty scanline (exclusive) */

    uint16_t       reported_w;     /* Last viewport width sent to server */
    uint16_t       reported_h;     /* Last viewport height sent to server */
    uint32_t       last_sync_secs; /* Timestamp of last @RESIZE sync (seconds) */
    uint32_t       last_sync_micros;/* Timestamp of last @RESIZE sync (micros) */
    BOOL           resize_pending; /* TRUE if debounced resize needs to be sent */

    uint32_t       frames_decoded; /* Total frames decoded */
    uint32_t       last_comp_err;  /* Result of last IGraphics->CompositeTags call */
} RendererState;

typedef struct VortexApp {
    /* Network configuration & state */
    char           server_host[128];
    uint16_t       server_port;
    LONG           sock_fd;
    BOOL           connected;
    BOOL           auto_fallback_done; /* Prevent infinite recursion on mutual fallback */
    char           current_url[VB_MAX_URL_LEN];
    char           status_text[256];

    /* Reaction GUI objects */
    Object        *win_obj;
    struct Window *win;
    Object        *gad_url;
    Object        *gad_search_chooser;
    Object        *gad_search;
    Object        *gad_host;
    Object        *gad_server_chooser;
    struct List    server_chooser_list;
    Object        *gad_space;
    Object        *gad_status;
    Object        *gad_gauge;
    Object        *gad_fav_chooser;
    struct List    fav_chooser_list;
    Object        *gad_quick_btn[VB_MAX_QUICKLINKS];
    Object        *gad_quick_del[VB_MAX_QUICKLINKS];
    Object        *gad_quick_add;
    Object        *gad_quick_remove;
    int16_t        active_quicklink_idx;
    char           win_title[512];

    /* Preferences */
    VortexPrefs    prefs;

    /* Public / Custom Screen */
    struct Screen *custom_screen;

    /* Subsystems */
    RendererState   renderer;
    DownloadState   download;
    DownloadManager download_mgr;
    AudioState      audio;

    /* Favorites */
    FavoriteItem   favorites[VB_MAX_FAVORITES];
    int            num_favorites;

    /* Password Store */
    struct PasswordStore *password_store;

    /* Modeless Password Manager Window */
    Object        *pw_win_obj;
    struct Window *pw_win;
    Object        *pw_gad_chooser;
    Object        *pw_gad_domain;
    Object        *pw_gad_user;
    Object        *pw_gad_pass;
    Object        *pw_gad_show_pass;
    struct List    pw_chooser_list;
    int            pw_cur_sel;
    BOOL           pw_show_plaintext;
    char           pw_real_pass[128];

    /* Modeless Download Manager Window (Reaction listbrowser) */
    Object             *dl_win_obj;
    struct Window      *dl_win;
    Object             *dl_gad_listbrowser;
    struct ColumnInfo  *dl_col_info;
    struct List         dl_node_list;
    Object             *dl_gad_status_txt;
    Object             *dl_gad_btn_open_file;
    Object             *dl_gad_btn_open_dir;
    Object             *dl_gad_btn_cancel;
    Object             *dl_gad_btn_remove;
    Object             *dl_gad_btn_clear;
    Object             *dl_gad_btn_close;
    int                 dl_cur_sel;

    /* Mouse throttling & instant wakeup */
    uint32_t       last_move_secs;
    uint32_t       last_move_micros;
    int16_t        last_remote_x;
    int16_t        last_remote_y;
    BOOL           pending_mouse_move;
    int16_t        pending_win_mouse_x;
    int16_t        pending_win_mouse_y;
    struct Task   *main_task;
    int8           mouse_sig_bit;
    uint32         mouse_sig_mask;
    struct IBox    cached_space_box;

    /* Native AmigaOS 4 Context Menu (popupmenu.class) */
    MsgContextMenu ctx_menu;
    BOOL           ctx_menu_pending;
    BOOL           ctx_menu_ready;

    /* Tabs (clicktab.gadget) */
    BrowserTab     tabs[VB_MAX_TABS];
    uint16_t       num_tabs;
    uint16_t       active_tab_idx;
    Object        *gad_clicktab;
    struct List    tab_list;
    Object        *gad_tab_new;
    Object        *gad_tab_close;

    /* URL Bar Favicon */
    Object        *gad_url_favicon;
    char           current_favicon_domain[64];

    /* Link Hovering */
    char           hover_url[VB_MAX_URL_LEN];
    char           hover_title[128];
    BOOL           is_hovering_link;

    /* Intuition Window Context Menus */
    struct Menu   *link_context_menu;
    struct Menu   *page_context_menu;

    /* Automated QEMU test mode (--autotest) */
    BOOL           autotest;
    int            autotest_stage;
    uint32_t       autotest_ticks;
    char           autotest_dl_dir[256];

    BOOL           reopen_gui_needed;    /* Rebuild window without closing screen */
    BOOL           reopen_screen_needed; /* Reopen screen when screen mode/res changes */
    BOOL           running;
} VortexApp;

extern VortexApp g_app;

/* Status bar update helper */
void app_set_status(const char *fmt, ...);
void app_set_gauge(int percent);

#endif /* VORTEXBROWSE_APP_H */

