#ifndef VORTEXBROWSE_GUI_MUI_H
#define VORTEXBROWSE_GUI_MUI_H

#include "app.h"

/* MUI Gadget & Action IDs */
enum {
    GID_BACK = 1,
    GID_FORWARD,
    GID_RELOAD,
    GID_STOP,
    GID_HOME,
    GID_BOOKMARKS,
    GID_DOWNLOADS,
    GID_PASSWORDS,
    GID_NEW_TAB,
    GID_CLOSE_TAB,
    GID_SECURITY_BADGE,
    GID_URL,
    GID_CLEAR_URL,
    GID_GO,
    GID_SEARCH_ENGINE,
    GID_SEARCH,
    GID_SEARCH_GO,
    GID_ADD_FAV,
    GID_FAV_CHOOSER,
    GID_BM_AMIGANEWS,
    GID_BM_OS4DEPOT,
    GID_BM_AMIGANS,
    GID_BM_AMINET,
    GID_BM_YOUTUBE,
    GID_BM_WIKI,
    GID_SERVER_CHOOSER,
    GID_CONNECT,
    GID_MENU,
    GID_STATUS,
    GID_GAUGE,
    GID_QUICK_ADD,
    GID_QUICK_REMOVE,
    GID_PREFS,
    GID_TAB_ADD,

    GID_QUICK_BASE     = 50,
    GID_QUICK_MAX      = 50 + VB_MAX_QUICKLINKS,

    GID_TAB_BASE       = 100,
    GID_TAB_MAX        = 100 + VB_MAX_TABS,
    GID_TAB_CLOSE_BASE = 150,
    GID_TAB_CLOSE_MAX  = 150 + VB_MAX_TABS
};

/* Menu IDs */
enum {
    MID_PROJECT = 200,
    MID_NEW_TAB,
    MID_CLOSE_TAB,
    MID_SETTINGS,
    MID_PASSWORDS,
    MID_DOWNLOADS,
    MID_CLEAR_CACHE,
    MID_ABOUT,
    MID_QUIT,

    MID_EDIT,
    MID_EDIT_CUT,
    MID_EDIT_COPY,
    MID_EDIT_PASTE,

    MID_NAV,
    MID_BACK,
    MID_FORWARD,
    MID_RELOAD,
    MID_STOP,
    MID_HOME,

    MID_BOOKMARKS,
    MID_ADD_FAV,
    MID_QUICK_ADD,
    MID_QUICK_REMOVE,
    MID_BM_AMIGANEWS,
    MID_BM_OS4DEPOT,
    MID_BM_AMIGANS,
    MID_BM_AMINET,
    MID_BM_YOUTUBE,
    MID_BM_WIKI,

    MID_SERVER,
    MID_SERVER_RPI,
    MID_SERVER_PC,
    MID_SERVER_CUSTOM,
    MID_DISCONNECT,

    MID_VIEW,
    MID_BITRATE_2000,
    MID_BITRATE_4000,
    MID_BITRATE_6000,
    MID_BITRATE_8000,
    MID_BITRATE_10000,

    MID_FPS_15,
    MID_FPS_25,
    MID_FPS_30,
    MID_FPS_50,
    MID_FPS_60
};

BOOL gui_mui_init(VortexApp *app);
void gui_mui_cleanup(VortexApp *app);
void gui_mui_handle_events(VortexApp *app);
void gui_mui_handle_action(VortexApp *app, uint32 action_id);
void gui_mui_flush_dirty(VortexApp *app);
void gui_mui_update_url_bar(VortexApp *app, const char *url);
void gui_mui_rebuild_fav_cycle(VortexApp *app);
void gui_mui_rebuild_quicklinks(VortexApp *app);
void gui_mui_rebuild_tabs(VortexApp *app);
void gui_mui_tab_add(VortexApp *app, const char *title, const char *url);
void gui_mui_tab_switch(VortexApp *app, uint16 idx);
void gui_mui_tab_close(VortexApp *app, uint16 idx);
void gui_mui_update_status_bar(VortexApp *app);

#endif /* VORTEXBROWSE_GUI_MUI_H */
