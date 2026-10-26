#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <gadgets/button.h>
#include <gadgets/space.h>

#include "prefs.h"
#include "app.h"
#include "net.h"
#include "renderer.h"

/* Unified Resolution Presets: One setting for Screen, Window & Web Engine */
const ResPreset g_res_presets[NUM_RES_PRESETS] = {
    { "1024 x 768 (Standard XGA)", 1024, 768 },
    { "1280 x 720 (HD 720p)",      1280, 720 },
    { "1920 x 1080 (Full HD)",     1920, 1080 },
    { "800 x 600 (SVGA)",          800,  600 },
    { "1280 x 1024 (SXGA)",        1280, 1024 },
    { "Automatisch anpassen",      0,    0 },
    { "Benutzerdefiniert...",      0,    0 }
};

/* Search Engine Presets (Default: 0 = DuckDuckGo) */
const SearchEnginePreset g_search_engines[NUM_SEARCH_ENGINES] = {
    { "DuckDuckGo (Standard)", "DuckDuckGo", "https://duckduckgo.com/?q=%s" },
    { "Google",                "Google",     "https://www.google.de/search?q=%s" },
    { "Bing",                  "Bing",       "https://www.bing.com/search?q=%s" },
    { "Startpage",             "Startpage",  "https://www.startpage.com/sp/search?query=%s" },
    { "Ecosia",                "Ecosia",     "https://www.ecosia.org/search?q=%s" },
    { "Wikipedia (DE)",        "Wikipedia",  "https://de.wikipedia.org/w/index.php?search=%s" },
    { "YouTube",               "YouTube",    "https://www.youtube.com/results?search_query=%s" },
    { "Aminet",                "Aminet",     "https://aminet.net/search?query=%s" },
    { "OS4Depot",              "OS4Depot",   "https://os4depot.net/?function=search&ias=1&s=%s" }
};

static void ensure_dir_exists(const char *dir_path)
{
    BPTR lock = IDOS->Lock(dir_path, SHARED_LOCK);
    if (lock) {
        IDOS->UnLock(lock);
        return;
    }
    lock = IDOS->CreateDir(dir_path);
    if (lock) {
        IDOS->UnLock(lock);
    }
}

static void set_quicklink_entry(QuickLinkItem *item, const char *label, const char *url, const char *icon)
{
    if (!item) return;
    strncpy(item->label, label ? label : "", sizeof(item->label) - 1);
    item->label[sizeof(item->label) - 1] = '\0';

    strncpy(item->url, url ? url : "", sizeof(item->url) - 1);
    item->url[sizeof(item->url) - 1] = '\0';

    strncpy(item->icon, icon ? icon : "bookmark", sizeof(item->icon) - 1);
    item->icon[sizeof(item->icon) - 1] = '\0';
}

