#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/chooser.h>

#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/page.h>
#include <gadgets/clicktab.h>
#include <gadgets/button.h>
#include <gadgets/string.h>
#include <gadgets/chooser.h>
#include <gadgets/checkbox.h>
#include <gadgets/slider.h>
#include <images/bevel.h>

#include <proto/asl.h>
#include <libraries/asl.h>

#include "gui_prefs.h"
#include "prefs.h"
#include "favorites.h"
#include "net.h"
#include "renderer.h"
#include "gui_download.h"

enum {
    GID_P_TAB = 1,
    GID_P_SAVE,
    GID_P_USE,
    GID_P_CANCEL,

    /* Tab 0: Server & Streaming */
    GID_P_SERVER_PRESET = 5,
    GID_P_SERVER_HOST,
    GID_P_SERVER_PORT,
    GID_P_SERVER_STREAM_RES,
    GID_P_SERVER_JPEG_QUALITY,
    GID_P_SERVER_DYN_VIDEO,
    GID_P_SERVER_SAVE_EXIT,

    /* Tab 1: Schnellwahl (Odyssey) */
    GID_P_QL_CHOOSER = 15,
    GID_P_QL_LBL,
    GID_P_QL_URL,
    GID_P_QL_ICON,
    GID_P_QL_ADD,
    GID_P_QL_UPDATE,
    GID_P_QL_DELETE,
    GID_P_QL_UP,
    GID_P_QL_DOWN,

    /* Tab 2: Bildschirm & Aufloesung */
    GID_P_SCREEN_CHOOSER = 30,
    GID_P_RES_CHOOSER,
    GID_P_RES_W,
    GID_P_RES_H,
    GID_P_SCREEN_MODE_REQ,
    GID_P_SOFTSCROLL,
    GID_P_STATUS_BAR,
    GID_P_THEME_CHOOSER,

    /* Tab 3: Schrift & Zeichensatz */
    GID_P_FONT_GUI_CHARSET = 45,
    GID_P_FONT_GUI_SIZE,
    GID_P_FONT_WEB_CHARSET,
    GID_P_FONT_WEB_SIZE,
    GID_P_FONT_WEB_ZOOM,
    GID_P_FONT_WEB_FAMILY,
    GID_P_SEARCH_ENGINE,

    /* Tab 4: Lesezeichen */
    GID_P_FAV_CHOOSER = 60,
    GID_P_FAV_TITLE,
    GID_P_FAV_URL,
    GID_P_FAV_ADD,
    GID_P_FAV_UPDATE,
    GID_P_FAV_DELETE,

    /* Tab 5: Downloads */
    GID_P_DL_DIR = 90,
    GID_P_DL_DIR_REQ,
    GID_P_DL_ALWAYS_ASK,
    GID_P_DL_OPEN_MGR
};

static const char *const s_tab_labels[] = {
    "Server & Streaming",
    "Schnellwahl (Odyssey)",
    "Bildschirm & Aufloesung",
    "Schrift & Zeichensatz",
    "Lesezeichen",
    "Downloads",
    NULL
};

static const char *const s_server_preset_labels[] = {
    "Host-PC (192.168.178.60:9757)",
    "Raspberry Pi 4 (192.168.178.2:7788)",
    "Benutzerdefiniert (Manuelle Adresse)",
    NULL
};

/* Unified resolution choices */
static const char *const s_res_labels[] = {
    "1024 x 768 (Standard XGA)",
    "1280 x 720 (HD 720p)",
    "1920 x 1080 (Full HD)",
    "800 x 600 (SVGA)",
    "1280 x 1024 (SXGA)",
    "Automatisch anpassen (Fenstergroesse)",
    "Benutzerdefiniert (Eigene B x H)",
    NULL
};

/* Stream Resolution (Server stream resolution scaled by GPU) */
static const char *const s_stream_res_labels[] = {
    "1:1 Fenster-Groesse (Standard / Nativ)",
    "720p HD (1280x720) [30-50 FPS / Video-Boost]",
    "540p qHD (960x540) [Turbo]",
    "1080p Full HD (1920x1080)",
    NULL
};

/* Color Theme (Light, Dark, Auto) */
static const char *const s_theme_labels[] = {
    "Hell (Standard / Light)",
    "Dunkel (Dark Mode)",
    "Automatisch (System)",
    NULL
};

static const char *const s_screen_mode_labels[] = {
    "Workbench (Fenster-Modus)",
    "Eigener Public Screen (Manuelle Aufloesung / Vollbild)",
    "Workbench klonen (Eigener Screen wie Workbench)",
    NULL
};

/* GUI Charset and Font sizes */
static const char *const s_gui_charset_labels[] = {
    "ISO-8859-1 (Westeuropaeisch / Amiga Standard)",
    "UTF-8 (Unicode)",
    "Windows-1252 (ANSI)",
    "ISO-8859-15 (Westeuropa mit Euro)",
    NULL
};

static const char *const s_gui_fontsize_labels[] = {
    "Standard (Workbench-Systemschrift)",
    "Klein (8 pt)",
    "Mittel (10 pt)",
    "Gross (12 pt)",
    "Sehr gross (14 pt)",
    NULL
};

/* Web Charset, Zoom & Font */
static const char *const s_web_charset_labels[] = {
    "UTF-8 (Internationaler Standard)",
    "ISO-8859-1 (Westeuropaeisch)",
    "Windows-1252 (Westeuropa / Windows)",
    "Automatisch erkennen",
    NULL
};

static const char *const s_web_fontsize_labels[] = {
    "16 px (Standard)",
    "18 px (Gross)",
    "20 px (Sehr gross)",
    "22 px (Extra gross)",
    "24 px (Riesig)",
    "14 px (Kompakt)",
    "12 px (Klein)",
    NULL
};

static const uint16 s_web_fontsize_values[] = { 16, 18, 20, 22, 24, 14, 12 };

static const char *const s_web_zoom_labels[] = {
    "100% (Normal)",
    "110% (Leicht vergroessert)",
    "125% (Gross - empfohlen fuer 1024x768 / HD)",
    "150% (Sehr gross)",
    "175% (Extra gross)",
    "200% (Maximal)",
    "90% (Kompakt)",
    "75% (Sehr klein)",
    NULL
};

static const uint16 s_web_zoom_values[] = { 100, 110, 125, 150, 175, 200, 90, 75 };

static const char *const s_web_family_labels[] = {
    "Sans-Serif (Standard / Grotesk)",
    "Serif (Antiqua / Buchschrift)",
    "Monospace (Feste Zeichenbreite)",
    NULL
};

static const char *const s_search_engine_prefs_labels[] = {
    "DuckDuckGo (Standard)",
    "Google",
    "Bing",
    "Startpage",
    "Ecosia",
    "Wikipedia (DE)",
    "YouTube",
    "Aminet",
    "OS4Depot",
    NULL
};

static void get_string_text(Object *gad_obj, char *out_buf, size_t out_cap)
{
    if (!out_buf || out_cap == 0) return;
    out_buf[0] = '\0';
    if (!gad_obj) return;

    STRPTR txt = NULL;
    IIntuition->GetAttr(STRINGA_TextVal, gad_obj, (uint32 *)&txt);
    if (txt) {
        strncpy(out_buf, txt, out_cap - 1);
        out_buf[out_cap - 1] = '\0';
    }
}

static void set_string_text(Object *gad_obj, struct Window *win, const char *text)
{
    if (!gad_obj || !win) return;
    IIntuition->SetGadgetAttrs(
        (struct Gadget *)gad_obj, win, NULL,
        STRINGA_TextVal, (Tag)(text ? text : ""),
        TAG_DONE);
}

static void invoke_screen_mode_requester(struct Window *win, uint32 *io_mode_id, uint16 *io_w, uint16 *io_h, uint16 *io_depth, Object *gad_w, Object *gad_h, Object *gad_res_chooser)
{
    if (!IAsl) return;

    struct ScreenModeRequester *sm_req = (struct ScreenModeRequester *)IAsl->AllocAslRequestTags(
        ASL_ScreenModeRequest,
        ASLSM_TitleText,            (Tag)"VortexBrowser Bildschirm-Modus waehlen",
        ASLSM_Window,               (Tag)win,
        ASLSM_SleepWindow,          TRUE,
        ASLSM_InitialDisplayID,     (*io_mode_id ? *io_mode_id : INVALID_ID),
        ASLSM_InitialDisplayWidth,  (uint32)*io_w,
        ASLSM_InitialDisplayHeight, (uint32)*io_h,
        ASLSM_InitialDisplayDepth,  (*io_depth ? (uint32)*io_depth : 24),
        ASLSM_MinDepth,             16,
        ASLSM_MaxDepth,             24,
        ASLSM_PropertyFlags,        DIPF_IS_RTG,
        ASLSM_PropertyMask,         DIPF_IS_RTG,
        ASLSM_DoWidth,              TRUE,
        ASLSM_DoHeight,             TRUE,
        ASLSM_DoDepth,              TRUE,
        TAG_DONE);

    if (sm_req) {
        if (IAsl->AslRequest(sm_req, NULL)) {
            *io_mode_id = sm_req->sm_DisplayID;
            *io_w       = (uint16)sm_req->sm_DisplayWidth;
            *io_h       = (uint16)sm_req->sm_DisplayHeight;
            *io_depth   = sm_req->sm_DisplayDepth;
            char bw[16], bh[16];
            snprintf(bw, sizeof(bw), "%u", (unsigned)*io_w);
            snprintf(bh, sizeof(bh), "%u", (unsigned)*io_h);
            set_string_text(gad_w, win, bw);
            set_string_text(gad_h, win, bh);
            if (gad_res_chooser && win) {
                IIntuition->SetGadgetAttrs((struct Gadget *)gad_res_chooser, win, NULL,
                                           CHOOSER_Selected, 6,
                                           TAG_DONE);
            }
        }
        IAsl->FreeAslRequest(sm_req);
    }
}

