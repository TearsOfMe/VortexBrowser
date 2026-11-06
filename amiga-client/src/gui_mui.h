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
    GID_URL,
    GID_CLEAR_URL,
    GID_GO,
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
    GID_QUICK_BASE = 50,
    GID_QUICK_MAX  = 50 + VB_MAX_QUICKLINKS
};

/* Menu IDs */
enum {
    MID_PROJECT = 100,
    MID_SETTINGS,
    MID_ABOUT,
    MID_CLEAR_CACHE,
    MID_QUIT,

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

    MID_FPS_15 = 200,
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

#endif /* VORTEXBROWSE_GUI_MUI_H */