void prefs_init(VortexApp *app)
{
    if (!app) return;

    memset(&app->prefs, 0, sizeof(VortexPrefs));

    /* 1. Unified Resolution: Default to Odyssey-style Auto-Fit window on Workbench screen */
    app->prefs.resolution.preset_idx        = 5; /* Automatisch anpassen (Odyssey Fenster) */
    app->prefs.resolution.width             = 1024;
    app->prefs.resolution.height            = 768;
    app->prefs.resolution.custom_w          = 1024;
    app->prefs.resolution.custom_h          = 768;
    app->prefs.resolution.screen_mode       = VB_SCREEN_WORKBENCH;
    app->prefs.resolution.use_custom_screen = FALSE;
    app->prefs.resolution.mode_id           = 0;
    app->prefs.resolution.depth             = 24; /* RTG screen depth (16 or 24/32-bit RGB; never 12) */
    app->prefs.resolution.win_x             = 0;
    app->prefs.resolution.win_y             = 0;
    app->prefs.resolution.win_w             = 0;
    app->prefs.resolution.win_h             = 0;
    app->prefs.resolution.has_win_pos       = FALSE;
    app->prefs.soft_scroll                  = TRUE;
    app->prefs.show_status_bar              = FALSE; /* Default: OFF */
    strncpy(app->prefs.start_url, "https://www.amiga-news.de", sizeof(app->prefs.start_url) - 1);
    app->prefs.start_url[sizeof(app->prefs.start_url) - 1] = '\0';
    app->prefs.save_last_url                = TRUE; /* Default: ON (letzte Seite speichern) */

    /* 2. Font & Charset Settings */
    app->prefs.font.gui_charset     = 0; /* ISO-8859-1 (Amiga Standard) */
    app->prefs.font.gui_font_size   = 0; /* System Standard */
    app->prefs.font.web_charset     = 0; /* UTF-8 */
    app->prefs.font.web_zoom_pct    = 100; /* 100% */
    app->prefs.font.web_font_size   = 16;  /* 16px Standard Headless Browser Font Size */
    app->prefs.font.web_font_family = 0; /* Sans-Serif */
    app->prefs.search_engine        = 0; /* DuckDuckGo (Default) */

    /* 3. Odyssey-style Quick Links (with icons & variable count) */
    app->prefs.num_quick_links = 7;
    set_quicklink_entry(&app->prefs.quick_links[0], "Amiga-News", "https://www.amiga-news.de", "amiga-news");
    set_quicklink_entry(&app->prefs.quick_links[1], "OS4Depot",   "https://os4depot.net",      "os4depot");
    set_quicklink_entry(&app->prefs.quick_links[2], "Amigans",    "https://www.amigans.net",   "amigans");
    set_quicklink_entry(&app->prefs.quick_links[3], "Aminet",     "https://aminet.net",        "aminet");
    set_quicklink_entry(&app->prefs.quick_links[4], "YouTube",    "https://www.youtube.com",   "youtube");
    set_quicklink_entry(&app->prefs.quick_links[5], "Twitch",     "https://m.twitch.tv",       "twitch");
    set_quicklink_entry(&app->prefs.quick_links[6], "Wikipedia",  "https://de.wikipedia.org",  "wikipedia");
}