/* Helpers for Schnellwahl Chooser List */
static void free_chooser_list(struct List *list)
{
    if (!IChooser || !list) return;
    struct Node *node = IExec->RemHead(list);
    while (node) {
        IChooser->FreeChooserNode(node);
        node = IExec->RemHead(list);
    }
}

static void rebuild_ql_list(QuickLinkItem *items, int num_items, struct List *list, Object *chooser_obj, struct Window *win, uint16 sel_idx)
{
    if (!IChooser || !list) return;

    if (chooser_obj && win) {
        IIntuition->SetGadgetAttrs((struct Gadget *)chooser_obj, win, NULL,
                                   CHOOSER_Labels, ~0,
                                   TAG_DONE);
    }

    free_chooser_list(list);

    for (int i = 0; i < num_items; i++) {
        char entry_txt[80];
        snprintf(entry_txt, sizeof(entry_txt), "%d: %s (%s)", i + 1, items[i].label, items[i].url);
        struct Node *node = IChooser->AllocChooserNode(
            CNA_CopyText, TRUE,
            CNA_Text,     entry_txt,
            CNA_UserData, (APTR)(uintptr_t)i,
            TAG_DONE);
        if (node) {
            IExec->AddTail(list, node);
        }
    }

    if (chooser_obj && win) {
        uint16 sel = (sel_idx < num_items) ? sel_idx : (num_items > 0 ? num_items - 1 : 0);
        IIntuition->SetGadgetAttrs((struct Gadget *)chooser_obj, win, NULL,
                                   CHOOSER_Labels,   list,
                                   CHOOSER_Selected, sel,
                                   TAG_DONE);
    }
}

static void rebuild_fav_list(VortexApp *app, struct List *list, Object *chooser_obj, struct Window *win, uint16 selected_idx)
{
    if (!IChooser || !list) return;

    if (chooser_obj && win) {
        IIntuition->SetGadgetAttrs((struct Gadget *)chooser_obj, win, NULL,
                                   CHOOSER_Labels, ~0,
                                   TAG_DONE);
    }

    free_chooser_list(list);

    for (int i = 0; i < app->num_favorites; i++) {
        struct Node *node = IChooser->AllocChooserNode(
            CNA_CopyText, TRUE,
            CNA_Text,     app->favorites[i].title,
            CNA_UserData, (APTR)(uintptr_t)i,
            TAG_DONE);
        if (node) {
            IExec->AddTail(list, node);
        }
    }

    if (chooser_obj && win) {
        uint16 sel = (selected_idx < app->num_favorites) ? selected_idx : (app->num_favorites > 0 ? app->num_favorites - 1 : 0);
        IIntuition->SetGadgetAttrs((struct Gadget *)chooser_obj, win, NULL,
                                   CHOOSER_Labels,   list,
                                   CHOOSER_Selected, sel,
                                   TAG_DONE);
    }
}

