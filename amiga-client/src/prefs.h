#ifndef VORTEXBROWSE_PREFS_H
#define VORTEXBROWSE_PREFS_H

#include <stdint.h>
#include <stdbool.h>
#include <exec/types.h>

#define VB_PREFS_ENV_DIR      "ENV:VortexBrowse"
#define VB_PREFS_ENVARC_DIR   "ENVARC:VortexBrowse"
#define VB_PREFS_ENV_FILE     "ENV:VortexBrowse/vortexbrowse.prefs"
#define VB_PREFS_ENVARC_FILE  "ENVARC:VortexBrowse/vortexbrowse.prefs"

#define VB_MAX_QUICKLINKS 24

typedef struct QuickLinkItem {
    char label[32];
    char url[512];
    char icon[32];   /* e.g. "news", "drawer", "video", "globe", "network", "bookmark" */
} QuickLinkItem;

/* Resolution Presets: One unified setting for Screen, Window & Web Viewport */
typedef struct ResPreset {
    const char *name;
    uint16_t    width;
    uint16_t    height;
} ResPreset;

#define NUM_RES_PRESETS 7
extern const ResPreset g_res_presets[NUM_RES_PRESETS];

enum {
    VB_SCREEN_WORKBENCH = 0,   /* Workbench (Fenster-Modus) */
    VB_SCREEN_CUSTOM    = 1,   /* Eigener Public Screen (Manuelle Aufloesung / Vollbild) */
    VB_SCREEN_CLONE_WB  = 2    /* Workbench klonen (Eigener Screen mit Workbench-ScreenMode) */
};

typedef struct ResolutionConfig {
    uint16_t preset_idx;        /* 0..NUM_RES_PRESETS-1 (Default: 5 = Auto-Fit / Odyssey window) */
    uint16_t width;             /* Current active width (1024) */
    uint16_t height;            /* Current active height (768) */
    uint16_t custom_w;          /* If custom: 1024 */
    uint16_t custom_h;          /* If custom: 768 */
    uint8_t  screen_mode;       /* 0 = Workbench, 1 = Eigener Screen, 2 = Workbench klonen */
    BOOL     use_custom_screen; /* FALSE = Workbench, TRUE = Eigener Screen (mode 1 or 2) */
    uint32_t mode_id;           /* AmigaOS DisplayID (from ASL ScreenMode or INVALID_ID) */
    uint16_t depth;             /* 16, 24, or 32 */
    int16_t  win_x;             /* Saved window LeftEdge */
    int16_t  win_y;             /* Saved window TopEdge */
    uint16_t win_w;             /* Saved window outer Width */
    uint16_t win_h;             /* Saved window outer Height */
    BOOL     has_win_pos;       /* TRUE if window position & size were saved */
} ResolutionConfig;

/* Search Engine Presets (Default: 0 = DuckDuckGo) */
typedef struct SearchEnginePreset {
    const char *name;
    const char *short_name;
    const char *query_url_fmt;
} SearchEnginePreset;

#define NUM_SEARCH_ENGINES 9
extern const SearchEnginePreset g_search_engines[NUM_SEARCH_ENGINES];

/* Font & Charset Settings for Program & Web Browser */
typedef struct FontConfig {
    /* GUI / Program Font & Charset */
    uint8_t  gui_charset;       /* 0 = ISO-8859-1 (Amiga Standard), 1 = UTF-8, 2 = Windows-1252, 3 = ISO-8859-15 */
    uint8_t  gui_font_size;     /* 0 = Standard (System), 1 = Klein (8pt), 2 = Mittel (10pt), 3 = Gross (12pt), 4 = Sehr gross (14pt) */

    /* Web Browser Font & Charset */
    uint8_t  web_charset;       /* 0 = UTF-8, 1 = ISO-8859-1, 2 = Windows-1252, 3 = Auto */
    uint16_t web_zoom_pct;      /* 75, 90, 100, 110, 125, 150, 175, 200 (Default: 100) */
    uint16_t web_font_size;     /* 12, 14, 16, 18, 20, 22, 24 px in Headless Browser (Default: 16) */
    uint8_t  web_font_family;   /* 0 = Sans-Serif, 1 = Serif, 2 = Monospace */
} FontConfig;

#define STREAM_RES_NATIVE 0  /* 1:1 Window Size (native) */
#define STREAM_RES_720P   1  /* 1280x720 HD (30-50 FPS) */
#define STREAM_RES_540P   2  /* 960x540 qHD (Turbo) */
#define STREAM_RES_1080P  3  /* 1920x1080 Full HD */

typedef struct VortexPrefs {
    ResolutionConfig resolution;
    FontConfig       font;
    uint8_t          search_engine;     /* 0 = DuckDuckGo (default), 1..NUM_SEARCH_ENGINES-1 */
    BOOL             soft_scroll;       /* TRUE = Softscrolling (default: TRUE) */
    BOOL             show_status_bar;   /* TRUE = Statusleiste anzeigen (default: FALSE) */
    char             start_url[512];    /* Startseite / Letzte aufgerufene Seite */
    BOOL             save_last_url;     /* TRUE = Automatisch letzte Seite als Startseite speichern */
    QuickLinkItem    quick_links[VB_MAX_QUICKLINKS];
    uint16_t         num_quick_links;
    uint8_t          stream_res_mode;   /* STREAM_RES_NATIVE, STREAM_RES_720P, etc. */
    uint8_t          dark_theme;        /* 0 = Hell (Standard), 1 = Dunkel (Dark Mode), 2 = Auto */
} VortexPrefs;

struct VortexApp;

void prefs_init(struct VortexApp *app);
BOOL prefs_load(struct VortexApp *app);
BOOL prefs_save(const struct VortexApp *app, BOOL permanent);
void prefs_capture_window_box(struct VortexApp *app);
void prefs_apply(struct VortexApp *app);
void prefs_resize_window_to_resolution(struct VortexApp *app, uint16_t req_w, uint16_t req_h);

BOOL prefs_quicklink_add(struct VortexApp *app, const char *label, const char *url, const char *icon);
BOOL prefs_quicklink_remove(struct VortexApp *app, int index);
void prefs_set_resolution(struct VortexApp *app, uint16_t preset_idx, uint16_t custom_w, uint16_t custom_h);

#endif /* VORTEXBROWSE_PREFS_H */