static BOOL load_from_file(VortexApp *app, const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return FALSE;

    int cfg_version = 1;
    int loaded_links = 0;
    char line[640];
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) {
            line[--len] = '\0';
        }
        if (len == 0 || line[0] == '#') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        if (strcmp(key, "version") == 0) {
            cfg_version = atoi(val);
            continue;
        }

        /* Quick-Launch items */
        for (int i = 0; i < VB_MAX_QUICKLINKS; i++) {
            char k_lbl[32], k_url[32], k_ico[32];
            snprintf(k_lbl, sizeof(k_lbl), "quick%d_label", i + 1);
            snprintf(k_url, sizeof(k_url), "quick%d_url",   i + 1);
            snprintf(k_ico, sizeof(k_ico), "quick%d_icon",  i + 1);

            if (strcmp(key, k_lbl) == 0) {
                strncpy(app->prefs.quick_links[i].label, val, sizeof(app->prefs.quick_links[i].label) - 1);
                app->prefs.quick_links[i].label[sizeof(app->prefs.quick_links[i].label) - 1] = '\0';
                if (i + 1 > loaded_links) loaded_links = i + 1;
            } else if (strcmp(key, k_url) == 0) {
                strncpy(app->prefs.quick_links[i].url, val, sizeof(app->prefs.quick_links[i].url) - 1);
                app->prefs.quick_links[i].url[sizeof(app->prefs.quick_links[i].url) - 1] = '\0';
                if (i + 1 > loaded_links) loaded_links = i + 1;
            } else if (strcmp(key, k_ico) == 0) {
                strncpy(app->prefs.quick_links[i].icon, val, sizeof(app->prefs.quick_links[i].icon) - 1);
                app->prefs.quick_links[i].icon[sizeof(app->prefs.quick_links[i].icon) - 1] = '\0';
            }
        }

        if (strcmp(key, "num_quicklinks") == 0) {
            int n = atoi(val);
            if (n >= 0 && n <= VB_MAX_QUICKLINKS) {
                app->prefs.num_quick_links = (uint16_t)n;
            }
        }
        /* Unified Resolution */
        else if (strcmp(key, "res_preset") == 0) {
            int p = atoi(val);
            if (p >= 0 && p < NUM_RES_PRESETS) {
                app->prefs.resolution.preset_idx = (uint16_t)p;
            }
        } else if (strcmp(key, "res_w") == 0 || strcmp(key, "custom_w") == 0) {
            int w = atoi(val);
            if (w >= 320 && w <= 1920) {
                app->prefs.resolution.width    = (uint16_t)w;
                app->prefs.resolution.custom_w = (uint16_t)w;
            }
        } else if (strcmp(key, "res_h") == 0 || strcmp(key, "custom_h") == 0) {
            int h = atoi(val);
            if (h >= 200 && h <= 1080) {
                app->prefs.resolution.height   = (uint16_t)h;
                app->prefs.resolution.custom_h = (uint16_t)h;
            }
        } else if (strcmp(key, "has_win_pos") == 0) {
            app->prefs.resolution.has_win_pos = (atoi(val) != 0);
        } else if (strcmp(key, "win_x") == 0) {
            app->prefs.resolution.win_x = (int16_t)atoi(val);
        } else if (strcmp(key, "win_y") == 0) {
            app->prefs.resolution.win_y = (int16_t)atoi(val);
        } else if (strcmp(key, "win_w") == 0) {
            int ww = atoi(val);
            if (ww >= 400 && ww <= 4096) app->prefs.resolution.win_w = (uint16_t)ww;
        } else if (strcmp(key, "win_h") == 0) {
            int wh = atoi(val);
            if (wh >= 300 && wh <= 4096) app->prefs.resolution.win_h = (uint16_t)wh;
        } else if (strcmp(key, "screen_mode") == 0) {
            app->prefs.resolution.screen_mode = (uint8_t)atoi(val);
            app->prefs.resolution.use_custom_screen = (app->prefs.resolution.screen_mode != VB_SCREEN_WORKBENCH);
        } else if (strcmp(key, "use_custom_screen") == 0) {
            app->prefs.resolution.use_custom_screen = (atoi(val) != 0);
            if (app->prefs.resolution.use_custom_screen && app->prefs.resolution.screen_mode == VB_SCREEN_WORKBENCH) {
                app->prefs.resolution.screen_mode = VB_SCREEN_CUSTOM;
            }
        } else if (strcmp(key, "res_mode_id") == 0) {
            app->prefs.resolution.mode_id = (uint32_t)strtoul(val, NULL, 0);
        } else if (strcmp(key, "res_depth") == 0) {
            int d = atoi(val);
            if (d == 12) {
                /* Heal broken config that forced 1920x1080 12-bit custom screen */
                app->prefs.resolution.preset_idx        = 5;
                app->prefs.resolution.width             = 1024;
                app->prefs.resolution.height            = 768;
                app->prefs.resolution.custom_w          = 1024;
                app->prefs.resolution.custom_h          = 768;
                app->prefs.resolution.screen_mode       = VB_SCREEN_WORKBENCH;
                app->prefs.resolution.use_custom_screen = FALSE;
                app->prefs.resolution.mode_id           = 0;
                d = 24;
            }
            if (d < 16 || d > 32) d = 24;
            app->prefs.resolution.depth = (uint16_t)d;
        } else if (strcmp(key, "soft_scroll") == 0) {
            app->prefs.soft_scroll = (atoi(val) != 0);
        } else if (strcmp(key, "show_status_bar") == 0) {
            app->prefs.show_status_bar = (atoi(val) != 0);
        } else if (strcmp(key, "start_url") == 0) {
            strncpy(app->prefs.start_url, val, sizeof(app->prefs.start_url) - 1);
            app->prefs.start_url[sizeof(app->prefs.start_url) - 1] = '\0';
        } else if (strcmp(key, "save_last_url") == 0) {
            app->prefs.save_last_url = (atoi(val) != 0);
        }
        /* Font & Charset */
        else if (strcmp(key, "gui_charset") == 0) {
            app->prefs.font.gui_charset = (uint8_t)atoi(val);
        } else if (strcmp(key, "gui_font_size") == 0) {
            app->prefs.font.gui_font_size = (uint8_t)atoi(val);
        } else if (strcmp(key, "web_charset") == 0) {
            app->prefs.font.web_charset = (uint8_t)atoi(val);
        } else if (strcmp(key, "web_zoom_pct") == 0) {
            int z = atoi(val);
            if (z >= 50 && z <= 250) app->prefs.font.web_zoom_pct = (uint16_t)z;
        } else if (strcmp(key, "web_font_size") == 0) {
            int fs = atoi(val);
            if (fs >= 10 && fs <= 36) app->prefs.font.web_font_size = (uint16_t)fs;
        } else if (strcmp(key, "web_font_family") == 0) {
            app->prefs.font.web_font_family = (uint8_t)atoi(val);
        } else if (strcmp(key, "search_engine") == 0) {
            int se = atoi(val);
            if (se >= 0 && se < NUM_SEARCH_ENGINES) app->prefs.search_engine = (uint8_t)se;
        }
    }

    if (loaded_links > 0 && app->prefs.num_quick_links == 0) {
        app->prefs.num_quick_links = (uint16_t)loaded_links;
    }

    /* Upgrade pre-v3 configs to Odyssey window layout (below screen titlebar to screen edges) */
    if (cfg_version < 3) {
        app->prefs.resolution.preset_idx  = 5;
        app->prefs.resolution.has_win_pos = FALSE;
    }
    if (app->prefs.resolution.win_w < 400 || app->prefs.resolution.win_h < 300) {
        app->prefs.resolution.has_win_pos = FALSE;
    }

    /* Keep resolution consistent */
    uint16_t p = app->prefs.resolution.preset_idx;
    if (p < 5) {
        app->prefs.resolution.width  = g_res_presets[p].width;
        app->prefs.resolution.height = g_res_presets[p].height;
    } else if (p == 6) {
        if (app->prefs.resolution.custom_w == 0) app->prefs.resolution.custom_w = 1024;
        if (app->prefs.resolution.custom_h == 0) app->prefs.resolution.custom_h = 768;
        app->prefs.resolution.width  = app->prefs.resolution.custom_w;
        app->prefs.resolution.height = app->prefs.resolution.custom_h;
    }

    if (app->prefs.font.web_zoom_pct == 0)  app->prefs.font.web_zoom_pct  = 100;
    if (app->prefs.font.web_font_size == 0) app->prefs.font.web_font_size = 16;
    if (app->prefs.search_engine >= NUM_SEARCH_ENGINES) app->prefs.search_engine = 0;

    fclose(fp);
    return TRUE;
}