void gui_prefs_open(VortexApp *app)
{
    if (!app) return;

    /* Local copy of quick links for non-destructive editing */
    QuickLinkItem temp_ql[VB_MAX_QUICKLINKS];
    int temp_num_ql = app->prefs.num_quick_links;
    for (int i = 0; i < temp_num_ql && i < VB_MAX_QUICKLINKS; i++) {
        temp_ql[i] = app->prefs.quick_links[i];
    }

    uint32 temp_res_mode_id = app->prefs.resolution.mode_id;
    uint16 temp_w           = app->prefs.resolution.width;
    uint16 temp_h           = app->prefs.resolution.height;
    uint16 temp_depth       = app->prefs.resolution.depth ? app->prefs.resolution.depth : 24;

    /* ------------------------------------------------------------------------
     * Tab 0: Server & Streaming
     * ------------------------------------------------------------------------ */
    const char *cur_host = app->server_host[0] ? app->server_host :
                          (app->prefs.server_host[0] ? app->prefs.server_host : VORTEXBROWSE_HOSTPC_HOST);
    uint16 cur_port = app->server_port ? app->server_port :
                     (app->prefs.server_port ? app->prefs.server_port : VORTEXBROWSE_HOSTPC_PORT);

    uint16 init_srv_preset = 2; /* Custom */
    if (strcmp(cur_host, VORTEXBROWSE_HOSTPC_HOST) == 0 && cur_port == VORTEXBROWSE_HOSTPC_PORT) {
        init_srv_preset = 0;
    } else if (strcmp(cur_host, VORTEXBROWSE_RPI_HOST) == 0 && cur_port == VORTEXBROWSE_RPI_PORT) {
        init_srv_preset = 1;
    }

    Object *gad_srv_preset = IIntuition->NewObject(NULL, "chooser.gadget",
        GA_ID,              GID_P_SERVER_PRESET,
        GA_RelVerify,       TRUE,
        CHOOSER_LabelArray, s_server_preset_labels,
        CHOOSER_Selected,   init_srv_preset,
        GA_HintInfo,        "Vorkonfigurierten Server waehlen oder manuell anpassen",
        TAG_DONE);

    char host_str[128];
    char port_str[16];
    snprintf(host_str, sizeof(host_str), "%s", cur_host);
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)cur_port);

    Object *gad_srv_host = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID,            GID_P_SERVER_HOST,
        GA_RelVerify,     TRUE,
        GA_TabCycle,      TRUE,
        STRINGA_MaxChars, sizeof(host_str) - 1,
        STRINGA_TextVal,  (Tag)host_str,
        GA_HintInfo,      "IP-Adresse oder Hostname des Streaming-Servers",
        TAG_DONE);

    Object *gad_srv_port = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID,            GID_P_SERVER_PORT,
        GA_RelVerify,     TRUE,
        GA_TabCycle,      TRUE,
        STRINGA_MaxChars, sizeof(port_str) - 1,
        STRINGA_TextVal,  (Tag)port_str,
        GA_HintInfo,      "TCP-Port des Streaming-Servers (z.B. 9757 oder 7788)",
        TAG_DONE);

    uint16 init_stream_sel = app->prefs.stream_res_mode;
    if (init_stream_sel > 3) init_stream_sel = 0;
    Object *gad_stream_res_chooser = IIntuition->NewObject(NULL, "chooser.gadget",
        GA_ID,              GID_P_SERVER_STREAM_RES,
        GA_RelVerify,       TRUE,
        CHOOSER_LabelArray, s_stream_res_labels,
        CHOOSER_Selected,   init_stream_sel,
        GA_HintInfo,        "Server-Aufloesung waehlen (wird per GPU/Composite skaliert)",
        TAG_DONE);

    uint32 cur_q = app->prefs.jpeg_quality ? app->prefs.jpeg_quality : 100;
    if (cur_q < 20 || cur_q > 100) cur_q = 100;

    Object *gad_jpeg_quality = IIntuition->NewObject(NULL, "slider.gadget",
        GA_ID,              GID_P_SERVER_JPEG_QUALITY,
        GA_RelVerify,       TRUE,
        SLIDER_Min,         20,
        SLIDER_Max,         100,
        SLIDER_Level,       cur_q,
        SLIDER_Orientation, SLIDER_HORIZONTAL,
        SLIDER_Ticks,       9,
        SLIDER_ShortTicks,  TRUE,
        SLIDER_LevelFormat, (Tag)"%ld %%",
        SLIDER_LevelPlace,  PLACETEXT_RIGHT,
        SLIDER_LevelDomain, (Tag)"100 %",
        GA_HintInfo,        "JPEG-Stream-Qualitaet (Standard: 100%% = maximale Bildschaerfe)",
        TAG_DONE);

    Object *gad_dyn_video = IIntuition->NewObject(NULL, "checkbox.gadget",
        GA_ID,            GID_P_SERVER_DYN_VIDEO,
        GA_RelVerify,     TRUE,
        GA_Text,          (Tag)"Dynamische JPEG-Qualitaet bei Video (Video-Boost)",
        CHECKBOX_Checked, app->prefs.dynamic_video_quality,
        GA_HintInfo,      "Bei Video-Wiedergabe Qualitaet temporaer auf 60%% fuer 30-50 FPS anpassen",
        TAG_DONE);

    Object *gad_save_exit = IIntuition->NewObject(NULL, "checkbox.gadget",
        GA_ID,            GID_P_SERVER_SAVE_EXIT,
        GA_RelVerify,     TRUE,
        GA_Text,          (Tag)"Aktuellen Server beim Beenden automatisch speichern",
        CHECKBOX_Checked, app->prefs.save_server_on_exit,
        GA_HintInfo,      "Zuletzt genutzten Server fuer den naechsten Start merken",
        TAG_DONE);

    Object *server_vlayout = IIntuition->NewObject(NULL, "layout.gadget",
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,    BVS_GROUP,
        LAYOUT_Label,         (Tag)"Streaming Server & Verbindungsoptionen",
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_DeferLayout,   TRUE,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"Server-Auswahl:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_RIGHT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  28,
                                  LAYOUT_AddChild,      gad_srv_preset,
                                  CHILD_WeightedWidth,  72,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"Server IP / Host:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_RIGHT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  28,
                                  LAYOUT_AddChild,      gad_srv_host,
                                  CHILD_WeightedWidth,  44,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"Port:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_RIGHT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  12,
                                  LAYOUT_AddChild,      gad_srv_port,
                                  CHILD_WeightedWidth,  16,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"Stream-Aufloesung:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_RIGHT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  28,
                                  LAYOUT_AddChild,      gad_stream_res_chooser,
                                  CHILD_WeightedWidth,  72,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"JPEG-Qualitaet:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_RIGHT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  28,
                                  LAYOUT_AddChild,      gad_jpeg_quality,
                                  CHILD_WeightedWidth,  72,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"Video-Optimierung:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_RIGHT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  28,
                                  LAYOUT_AddChild,      gad_dyn_video,
                                  CHILD_WeightedWidth,  72,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"Beim Beenden:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_RIGHT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  28,
                                  LAYOUT_AddChild,      gad_save_exit,
                                  CHILD_WeightedWidth,  72,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                  GA_Text,              (Tag)"Standard-Qualitaet ist 100% (Verlustfrei). 720p/540p erhoeht FPS deutlich.",
                                  GA_ReadOnly,          TRUE,
                                  BUTTON_Justification, BCJ_LEFT,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        TAG_DONE);

    /* ------------------------------------------------------------------------
     * Tab 1: Schnellwahl-Buttons (Odyssey-Leiste)
     * ------------------------------------------------------------------------ */
    struct List ql_list;
    IExec->NewList(&ql_list);

    Object *gad_ql_chooser = IIntuition->NewObject(NULL, "chooser.gadget",
        GA_ID,            GID_P_QL_CHOOSER,
        GA_RelVerify,     TRUE,
        CHOOSER_Labels,   &ql_list,
        CHOOSER_Selected, 0,
        TAG_DONE);

    const char *init_ql_lbl  = (temp_num_ql > 0) ? temp_ql[0].label : "";
    const char *init_ql_url  = (temp_num_ql > 0) ? temp_ql[0].url   : "";
    const char *init_ql_icon = (temp_num_ql > 0) ? temp_ql[0].icon  : "bookmark";

    Object *gad_ql_lbl = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID,            GID_P_QL_LBL,
        GA_RelVerify,     TRUE,
        GA_TabCycle,      TRUE,
        STRINGA_MaxChars, 31,
        STRINGA_TextVal,  (Tag)init_ql_lbl,
        TAG_DONE);

    Object *gad_ql_url = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID,            GID_P_QL_URL,
        GA_RelVerify,     TRUE,
        GA_TabCycle,      TRUE,
        STRINGA_MaxChars, 511,
        STRINGA_TextVal,  (Tag)init_ql_url,
        TAG_DONE);

    Object *gad_ql_icon = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID,            GID_P_QL_ICON,
        GA_RelVerify,     TRUE,
        GA_TabCycle,      TRUE,
        STRINGA_MaxChars, 31,
        STRINGA_TextVal,  (Tag)init_ql_icon,
        TAG_DONE);

    Object *btn_ql_add = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        GID_P_QL_ADD,
        GA_RelVerify, TRUE,
        GA_Text,      "Neu / Hinzufuegen",
        TAG_DONE);

    Object *btn_ql_update = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        GID_P_QL_UPDATE,
        GA_RelVerify, TRUE,
        GA_Text,      "Aktualisieren",
        TAG_DONE);

    Object *btn_ql_delete = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        GID_P_QL_DELETE,
        GA_RelVerify, TRUE,
        GA_Text,      "Entfernen",
        TAG_DONE);

    Object *btn_ql_up = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        GID_P_QL_UP,
        GA_RelVerify, TRUE,
        GA_Text,      "Nach oben",
        TAG_DONE);

    Object *btn_ql_down = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        GID_P_QL_DOWN,
        GA_RelVerify, TRUE,
        GA_Text,      "Nach unten",
        TAG_DONE);

    Object *ql_vlayout = IIntuition->NewObject(NULL, "layout.gadget",
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,    BVS_GROUP,
        LAYOUT_Label,         (Tag)"Odyssey-Schnellwahl-Leiste (Variable Buttons mit Icons)",
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_DeferLayout,   TRUE,
        LAYOUT_AddChild,      gad_ql_chooser,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"Titel / Name:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_RIGHT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  20,
                                  LAYOUT_AddChild,      gad_ql_lbl,
                                  CHILD_WeightedWidth,  80,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"Adresse (URL):",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_RIGHT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  20,
                                  LAYOUT_AddChild,      gad_ql_url,
                                  CHILD_WeightedWidth,  80,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"AISS Icon-Name:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_RIGHT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  20,
                                  LAYOUT_AddChild,      gad_ql_icon,
                                  CHILD_WeightedWidth,  80,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      btn_ql_update,
                                  CHILD_WeightedWidth,  25,
                                  LAYOUT_AddChild,      btn_ql_add,
                                  CHILD_WeightedWidth,  25,
                                  LAYOUT_AddChild,      btn_ql_delete,
                                  CHILD_WeightedWidth,  20,
                                  LAYOUT_AddChild,      btn_ql_up,
                                  CHILD_WeightedWidth,  15,
                                  LAYOUT_AddChild,      btn_ql_down,
                                  CHILD_WeightedWidth,  15,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                  GA_Text,              (Tag)"Tipp: Buttons in der Leiste haben Icons und erweitern sich automatisch.",
                                  GA_ReadOnly,          TRUE,
                                  BUTTON_Justification, BCJ_LEFT,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                  GA_Text,              (Tag)"Neue Buttons koennen auch direkt mit dem [+] Button in der Toolbar hinzugefuegt werden.",
                                  GA_ReadOnly,          TRUE,
                                  BUTTON_Justification, BCJ_LEFT,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        TAG_DONE);

    /* ------------------------------------------------------------------------
     * Tab 1: Einheitliche Bildschirm- & Browser-Aufloesung
     * ------------------------------------------------------------------------ */
    uint16 init_res_sel = app->prefs.resolution.preset_idx;
    if (init_res_sel >= NUM_RES_PRESETS) init_res_sel = 0;

    Object *gad_res_chooser = IIntuition->NewObject(NULL, "chooser.gadget",
        GA_ID,              GID_P_RES_CHOOSER,
        GA_RelVerify,       TRUE,
        CHOOSER_LabelArray, s_res_labels,
        CHOOSER_Selected,   init_res_sel,
        TAG_DONE);

    char custom_w_str[16];
    char custom_h_str[16];
    snprintf(custom_w_str, sizeof(custom_w_str), "%u", (unsigned)app->prefs.resolution.custom_w);
    snprintf(custom_h_str, sizeof(custom_h_str), "%u", (unsigned)app->prefs.resolution.custom_h);

    Object *gad_res_w = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID,            GID_P_RES_W,
        GA_RelVerify,     TRUE,
        GA_TabCycle,      TRUE,
        STRINGA_MaxChars, 6,
        STRINGA_TextVal,  (Tag)custom_w_str,
        TAG_DONE);

    Object *gad_res_h = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID,            GID_P_RES_H,
        GA_RelVerify,     TRUE,
        GA_TabCycle,      TRUE,
        STRINGA_MaxChars, 6,
        STRINGA_TextVal,  (Tag)custom_h_str,
        TAG_DONE);

    uint16 init_screen_sel = app->prefs.resolution.screen_mode;
    if (init_screen_sel > 2) init_screen_sel = app->prefs.resolution.use_custom_screen ? 1 : 0;
    Object *gad_screen_chooser = IIntuition->NewObject(NULL, "chooser.gadget",
        GA_ID,              GID_P_SCREEN_CHOOSER,
        GA_RelVerify,       TRUE,
        CHOOSER_LabelArray, s_screen_mode_labels,
        CHOOSER_Selected,   init_screen_sel,
        TAG_DONE);

    Object *btn_screen_mode_req = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        GID_P_SCREEN_MODE_REQ,
        GA_RelVerify, TRUE,
        GA_Text,      (Tag)"Bildschirm-Modus waehlen (ASL)...",
        TAG_DONE);

    Object *gad_softscroll = IIntuition->NewObject(NULL, "checkbox.gadget",
        GA_ID,            GID_P_SOFTSCROLL,
        GA_RelVerify,     TRUE,
        GA_Text,          (Tag)"Weiches Scrollen (Softscrolling)",
        CHECKBOX_Checked, app->prefs.soft_scroll,
        TAG_DONE);

    Object *gad_show_status = IIntuition->NewObject(NULL, "checkbox.gadget",
        GA_ID,            GID_P_STATUS_BAR,
        GA_RelVerify,     TRUE,
        GA_Text,          (Tag)"Statusleiste anzeigen (unten)",
        CHECKBOX_Checked, app->prefs.show_status_bar,
        TAG_DONE);

    uint16 init_theme_sel = app->prefs.dark_theme;
    if (init_theme_sel > 2) init_theme_sel = 0;
    Object *gad_theme_chooser = IIntuition->NewObject(NULL, "chooser.gadget",
        GA_ID,              GID_P_THEME_CHOOSER,
        GA_RelVerify,       TRUE,
        CHOOSER_LabelArray, s_theme_labels,
        CHOOSER_Selected,   init_theme_sel,
        TAG_DONE);

    Object *res_vlayout = IIntuition->NewObject(NULL, "layout.gadget",
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,    BVS_GROUP,
        LAYOUT_Label,         (Tag)"Zentrale Aufloesung & Bildschirm-Modus",
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_DeferLayout,   TRUE,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                  GA_Text,              (Tag)"Eine einzige Aufloesung fuer Bildschirm, Amiga-Fenster und Webbrowser:",
                                  GA_ReadOnly,          TRUE,
                                  BUTTON_Justification, BCJ_LEFT,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"Bildschirm-Modus:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_LEFT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  30,
                                  LAYOUT_AddChild,      gad_screen_chooser,
                                  CHILD_WeightedWidth,  70,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"RTG ScreenMode:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_LEFT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  30,
                                  LAYOUT_AddChild,      btn_screen_mode_req,
                                  CHILD_WeightedWidth,  70,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"Aufloesung:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_LEFT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  30,
                                  LAYOUT_AddChild,      gad_res_chooser,
                                  CHILD_WeightedWidth,  70,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"Benutzerdefiniert B:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_RIGHT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  25,
                                  LAYOUT_AddChild,      gad_res_w,
                                  CHILD_WeightedWidth,  25,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"H:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_RIGHT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  15,
                                  LAYOUT_AddChild,      gad_res_h,
                                  CHILD_WeightedWidth,  25,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"Mausrad & Scrollen:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_LEFT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  30,
                                  LAYOUT_AddChild,      gad_softscroll,
                                  CHILD_WeightedWidth,  70,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"Statusleiste:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_LEFT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  30,
                                  LAYOUT_AddChild,      gad_show_status,
                                  CHILD_WeightedWidth,  70,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"Farbschema (Theme):",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_LEFT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  30,
                                  LAYOUT_AddChild,      gad_theme_chooser,
                                  CHILD_WeightedWidth,  70,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                  GA_Text,              (Tag)"Hinweis: Umschalten zwischen Screen und Workbench auch mit Amiga+M moeglich.",
                                  GA_ReadOnly,          TRUE,
                                  BUTTON_Justification, BCJ_LEFT,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        TAG_DONE);

    /* ------------------------------------------------------------------------
     * Tab 2: Schriftart & Zeichensatz
     * ------------------------------------------------------------------------ */
    uint16 init_gui_charset = app->prefs.font.gui_charset;
    if (init_gui_charset > 3) init_gui_charset = 0;

    Object *gad_gui_charset = IIntuition->NewObject(NULL, "chooser.gadget",
        GA_ID,              GID_P_FONT_GUI_CHARSET,
        GA_RelVerify,       TRUE,
        CHOOSER_LabelArray, s_gui_charset_labels,
        CHOOSER_Selected,   init_gui_charset,
        TAG_DONE);

    uint16 init_gui_size = app->prefs.font.gui_font_size;
    if (init_gui_size > 4) init_gui_size = 0;

    Object *gad_gui_size = IIntuition->NewObject(NULL, "chooser.gadget",
        GA_ID,              GID_P_FONT_GUI_SIZE,
        GA_RelVerify,       TRUE,
        CHOOSER_LabelArray, s_gui_fontsize_labels,
        CHOOSER_Selected,   init_gui_size,
        TAG_DONE);

    uint16 init_web_charset = app->prefs.font.web_charset;
    if (init_web_charset > 3) init_web_charset = 0;

    Object *gad_web_charset = IIntuition->NewObject(NULL, "chooser.gadget",
        GA_ID,              GID_P_FONT_WEB_CHARSET,
        GA_RelVerify,       TRUE,
        CHOOSER_LabelArray, s_web_charset_labels,
        CHOOSER_Selected,   init_web_charset,
        TAG_DONE);

    uint16 init_web_fsize = 0;
    uint16 cur_fs = app->prefs.font.web_font_size ? app->prefs.font.web_font_size : 16;
    for (int fsi = 0; fsi < 7; fsi++) {
        if (s_web_fontsize_values[fsi] == cur_fs) {
            init_web_fsize = fsi;
            break;
        }
    }

    Object *gad_web_fsize = IIntuition->NewObject(NULL, "chooser.gadget",
        GA_ID,              GID_P_FONT_WEB_SIZE,
        GA_RelVerify,       TRUE,
        CHOOSER_LabelArray, s_web_fontsize_labels,
        CHOOSER_Selected,   init_web_fsize,
        TAG_DONE);

    uint16 init_web_zoom = 0;
    for (int zi = 0; zi < 8; zi++) {
        if (s_web_zoom_values[zi] == app->prefs.font.web_zoom_pct) {
            init_web_zoom = zi;
            break;
        }
    }

    Object *gad_web_zoom = IIntuition->NewObject(NULL, "chooser.gadget",
        GA_ID,              GID_P_FONT_WEB_ZOOM,
        GA_RelVerify,       TRUE,
        CHOOSER_LabelArray, s_web_zoom_labels,
        CHOOSER_Selected,   init_web_zoom,
        TAG_DONE);

    uint16 init_web_fam = app->prefs.font.web_font_family;
    if (init_web_fam > 2) init_web_fam = 0;

    Object *gad_web_fam = IIntuition->NewObject(NULL, "chooser.gadget",
        GA_ID,              GID_P_FONT_WEB_FAMILY,
        GA_RelVerify,       TRUE,
        CHOOSER_LabelArray, s_web_family_labels,
        CHOOSER_Selected,   init_web_fam,
        TAG_DONE);

    uint16 init_se_prefs = app->prefs.search_engine;
    if (init_se_prefs >= NUM_SEARCH_ENGINES) init_se_prefs = 0;

    Object *gad_search_engine = IIntuition->NewObject(NULL, "chooser.gadget",
        GA_ID,              GID_P_SEARCH_ENGINE,
        GA_RelVerify,       TRUE,
        CHOOSER_LabelArray, s_search_engine_prefs_labels,
        CHOOSER_Selected,   init_se_prefs,
        TAG_DONE);

    Object *font_vlayout = IIntuition->NewObject(NULL, "layout.gadget",
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,    BVS_GROUP,
        LAYOUT_Label,         (Tag)"Schriftart, Zeichensatz & Suchmaschine",
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_DeferLayout,   TRUE,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                  GA_Text,              (Tag)"Headless Webbrowser (Remote Rendering & Webseiten):",
                                  GA_ReadOnly,          TRUE,
                                  BUTTON_Justification, BCJ_LEFT,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"Browser-Schriftgroesse:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_RIGHT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  32,
                                  LAYOUT_AddChild,      gad_web_fsize,
                                  CHILD_WeightedWidth,  68,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"Web-Zoom & Skalierung:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_RIGHT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  32,
                                  LAYOUT_AddChild,      gad_web_zoom,
                                  CHILD_WeightedWidth,  68,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"Web-Schriftfamilie:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_RIGHT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  32,
                                  LAYOUT_AddChild,      gad_web_fam,
                                  CHILD_WeightedWidth,  68,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"Web-Zeichensatz:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_RIGHT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  32,
                                  LAYOUT_AddChild,      gad_web_charset,
                                  CHILD_WeightedWidth,  68,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"Standard-Suchmaschine:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_RIGHT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  32,
                                  LAYOUT_AddChild,      gad_search_engine,
                                  CHILD_WeightedWidth,  68,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                  GA_Text,              (Tag)"Programm / AmigaOS-Oberflaeche (GUI):",
                                  GA_ReadOnly,          TRUE,
                                  BUTTON_Justification, BCJ_LEFT,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"GUI-Zeichensatz:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_RIGHT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  32,
                                  LAYOUT_AddChild,      gad_gui_charset,
                                  CHILD_WeightedWidth,  68,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"GUI-Schriftgroesse:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_RIGHT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  32,
                                  LAYOUT_AddChild,      gad_gui_size,
                                  CHILD_WeightedWidth,  68,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        TAG_DONE);

    /* ------------------------------------------------------------------------
     * Tab 3: Lesezeichen-Verwaltung
     * ------------------------------------------------------------------------ */
    struct List fav_list;
    IExec->NewList(&fav_list);

    Object *gad_fav_chooser = IIntuition->NewObject(NULL, "chooser.gadget",
        GA_ID,            GID_P_FAV_CHOOSER,
        GA_RelVerify,     TRUE,
        CHOOSER_Labels,   &fav_list,
        CHOOSER_Selected, 0,
        TAG_DONE);

    const char *init_fav_title = (app->num_favorites > 0) ? app->favorites[0].title : "";
    const char *init_fav_url   = (app->num_favorites > 0) ? app->favorites[0].url   : "";

    Object *gad_fav_title = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID,            GID_P_FAV_TITLE,
        GA_RelVerify,     TRUE,
        GA_TabCycle,      TRUE,
        STRINGA_MaxChars, VB_MAX_TITLE_LEN - 1,
        STRINGA_TextVal,  (Tag)init_fav_title,
        TAG_DONE);

    Object *gad_fav_url = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID,            GID_P_FAV_URL,
        GA_RelVerify,     TRUE,
        GA_TabCycle,      TRUE,
        STRINGA_MaxChars, VB_MAX_URL_LEN - 1,
        STRINGA_TextVal,  (Tag)init_fav_url,
        TAG_DONE);

    Object *btn_fav_add = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        GID_P_FAV_ADD,
        GA_RelVerify, TRUE,
        GA_Text,      "Hinzufuegen",
        TAG_DONE);

    Object *btn_fav_update = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        GID_P_FAV_UPDATE,
        GA_RelVerify, TRUE,
        GA_Text,      "Aktualisieren",
        TAG_DONE);

    Object *btn_fav_delete = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        GID_P_FAV_DELETE,
        GA_RelVerify, TRUE,
        GA_Text,      "Loeschen",
        TAG_DONE);

    Object *fav_vlayout = IIntuition->NewObject(NULL, "layout.gadget",
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,    BVS_GROUP,
        LAYOUT_Label,         (Tag)"Lesezeichen-Verwaltung",
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_DeferLayout,   TRUE,
        LAYOUT_AddChild,      gad_fav_chooser,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"Titel:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_RIGHT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  20,
                                  LAYOUT_AddChild,      gad_fav_title,
                                  CHILD_WeightedWidth,  80,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"Adresse (URL):",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_RIGHT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  20,
                                  LAYOUT_AddChild,      gad_fav_url,
                                  CHILD_WeightedWidth,  80,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      btn_fav_add,
                                  CHILD_WeightedWidth,  34,
                                  LAYOUT_AddChild,      btn_fav_update,
                                  CHILD_WeightedWidth,  33,
                                  LAYOUT_AddChild,      btn_fav_delete,
                                  CHILD_WeightedWidth,  33,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        TAG_DONE);

    /* ------------------------------------------------------------------------
     * Tab 5: Downloads (Download-Manager & Speicherort)
     * ------------------------------------------------------------------------ */
    const char *init_dl_dir = app->prefs.download_dir[0] ? app->prefs.download_dir : "RAM:";

    Object *gad_dl_dir = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID,            GID_P_DL_DIR,
        GA_RelVerify,     TRUE,
        GA_TabCycle,      TRUE,
        STRINGA_MaxChars, 255,
        STRINGA_TextVal,  (Tag)init_dl_dir,
        TAG_DONE);

    Object *btn_dl_dir_req = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        GID_P_DL_DIR_REQ,
        GA_RelVerify, TRUE,
        GA_Text,      "Ordner waehlen...",
        GA_HintInfo,  "Download-Verzeichnis ueber Dateidialog auswaehlen",
        TAG_DONE);

    Object *gad_dl_always_ask = IIntuition->NewObject(NULL, "checkbox.gadget",
        GA_ID,            GID_P_DL_ALWAYS_ASK,
        GA_RelVerify,     TRUE,
        GA_Text,          "Immer nach Zielordner fragen (ASL-Requester)",
        CHECKBOX_Checked, app->prefs.always_ask_download,
        TAG_DONE);

    Object *btn_dl_open_mgr = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        GID_P_DL_OPEN_MGR,
        GA_RelVerify, TRUE,
        GA_Text,      "Download-Manager jetzt oeffnen...",
        GA_HintInfo,  "Oeffnet das Odyssey/IBrowse-Download-Fenster",
        TAG_DONE);

    Object *dl_vlayout = IIntuition->NewObject(NULL, "layout.gadget",
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,    BVS_GROUP,
        LAYOUT_Label,         (Tag)"Download-Einstellungen",
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_DeferLayout,   TRUE,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"Standard-Ordner:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_RIGHT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  25,
                                  LAYOUT_AddChild,      gad_dl_dir,
                                  CHILD_WeightedWidth,  55,
                                  LAYOUT_AddChild,      btn_dl_dir_req,
                                  CHILD_WeightedWidth,  20,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"Dateidialog:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_RIGHT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  25,
                                  LAYOUT_AddChild,      gad_dl_always_ask,
                                  CHILD_WeightedWidth,  75,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              (Tag)"Download-Manager:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_RIGHT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  25,
                                  LAYOUT_AddChild,      btn_dl_open_mgr,
                                  CHILD_WeightedWidth,  75,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                  GA_Text,              (Tag)"Dateien werden bei deaktivierter Abfrage direkt in den Standard-Ordner geladen.",
                                  GA_ReadOnly,          TRUE,
                                  BUTTON_Justification, BCJ_LEFT,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        TAG_DONE);

    /* ------------------------------------------------------------------------
     * Pages & ClickTabs
     * ------------------------------------------------------------------------ */
    Object *gad_pages = IIntuition->NewObject(NULL, "page.gadget",
        PAGE_Current, 0,
        PAGE_Add,     server_vlayout,
        PAGE_Add,     ql_vlayout,
        PAGE_Add,     res_vlayout,
        PAGE_Add,     font_vlayout,
        PAGE_Add,     fav_vlayout,
        PAGE_Add,     dl_vlayout,
        TAG_DONE);

    Object *gad_tabs = IIntuition->NewObject(NULL, "clicktab.gadget",
        GA_ID,              GID_P_TAB,
        GA_RelVerify,       TRUE,
        GA_Text,            (Tag)s_tab_labels,
        CLICKTAB_PageGroup, gad_pages,
        TAG_DONE);

    /* ------------------------------------------------------------------------
     * Bottom Action Buttons: Speichern, Benutzen, Abbrechen
     * ------------------------------------------------------------------------ */
    Object *btn_save = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        GID_P_SAVE,
        GA_RelVerify, TRUE,
        GA_Text,      "Speichern",
        GA_HintInfo,  "Einstellungen dauerhaft in ENVARC: und ENV: speichern und anwenden",
        TAG_DONE);

    Object *btn_use = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        GID_P_USE,
        GA_RelVerify, TRUE,
        GA_Text,      "Benutzen",
        GA_HintInfo,  "Einstellungen temporaer fuer diese Sitzung anwenden",
        TAG_DONE);

    Object *btn_cancel = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        GID_P_CANCEL,
        GA_RelVerify, TRUE,
        GA_Text,      "Abbrechen",
        GA_HintInfo,  "Aenderungen verwerfen und Fenster schliessen",
        TAG_DONE);

    Object *bottom_bar = IIntuition->NewObject(NULL, "layout.gadget",
        LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
        LAYOUT_AddChild,      btn_save,
        CHILD_WeightedWidth,  34,
        LAYOUT_AddChild,      btn_use,
        CHILD_WeightedWidth,  33,
        LAYOUT_AddChild,      btn_cancel,
        CHILD_WeightedWidth,  33,
        TAG_DONE);

    /* ------------------------------------------------------------------------
     * Root Prefs Dialog Layout
     * ------------------------------------------------------------------------ */
    Object *root_prefs_layout = IIntuition->NewObject(NULL, "layout.gadget",
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_DeferLayout,   TRUE,
        LAYOUT_AddChild,      gad_tabs,
        CHILD_WeightedHeight, 100,
        LAYOUT_AddChild,      bottom_bar,
        CHILD_WeightedHeight, 0,
        TAG_DONE);

    if (!root_prefs_layout) {
        DBG_LOG("[PrefsGUI] Failed to create Reaction layout!\n");
        return;
    }

    struct Screen *target_scr = (app && app->win && app->win->WScreen) ? app->win->WScreen : (app ? app->custom_screen : NULL);

    Object *win_obj = IIntuition->NewObject(NULL, "window.class",
        WA_Title,             "VortexBrowser Voreinstellungen",
        WA_ScreenTitle,       "VortexBrowser",
        target_scr ? WA_CustomScreen : TAG_IGNORE, (Tag)target_scr,
        WA_CloseGadget,       TRUE,
        WA_DepthGadget,       TRUE,
        WA_DragBar,           TRUE,
        WA_Activate,          TRUE,
        WA_SmartRefresh,      TRUE,
        WA_InnerWidth,        560,
        WA_InnerHeight,       390,
        WA_IDCMP,             IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_VANILLAKEY,
        WINDOW_Position,      WPOS_CENTERSCREEN,
        WINDOW_ParentGroup,   root_prefs_layout,
        TAG_DONE);

    if (!win_obj) {
        DBG_LOG("[PrefsGUI] Failed to create Reaction window object!\n");
        IIntuition->DisposeObject(root_prefs_layout);
        return;
    }

    struct Window *win = (struct Window *)IIntuition->IDoMethod(win_obj, WM_OPEN);
    if (!win) {
        DBG_LOG("[PrefsGUI] Failed to open Intuition window!\n");
        IIntuition->DisposeObject(win_obj);
        return;
    }

    /* Populate initial chooser lists */
    rebuild_ql_list(temp_ql, temp_num_ql, &ql_list, gad_ql_chooser, win, 0);
    rebuild_fav_list(app, &fav_list, gad_fav_chooser, win, 0);

    uint16 current_ql_sel  = 0;
    uint16 current_fav_sel = 0;
    BOOL done = FALSE;
    BOOL apply_save = FALSE;
    BOOL apply_use  = FALSE;

    uint32 sigmask = 0;
    while (!done) {
        IIntuition->GetAttr(WINDOW_SigMask, win_obj, &sigmask);
        IExec->Wait(sigmask | SIGBREAKF_CTRL_C);

        uint32 res;
        int16 code = 0;
        while ((res = IIntuition->IDoMethod(win_obj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            switch (res & WMHI_CLASSMASK) {
                case WMHI_CLOSEWINDOW:
                    done = TRUE;
                    break;

                case WMHI_VANILLAKEY: {
                    uint16 key = (uint16)(res & WMHI_KEYMASK);
                    if (key == 0x1B) { /* ESC = Cancel */
                        done = TRUE;
                    } else if (key == 0x0D || key == 0x0A) { /* Enter = Use */
                        apply_use = TRUE;
                        done = TRUE;
                    }
                    break;
                }

                case WMHI_GADGETUP: {
                    uint32 gid = res & WMHI_GADGETMASK;

                    if (gid == GID_P_SAVE) {
                        apply_save = TRUE;
                        done = TRUE;
                    } else if (gid == GID_P_USE) {
                        apply_use = TRUE;
                        done = TRUE;
                    } else if (gid == GID_P_CANCEL) {
                        done = TRUE;
                    }
                    /* Tab 0: Server & Streaming Events */
                    else if (gid == GID_P_SERVER_PRESET) {
                        uint32 p_sel = 0;
                        IIntuition->GetAttr(CHOOSER_Selected, gad_srv_preset, &p_sel);
                        if (p_sel == 0) { /* Host-PC */
                            char p_str[16];
                            snprintf(p_str, sizeof(p_str), "%u", (unsigned)VORTEXBROWSE_HOSTPC_PORT);
                            set_string_text(gad_srv_host, win, VORTEXBROWSE_HOSTPC_HOST);
                            set_string_text(gad_srv_port, win, p_str);
                        } else if (p_sel == 1) { /* Raspberry Pi 4 */
                            char p_str[16];
                            snprintf(p_str, sizeof(p_str), "%u", (unsigned)VORTEXBROWSE_RPI_PORT);
                            set_string_text(gad_srv_host, win, VORTEXBROWSE_RPI_HOST);
                            set_string_text(gad_srv_port, win, p_str);
                        }
                    } else if (gid == GID_P_SERVER_HOST || gid == GID_P_SERVER_PORT) {
                        char h_buf[128], p_buf[16];
                        get_string_text(gad_srv_host, h_buf, sizeof(h_buf));
                        get_string_text(gad_srv_port, p_buf, sizeof(p_buf));
                        uint32 p_sel = 2; /* Custom */
                        if (strcmp(h_buf, VORTEXBROWSE_HOSTPC_HOST) == 0 && atoi(p_buf) == VORTEXBROWSE_HOSTPC_PORT) {
                            p_sel = 0;
                        } else if (strcmp(h_buf, VORTEXBROWSE_RPI_HOST) == 0 && atoi(p_buf) == VORTEXBROWSE_RPI_PORT) {
                            p_sel = 1;
                        }
                        IIntuition->SetGadgetAttrs((struct Gadget *)gad_srv_preset, win, NULL,
                                                   CHOOSER_Selected, p_sel,
                                                   TAG_DONE);
                    }
                    /* Tab 1: Schnellwahl Events */
                    else if (gid == GID_P_QL_CHOOSER) {
                        uint32 sel = 0;
                        IIntuition->GetAttr(CHOOSER_Selected, gad_ql_chooser, &sel);
                        if (sel < (uint32)temp_num_ql) {
                            current_ql_sel = (uint16)sel;
                            set_string_text(gad_ql_lbl,  win, temp_ql[sel].label);
                            set_string_text(gad_ql_url,  win, temp_ql[sel].url);
                            set_string_text(gad_ql_icon, win, temp_ql[sel].icon);
                        }
                    } else if (gid == GID_P_QL_UPDATE) {
                        if (current_ql_sel < temp_num_ql) {
                            get_string_text(gad_ql_lbl,  temp_ql[current_ql_sel].label, sizeof(temp_ql[0].label));
                            get_string_text(gad_ql_url,  temp_ql[current_ql_sel].url,   sizeof(temp_ql[0].url));
                            get_string_text(gad_ql_icon, temp_ql[current_ql_sel].icon,  sizeof(temp_ql[0].icon));
                            rebuild_ql_list(temp_ql, temp_num_ql, &ql_list, gad_ql_chooser, win, current_ql_sel);
                        }
                    } else if (gid == GID_P_QL_ADD) {
                        if (temp_num_ql < VB_MAX_QUICKLINKS) {
                            char buf_l[32], buf_u[512], buf_i[32];
                            get_string_text(gad_ql_lbl,  buf_l, sizeof(buf_l));
                            get_string_text(gad_ql_url,  buf_u, sizeof(buf_u));
                            get_string_text(gad_ql_icon, buf_i, sizeof(buf_i));
                            if (buf_l[0] == '\0') strncpy(buf_l, "Neuer Link", sizeof(buf_l) - 1);
                            if (buf_u[0] == '\0') strncpy(buf_u, "https://", sizeof(buf_u) - 1);
                            if (buf_i[0] == '\0') strncpy(buf_i, "bookmark", sizeof(buf_i) - 1);

                            strncpy(temp_ql[temp_num_ql].label, buf_l, sizeof(temp_ql[0].label) - 1);
                            strncpy(temp_ql[temp_num_ql].url,   buf_u, sizeof(temp_ql[0].url) - 1);
                            strncpy(temp_ql[temp_num_ql].icon,  buf_i, sizeof(temp_ql[0].icon) - 1);
                            current_ql_sel = (uint16)temp_num_ql;
                            temp_num_ql++;
                            rebuild_ql_list(temp_ql, temp_num_ql, &ql_list, gad_ql_chooser, win, current_ql_sel);
                        }
                    } else if (gid == GID_P_QL_DELETE) {
                        if (temp_num_ql > 0 && current_ql_sel < temp_num_ql) {
                            for (int i = current_ql_sel; i < temp_num_ql - 1; i++) {
                                temp_ql[i] = temp_ql[i + 1];
                            }
                            temp_num_ql--;
                            if (current_ql_sel >= temp_num_ql && temp_num_ql > 0) {
                                current_ql_sel = temp_num_ql - 1;
                            }
                            rebuild_ql_list(temp_ql, temp_num_ql, &ql_list, gad_ql_chooser, win, current_ql_sel);
                            if (temp_num_ql > 0) {
                                set_string_text(gad_ql_lbl,  win, temp_ql[current_ql_sel].label);
                                set_string_text(gad_ql_url,  win, temp_ql[current_ql_sel].url);
                                set_string_text(gad_ql_icon, win, temp_ql[current_ql_sel].icon);
                            } else {
                                set_string_text(gad_ql_lbl,  win, "");
                                set_string_text(gad_ql_url,  win, "");
                                set_string_text(gad_ql_icon, win, "");
                            }
                        }
                    } else if (gid == GID_P_QL_UP) {
                        if (current_ql_sel > 0 && current_ql_sel < temp_num_ql) {
                            QuickLinkItem t = temp_ql[current_ql_sel];
                            temp_ql[current_ql_sel] = temp_ql[current_ql_sel - 1];
                            temp_ql[current_ql_sel - 1] = t;
                            current_ql_sel--;
                            rebuild_ql_list(temp_ql, temp_num_ql, &ql_list, gad_ql_chooser, win, current_ql_sel);
                        }
                    } else if (gid == GID_P_QL_DOWN) {
                        if (current_ql_sel + 1 < temp_num_ql) {
                            QuickLinkItem t = temp_ql[current_ql_sel];
                            temp_ql[current_ql_sel] = temp_ql[current_ql_sel + 1];
                            temp_ql[current_ql_sel + 1] = t;
                            current_ql_sel++;
                            rebuild_ql_list(temp_ql, temp_num_ql, &ql_list, gad_ql_chooser, win, current_ql_sel);
                        }
                    }
                    /* Tab 1: Resolution & Screen Events */
                    else if (gid == GID_P_SCREEN_CHOOSER) {
                        uint32 scr_sel = 0;
                        IIntuition->GetAttr(CHOOSER_Selected, gad_screen_chooser, &scr_sel);
                        if (scr_sel == 1) {
                            invoke_screen_mode_requester(win, &temp_res_mode_id, &temp_w, &temp_h, &temp_depth, gad_res_w, gad_res_h, gad_res_chooser);
                        }
                    }
                    else if (gid == GID_P_SCREEN_MODE_REQ) {
                        invoke_screen_mode_requester(win, &temp_res_mode_id, &temp_w, &temp_h, &temp_depth, gad_res_w, gad_res_h, gad_res_chooser);
                    }
                    else if (gid == GID_P_RES_CHOOSER) {
                        uint32 sel = 0;
                        IIntuition->GetAttr(CHOOSER_Selected, gad_res_chooser, &sel);
                        if (sel < 5) {
                            char buf_w[16], buf_h[16];
                            snprintf(buf_w, sizeof(buf_w), "%u", (unsigned)g_res_presets[sel].width);
                            snprintf(buf_h, sizeof(buf_h), "%u", (unsigned)g_res_presets[sel].height);
                            set_string_text(gad_res_w, win, buf_w);
                            set_string_text(gad_res_h, win, buf_h);
                        }
                    }
                    /* Tab 3: Lesezeichen Events */
                    else if (gid == GID_P_FAV_CHOOSER) {
                        uint32 sel = 0;
                        IIntuition->GetAttr(CHOOSER_Selected, gad_fav_chooser, &sel);
                        if (sel < (uint32)app->num_favorites) {
                            current_fav_sel = (uint16)sel;
                            set_string_text(gad_fav_title, win, app->favorites[sel].title);
                            set_string_text(gad_fav_url,   win, app->favorites[sel].url);
                        }
                    } else if (gid == GID_P_FAV_UPDATE) {
                        if (current_fav_sel < app->num_favorites) {
                            get_string_text(gad_fav_title, app->favorites[current_fav_sel].title, sizeof(app->favorites[0].title));
                            get_string_text(gad_fav_url,   app->favorites[current_fav_sel].url,   sizeof(app->favorites[0].url));
                            rebuild_fav_list(app, &fav_list, gad_fav_chooser, win, current_fav_sel);
                        }
                    } else if (gid == GID_P_FAV_ADD) {
                        char title_buf[VB_MAX_TITLE_LEN];
                        char url_buf[VB_MAX_URL_LEN];
                        get_string_text(gad_fav_title, title_buf, sizeof(title_buf));
                        get_string_text(gad_fav_url,   url_buf,   sizeof(url_buf));
                        if (title_buf[0] != '\0' && url_buf[0] != '\0') {
                            if (favorites_add(app, title_buf, url_buf)) {
                                current_fav_sel = (uint16)(app->num_favorites - 1);
                                rebuild_fav_list(app, &fav_list, gad_fav_chooser, win, current_fav_sel);
                            }
                        }
                    } else if (gid == GID_P_FAV_DELETE) {
                        if (app->num_favorites > 0 && current_fav_sel < app->num_favorites) {
                            for (int i = current_fav_sel; i < app->num_favorites - 1; i++) {
                                app->favorites[i] = app->favorites[i + 1];
                            }
                            app->num_favorites--;
                            if (current_fav_sel >= app->num_favorites && app->num_favorites > 0) {
                                current_fav_sel = app->num_favorites - 1;
                            }
                            rebuild_fav_list(app, &fav_list, gad_fav_chooser, win, current_fav_sel);
                            if (app->num_favorites > 0) {
                                set_string_text(gad_fav_title, win, app->favorites[current_fav_sel].title);
                                set_string_text(gad_fav_url,   win, app->favorites[current_fav_sel].url);
                            } else {
                                set_string_text(gad_fav_title, win, "");
                                set_string_text(gad_fav_url,   win, "");
                            }
                        }
                    }
                    /* Tab 5: Downloads Events */
                    else if (gid == GID_P_DL_DIR_REQ) {
                        char cur_dir[256];
                        get_string_text(gad_dl_dir, cur_dir, sizeof(cur_dir));
                        struct FileRequester *freq = (struct FileRequester *)IAsl->AllocAslRequestTags(
                            ASL_FileRequest,
                            ASLFR_Window,        win,
                            ASLFR_SleepWindow,   TRUE,
                            ASLFR_TitleText,     "Download-Verzeichnis waehlen",
                            ASLFR_PositiveText,  "Auswaehlen",
                            ASLFR_DrawersOnly,   TRUE,
                            ASLFR_InitialDrawer, (Tag)(cur_dir[0] ? cur_dir : "RAM:"),
                            TAG_DONE);
                        if (freq) {
                            if (IAsl->AslRequest(freq, NULL)) {
                                if (freq->fr_Drawer && freq->fr_Drawer[0] != '\0') {
                                    set_string_text(gad_dl_dir, win, freq->fr_Drawer);
                                }
                            }
                            IAsl->FreeAslRequest(freq);
                        }
                    } else if (gid == GID_P_DL_OPEN_MGR) {
                        gui_open_download_manager(app);
                    }
                    break;
                }

                default:
                    break;
            }
        }
    }

    if (apply_save || apply_use) {
        /* 0. Commit Server & Streaming Settings */
        char new_host[128] = {0};
        char new_port_str[16] = {0};
        get_string_text(gad_srv_host, new_host, sizeof(new_host));
        get_string_text(gad_srv_port, new_port_str, sizeof(new_port_str));
        int new_port = atoi(new_port_str);
        if (new_port <= 0 || new_port > 65535) new_port = VORTEXBROWSE_HOSTPC_PORT;

        uint32 stream_sel = 0;
        IIntuition->GetAttr(CHOOSER_Selected, gad_stream_res_chooser, &stream_sel);
        BOOL stream_res_changed = ((uint8_t)stream_sel != app->prefs.stream_res_mode);
        app->prefs.stream_res_mode = (uint8_t)stream_sel;

        uint32 q_val = 100;
        IIntuition->GetAttr(SLIDER_Level, gad_jpeg_quality, &q_val);
        if (q_val < 20) q_val = 20;
        if (q_val > 100) q_val = 100;
        BOOL jpeg_q_changed = (app->prefs.jpeg_quality != (uint16_t)q_val);
        app->prefs.jpeg_quality = (uint16_t)q_val;

        uint32 dyn_v = 0;
        IIntuition->GetAttr(CHECKBOX_Checked, gad_dyn_video, &dyn_v);
        app->prefs.dynamic_video_quality = (dyn_v != 0);

        uint32 s_exit = 0;
        IIntuition->GetAttr(CHECKBOX_Checked, gad_save_exit, &s_exit);
        app->prefs.save_server_on_exit = (s_exit != 0);

        BOOL server_changed = FALSE;
        if (new_host[0] != '\0') {
            if (strcmp(app->server_host, new_host) != 0 || app->server_port != (uint16_t)new_port) {
                server_changed = TRUE;
                strncpy(app->server_host, new_host, sizeof(app->server_host) - 1);
                app->server_host[sizeof(app->server_host) - 1] = '\0';
                app->server_port = (uint16_t)new_port;
            }
            strncpy(app->prefs.server_host, new_host, sizeof(app->prefs.server_host) - 1);
            app->prefs.server_host[sizeof(app->prefs.server_host) - 1] = '\0';
            app->prefs.server_port = (uint16_t)new_port;
        }

        if (app->gad_server_chooser && app->win) {
            uint32 s_sel = 2; /* Custom */
            if (strcmp(app->server_host, VORTEXBROWSE_RPI_HOST) == 0 && app->server_port == VORTEXBROWSE_RPI_PORT) {
                s_sel = 0;
            } else if (strcmp(app->server_host, VORTEXBROWSE_HOSTPC_HOST) == 0 && app->server_port == VORTEXBROWSE_HOSTPC_PORT) {
                s_sel = 1;
            }
            IIntuition->SetGadgetAttrs((struct Gadget *)app->gad_server_chooser, app->win, NULL,
                                       CHOOSER_Selected, s_sel,
                                       TAG_DONE);
        }

        if (server_changed) {
            net_connect(app, app->server_host, app->server_port);
        } else if (jpeg_q_changed && app->connected) {
            char qcmd[32];
            snprintf(qcmd, sizeof(qcmd), "@JPEG_QUALITY:%u", (unsigned)app->prefs.jpeg_quality);
            net_send_url(app, qcmd);
        }

        /* 1. Commit Schnellwahl items */
        BOOL ql_changed = (app->prefs.num_quick_links != temp_num_ql);
        app->prefs.num_quick_links = temp_num_ql;
        for (int i = 0; i < temp_num_ql && i < VB_MAX_QUICKLINKS; i++) {
            if (strcmp(app->prefs.quick_links[i].label, temp_ql[i].label) != 0 ||
                strcmp(app->prefs.quick_links[i].url,   temp_ql[i].url) != 0 ||
                strcmp(app->prefs.quick_links[i].icon,  temp_ql[i].icon) != 0) {
                ql_changed = TRUE;
            }
            app->prefs.quick_links[i] = temp_ql[i];
        }

        /* 2. Read Unified Resolution */
        uint16 old_res_sel = app->prefs.resolution.preset_idx;
        uint16 old_w       = app->prefs.resolution.width;
        uint16 old_h       = app->prefs.resolution.height;

        uint32 res_sel = 0;
        IIntuition->GetAttr(CHOOSER_Selected, gad_res_chooser, &res_sel);

        char w_str[16], h_str[16];
        get_string_text(gad_res_w, w_str, sizeof(w_str));
        get_string_text(gad_res_h, h_str, sizeof(h_str));
        int custom_w = atoi(w_str);
        int custom_h = atoi(h_str);
        if (custom_w >= 320 && custom_w <= 1920) app->prefs.resolution.custom_w = (uint16_t)custom_w;
        if (custom_h >= 200 && custom_h <= 1080) app->prefs.resolution.custom_h = (uint16_t)custom_h;

        app->prefs.resolution.preset_idx = (uint16_t)res_sel;
        if (res_sel < 5) {
            app->prefs.resolution.width  = g_res_presets[res_sel].width;
            app->prefs.resolution.height = g_res_presets[res_sel].height;
            temp_res_mode_id = 0; /* Reset custom mode ID so find_rtg_screen_mode finds the exact mode for preset */
        } else if (res_sel == 6) {
            app->prefs.resolution.width  = app->prefs.resolution.custom_w;
            app->prefs.resolution.height = app->prefs.resolution.custom_h;
        }

        /* Read Screen Mode */
        uint32 scr_sel = 0;
        IIntuition->GetAttr(CHOOSER_Selected, gad_screen_chooser, &scr_sel);
        uint8_t new_screen_mode = (uint8_t)scr_sel;
        BOOL new_use_custom = (new_screen_mode != VB_SCREEN_WORKBENCH);
        BOOL screen_mode_changed = (new_screen_mode != app->prefs.resolution.screen_mode);
        BOOL res_changed = (app->prefs.resolution.preset_idx != old_res_sel ||
                            app->prefs.resolution.width != old_w ||
                            app->prefs.resolution.height != old_h);

        uint32 old_mode_id = app->prefs.resolution.mode_id;
        app->prefs.resolution.mode_id = temp_res_mode_id;
        app->prefs.resolution.depth   = temp_depth;

        uint32 soft_val = 0;
        IIntuition->GetAttr(CHECKBOX_Checked, gad_softscroll, &soft_val);
        app->prefs.soft_scroll = (soft_val != 0);

        BOOL old_status_bar = app->prefs.show_status_bar;
        uint32 status_val = 0;
        IIntuition->GetAttr(CHECKBOX_Checked, gad_show_status, &status_val);
        app->prefs.show_status_bar = (status_val != 0);
        BOOL status_bar_changed = (app->prefs.show_status_bar != old_status_bar);

        uint32 theme_sel = 0;
        IIntuition->GetAttr(CHOOSER_Selected, gad_theme_chooser, &theme_sel);
        BOOL theme_changed = ((uint8_t)theme_sel != app->prefs.dark_theme);
        app->prefs.dark_theme = (uint8_t)theme_sel;

        BOOL mode_id_changed = (app->prefs.resolution.mode_id != old_mode_id);
        BOOL screen_reopen_needed = screen_mode_changed || (new_use_custom && (res_changed || mode_id_changed));
        app->prefs.resolution.screen_mode       = new_screen_mode;
        app->prefs.resolution.use_custom_screen = new_use_custom;

        /* 3. Read Font, Charset & Search Engine Settings */
        uint32 g_charset = 0, g_size = 0;
        uint32 w_charset = 0, w_fsize = 0, w_zoom = 0, w_fam = 0, se_sel = 0;
        IIntuition->GetAttr(CHOOSER_Selected, gad_gui_charset,   &g_charset);
        IIntuition->GetAttr(CHOOSER_Selected, gad_gui_size,      &g_size);
        IIntuition->GetAttr(CHOOSER_Selected, gad_web_charset,   &w_charset);
        IIntuition->GetAttr(CHOOSER_Selected, gad_web_fsize,     &w_fsize);
        IIntuition->GetAttr(CHOOSER_Selected, gad_web_zoom,      &w_zoom);
        IIntuition->GetAttr(CHOOSER_Selected, gad_web_fam,       &w_fam);
        IIntuition->GetAttr(CHOOSER_Selected, gad_search_engine, &se_sel);

        app->prefs.font.gui_charset     = (uint8_t)g_charset;
        app->prefs.font.gui_font_size   = (uint8_t)g_size;
        app->prefs.font.web_charset     = (uint8_t)w_charset;
        if (w_fsize < 7) app->prefs.font.web_font_size = s_web_fontsize_values[w_fsize];
        if (w_zoom < 8)  app->prefs.font.web_zoom_pct  = s_web_zoom_values[w_zoom];
        app->prefs.font.web_font_family = (uint8_t)w_fam;
        if (se_sel < NUM_SEARCH_ENGINES) {
            app->prefs.search_engine = (uint8_t)se_sel;
            if (app->gad_search_chooser && app->win) {
                IIntuition->SetGadgetAttrs((struct Gadget *)app->gad_search_chooser, app->win, NULL,
                                           CHOOSER_Selected, se_sel,
                                           TAG_DONE);
            }
        }

        /* 4. Read Download Settings */
        char dl_dir_buf[256];
        get_string_text(gad_dl_dir, dl_dir_buf, sizeof(dl_dir_buf));
        if (dl_dir_buf[0] != '\0') {
            strncpy(app->prefs.download_dir, dl_dir_buf, sizeof(app->prefs.download_dir) - 1);
            app->prefs.download_dir[sizeof(app->prefs.download_dir) - 1] = '\0';
        }
        uint32 dl_ask_val = 0;
        IIntuition->GetAttr(CHECKBOX_Checked, gad_dl_always_ask, &dl_ask_val);
        app->prefs.always_ask_download = (dl_ask_val != 0);

        /* If user explicitly picked a fixed window resolution preset, resize window now */
        if (!screen_reopen_needed && res_changed && res_sel != 5) {
            prefs_resize_window_to_resolution(app, app->prefs.resolution.width, app->prefs.resolution.height);
        }

        /* Save preferences (captures current window LeftEdge/TopEdge/Width/Height) */
        prefs_save(app, apply_save);
        if (apply_save) {
            favorites_save(app);
        }

        if (screen_reopen_needed) {
            app->reopen_screen_needed = TRUE;
        } else if (ql_changed || status_bar_changed || theme_changed) {
            prefs_apply(app);
            app->reopen_gui_needed = TRUE;
        } else {
            prefs_apply(app);
            favorites_rebuild_chooser_list(app);
            if (stream_res_changed) {
                app->renderer.reported_w = 0;
                app->renderer.reported_h = 0;
                renderer_sync_viewport_size(app);
            }
        }
    }

    /* Detach dynamic chooser lists before disposing window object */
    if (gad_ql_chooser) {
        IIntuition->SetAttrs(gad_ql_chooser, CHOOSER_Labels, (Tag)NULL, TAG_DONE);
    }
    if (gad_fav_chooser) {
        IIntuition->SetAttrs(gad_fav_chooser, CHOOSER_Labels, (Tag)NULL, TAG_DONE);
    }

    IIntuition->DisposeObject(win_obj);

    free_chooser_list(&ql_list);
    free_chooser_list(&fav_list);
}
