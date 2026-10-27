#ifndef VORTEXBROWSE_GUI_H
#define VORTEXBROWSE_GUI_H

#include "app.h"

#define VORTEXBROWSE_RPI_HOST       "192.168.178.2"
#define VORTEXBROWSE_RPI_PORT       7788
#define VORTEXBROWSE_HOSTPC_HOST    "192.168.178.60"
#define VORTEXBROWSE_HOSTPC_PORT    9757

/* Reaction Gadget IDs (Odyssey Web Browser Layout) */
enum {
    GID_BACK = 1,
    GID_FORWARD,
    GID_RELOAD,
    GID_STOP,
    GID_HOME,
    GID_SCROLL_UP,
    GID_SCROLL_DOWN,
    GID_HOST,
    GID_CONNECT,
    GID_URL,
    GID_CLEAR_URL,
    GID_GO,
    GID_SEARCH_CHOOSER,
    GID_SEARCH,
    GID_SEARCH_GO,
    GID_ADD_FAV,
    GID_FAV_CHOOSER,
    GID_MENU,
    GID_SERVER_CHOOSER,
    GID_SPACE,
    GID_STATUS,
    GID_GAUGE,
    GID_QUICK_ADD,
    GID_CLICKTAB = 40,
    GID_TAB_NEW,
    GID_URL_FAVICON = 45,
    GID_QUICK_BASE = 50,     /* 50 .. 50 + VB_MAX_QUICKLINKS - 1 */
    GID_QUICK_DEL_BASE = 70  /* 70 .. 70 + VB_MAX_QUICKLINKS - 1 */
};

/* Amiga Standard Pull-Down Menu Action IDs */
enum {
    MID_PROJ_NEW = 100,
    MID_PROJ_OPEN,
    MID_PROJ_CONNECT_RPI4,
    MID_PROJ_CONNECT_HOSTPC,
    MID_PROJ_DISCONNECT,
    MID_PROJ_ABOUT,
    MID_PROJ_PASSWORDS,
    MID_PROJ_AUTOLOGIN,
    MID_TAB_NEW,
    MID_TAB_CLOSE,
    MID_PROJ_QUIT,

    MID_EDIT_CUT,
    MID_EDIT_COPY,
    MID_EDIT_PASTE,

    MID_NAV_BACK,
    MID_NAV_FORWARD,
    MID_NAV_RELOAD,
    MID_NAV_STOP,
    MID_NAV_HOME,
    MID_FONT_LARGER,
    MID_FONT_SMALLER,
    MID_FONT_RESET,

    MID_FAV_ADD,
    MID_QUICK_ADD_CURRENT,
    MID_FAV_AMIGANEWS,
    MID_FAV_OS4DEPOT,
    MID_FAV_AMIGANS,
    MID_FAV_AMINET,
    MID_FAV_YOUTUBE,
    MID_FAV_WIKIPEDIA,

    MID_SET_SERVER_RPI4,
    MID_SET_SERVER_HOSTPC,
    MID_SET_SERVER_CUSTOM,
    MID_SET_SAVE,
    MID_SETTINGS,
    MID_TOGGLE_SCREEN,

    MID_RES_1024_768,
    MID_RES_1280_720,
    MID_RES_1920_1080,
    MID_RES_800_600,
    MID_RES_1280_1024,
    MID_RES_AUTO,

    MID_STREAM_RES_NATIVE,
    MID_STREAM_RES_720P,
    MID_STREAM_RES_540P,
    MID_STREAM_RES_1080P,
    MID_TOGGLE_STATUSBAR,
    MID_TOGGLE_DARK_THEME
};

BOOL gui_init(VortexApp *app);
void gui_cleanup(VortexApp *app);
void gui_rebuild_window(VortexApp *app);
void gui_reopen_screen(VortexApp *app);
void prompt_add_quicklink(VortexApp *app);
void gui_handle_events(VortexApp *app);
void gui_poll_mouse(VortexApp *app);
void gui_set_rmb_trap(VortexApp *app, BOOL trap);
uint32 gui_get_signal_mask(VortexApp *app);
void gui_update_url_bar(VortexApp *app, const char *url);
void gui_tab_add(VortexApp *app, const char *title, const char *url, BOOL switch_to);
void gui_tab_switch(VortexApp *app, uint16 idx);
void gui_tab_close(VortexApp *app, uint16 idx);
void gui_tab_set_title(VortexApp *app, uint16 idx, const char *title);

#endif /* VORTEXBROWSE_GUI_H */