BOOL prefs_load(VortexApp *app)
{
    if (!app) return FALSE;

    if (load_from_file(app, VB_PREFS_ENV_FILE)) {
        return TRUE;
    }
    if (load_from_file(app, VB_PREFS_ENVARC_FILE)) {
        return TRUE;
    }
    return FALSE;
}

void prefs_capture_window_box(VortexApp *app)
{
    if (!app || !app->win) return;
    if (app->win->Width >= 400 && app->win->Height >= 300) {
        app->prefs.resolution.win_x       = app->win->LeftEdge;
        app->prefs.resolution.win_y       = app->win->TopEdge;
        app->prefs.resolution.win_w       = app->win->Width;
        app->prefs.resolution.win_h       = app->win->Height;
        app->prefs.resolution.has_win_pos = TRUE;
    }
}

static BOOL save_to_file(const VortexApp *app, const char *path)
{
    FILE *fp = fopen(path, "w");
    if (!fp) return FALSE;

    fprintf(fp, "# VortexBrowse Configuration\n");
    fprintf(fp, "version=3\n\n");

    /* Unified Resolution & Saved Window Geometry */
    fprintf(fp, "# Unified Resolution (Screen, Window & Browser)\n");
    fprintf(fp, "res_preset=%u\n", (unsigned)app->prefs.resolution.preset_idx);
    fprintf(fp, "res_w=%u\n",      (unsigned)app->prefs.resolution.width);
    fprintf(fp, "res_h=%u\n",      (unsigned)app->prefs.resolution.height);
    fprintf(fp, "custom_w=%u\n",   (unsigned)app->prefs.resolution.custom_w);
    fprintf(fp, "custom_h=%u\n",   (unsigned)app->prefs.resolution.custom_h);
    fprintf(fp, "has_win_pos=%d\n", app->prefs.resolution.has_win_pos ? 1 : 0);
    fprintf(fp, "win_x=%d\n",       (int)app->prefs.resolution.win_x);
    fprintf(fp, "win_y=%d\n",       (int)app->prefs.resolution.win_y);
    fprintf(fp, "win_w=%u\n",       (unsigned)app->prefs.resolution.win_w);
    fprintf(fp, "win_h=%u\n",       (unsigned)app->prefs.resolution.win_h);
    fprintf(fp, "screen_mode=%u\n",       (unsigned)app->prefs.resolution.screen_mode);
    fprintf(fp, "use_custom_screen=%d\n", app->prefs.resolution.use_custom_screen ? 1 : 0);
    fprintf(fp, "res_mode_id=0x%08lx\n",  (unsigned long)app->prefs.resolution.mode_id);
    fprintf(fp, "res_depth=%u\n",         (unsigned)app->prefs.resolution.depth);
    fprintf(fp, "soft_scroll=%d\n",       app->prefs.soft_scroll ? 1 : 0);
    fprintf(fp, "show_status_bar=%d\n",   app->prefs.show_status_bar ? 1 : 0);
    fprintf(fp, "start_url=%s\n",         app->prefs.start_url);
    fprintf(fp, "save_last_url=%d\n\n",   app->prefs.save_last_url ? 1 : 0);

    /* Font & Charset */
    fprintf(fp, "# Font & Charset Configuration\n");
    fprintf(fp, "gui_charset=%u\n",     (unsigned)app->prefs.font.gui_charset);
    fprintf(fp, "gui_font_size=%u\n",   (unsigned)app->prefs.font.gui_font_size);
    fprintf(fp, "web_charset=%u\n",     (unsigned)app->prefs.font.web_charset);
    fprintf(fp, "web_zoom_pct=%u\n",    (unsigned)app->prefs.font.web_zoom_pct);
    fprintf(fp, "web_font_size=%u\n",   (unsigned)app->prefs.font.web_font_size);
    fprintf(fp, "web_font_family=%u\n", (unsigned)app->prefs.font.web_font_family);
    fprintf(fp, "search_engine=%u\n\n", (unsigned)app->prefs.search_engine);

    /* Odyssey Quick Links */
    fprintf(fp, "# Odyssey Quick Links\n");
    fprintf(fp, "num_quicklinks=%u\n", (unsigned)app->prefs.num_quick_links);
    for (int i = 0; i < app->prefs.num_quick_links && i < VB_MAX_QUICKLINKS; i++) {
        fprintf(fp, "quick%d_label=%s\n", i + 1, app->prefs.quick_links[i].label);
        fprintf(fp, "quick%d_url=%s\n",   i + 1, app->prefs.quick_links[i].url);
        fprintf(fp, "quick%d_icon=%s\n",  i + 1, app->prefs.quick_links[i].icon);
    }

    fclose(fp);
    return TRUE;
}

BOOL prefs_save(const VortexApp *app, BOOL permanent)
{
    if (!app) return FALSE;

    prefs_capture_window_box((VortexApp *)app);

    ensure_dir_exists(VB_PREFS_ENV_DIR);
    BOOL ok_env = save_to_file(app, VB_PREFS_ENV_FILE);

    BOOL ok_arc = TRUE;
    if (permanent) {
        ensure_dir_exists(VB_PREFS_ENVARC_DIR);
        ok_arc = save_to_file(app, VB_PREFS_ENVARC_FILE);
    }

    return ok_env && ok_arc;
}

void prefs_resize_window_to_resolution(VortexApp *app, uint16_t req_w, uint16_t req_h)
{
    if (!app || !app->win || req_w == 0 || req_h == 0) return;

    /* Do not resize window box if running on custom full-screen */
    if (app->custom_screen && app->win->WScreen == app->custom_screen) {
        return;
    }

    int overhead_w = (int)(app->win->BorderLeft + app->win->BorderRight);
    int overhead_h = (int)(app->win->BorderTop + app->win->BorderBottom);

    if (app->gad_space) {
        struct IBox *area = NULL;
        IIntuition->GetAttr(SPACE_AreaBox, app->gad_space, (uint32 *)&area);
        if (area && area->Width > 0 && area->Height > 0) {
            overhead_w = (int)app->win->Width - (int)area->Width;
            overhead_h = (int)app->win->Height - (int)area->Height;
        } else {
            overhead_h += 95;
            overhead_w += 16;
        }
    }

    int target_w = (int)req_w + overhead_w;
    int target_h = (int)req_h + overhead_h;

    struct Screen *scr = app->win->WScreen;
    if (scr) {
        if (target_w > (int)scr->Width)  target_w = (int)scr->Width;
        if (target_h > (int)scr->Height) target_h = (int)scr->Height;
    }

    int new_left = (int)app->win->LeftEdge;
    int new_top  = (int)app->win->TopEdge;

    if (scr) {
        if (new_left + target_w > (int)scr->Width) {
            new_left = ((int)scr->Width - target_w) / 2;
        }
        if (new_top + target_h > (int)scr->Height) {
            new_top = ((int)scr->Height - target_h) / 2;
        }
        if (new_left < 0) new_left = 0;
        if (new_top < 0)  new_top = 0;
    }

    if (target_w != (int)app->win->Width || target_h != (int)app->win->Height ||
        new_left != (int)app->win->LeftEdge || new_top != (int)app->win->TopEdge) {
        IIntuition->ChangeWindowBox(app->win, (int16)new_left, (int16)new_top, (int16)target_w, (int16)target_h);
        app->prefs.resolution.win_x       = (int16_t)new_left;
        app->prefs.resolution.win_y       = (int16_t)new_top;
        app->prefs.resolution.win_w       = (uint16_t)target_w;
        app->prefs.resolution.win_h       = (uint16_t)target_h;
        app->prefs.resolution.has_win_pos = TRUE;
    }
}

void prefs_apply(VortexApp *app)
{
    if (!app) return;

    /* 1. Determine active unified resolution */
    uint16_t req_w = 1024;
    uint16_t req_h = 768;

    uint16_t idx = app->prefs.resolution.preset_idx;
    if (idx == 6) {
        /* Custom mode */
        req_w = app->prefs.resolution.custom_w ? app->prefs.resolution.custom_w : 1024;
        req_h = app->prefs.resolution.custom_h ? app->prefs.resolution.custom_h : 768;
        app->prefs.resolution.width  = req_w;
        app->prefs.resolution.height = req_h;
    } else if (idx < 5) {
        req_w = g_res_presets[idx].width;
        req_h = g_res_presets[idx].height;
        app->prefs.resolution.width  = req_w;
        app->prefs.resolution.height = req_h;
    }

    /* Synchronize the ACTUAL inner rendering viewport (space.gadget) to the remote browser */
    renderer_sync_viewport_size(app);

    /* 2. Apply Font, Zoom & Charset to Browser Engine */
    if (app->connected) {
        /* Web Zoom */
        uint16_t zoom = app->prefs.font.web_zoom_pct ? app->prefs.font.web_zoom_pct : 100;
        char zcmd[32];
        snprintf(zcmd, sizeof(zcmd), "@ZOOM:%u", (unsigned)zoom);
        net_send_url(app, zcmd);

        /* Web Font Size (Headless Browser base font size in px) */
        uint16_t fsize = app->prefs.font.web_font_size ? app->prefs.font.web_font_size : 16;
        char fscmd[32];
        snprintf(fscmd, sizeof(fscmd), "@FONTSIZE:%u", (unsigned)fsize);
        net_send_url(app, fscmd);

        /* Web Font Family */
        const char *fam = "sans-serif";
        if (app->prefs.font.web_font_family == 1) fam = "serif";
        else if (app->prefs.font.web_font_family == 2) fam = "monospace";
        char fcmd[48];
        snprintf(fcmd, sizeof(fcmd), "@FONT:%s", fam);
        net_send_url(app, fcmd);

        /* Softscrolling */
        char scmd[32];
        snprintf(scmd, sizeof(scmd), "@SOFTSCROLL:%d", app->prefs.soft_scroll ? 1 : 0);
        net_send_url(app, scmd);

        /* Web Charset */
        const char *enc = "utf-8";
        if (app->prefs.font.web_charset == 1) enc = "iso-8859-1";
        else if (app->prefs.font.web_charset == 2) enc = "windows-1252";
        char ccmd[48];
        snprintf(ccmd, sizeof(ccmd), "@CHARSET:%s", enc);
        net_send_url(app, ccmd);
    }
}

void prefs_set_resolution(VortexApp *app, uint16_t preset_idx, uint16_t custom_w, uint16_t custom_h)
{
    if (!app) return;
    app->prefs.resolution.preset_idx = preset_idx;
    if (preset_idx < 5) {
        app->prefs.resolution.width  = g_res_presets[preset_idx].width;
        app->prefs.resolution.height = g_res_presets[preset_idx].height;
        app->prefs.resolution.mode_id = 0;
        if (app->prefs.resolution.use_custom_screen) {
            app->reopen_screen_needed = TRUE;
        } else {
            prefs_resize_window_to_resolution(app, app->prefs.resolution.width, app->prefs.resolution.height);
        }
    } else if (preset_idx == 6) {
        if (custom_w >= 320 && custom_w <= 1920) app->prefs.resolution.custom_w = custom_w;
        if (custom_h >= 200 && custom_h <= 1080) app->prefs.resolution.custom_h = custom_h;
        app->prefs.resolution.width  = app->prefs.resolution.custom_w;
        app->prefs.resolution.height = app->prefs.resolution.custom_h;
        app->prefs.resolution.mode_id = 0;
        if (app->prefs.resolution.use_custom_screen) {
            app->reopen_screen_needed = TRUE;
        } else {
            prefs_resize_window_to_resolution(app, app->prefs.resolution.width, app->prefs.resolution.height);
        }
    }
    prefs_apply(app);
}

BOOL prefs_quicklink_add(VortexApp *app, const char *label, const char *url, const char *icon)
{
    if (!app || !label || label[0] == '\0' || !url || url[0] == '\0') {
        return FALSE;
    }
    if (app->prefs.num_quick_links >= VB_MAX_QUICKLINKS) {
        return FALSE;
    }
    int idx = app->prefs.num_quick_links;
    set_quicklink_entry(&app->prefs.quick_links[idx], label, url, icon);
    app->prefs.num_quick_links++;
    return TRUE;
}

BOOL prefs_quicklink_remove(VortexApp *app, int index)
{
    if (!app || index < 0 || index >= app->prefs.num_quick_links) {
        return FALSE;
    }
    for (int i = index; i < app->prefs.num_quick_links - 1; i++) {
        app->prefs.quick_links[i] = app->prefs.quick_links[i + 1];
    }
    app->prefs.num_quick_links--;
    return TRUE;
}
