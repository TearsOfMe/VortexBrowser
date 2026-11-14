#include "locale_strings.h"
#include "app.h"
#include <proto/exec.h>
#include <proto/locale.h>
#include <proto/codesets.h>
#include <libraries/codesets.h>
#include <proto/dos.h>
#include <libraries/locale.h>
#include <utility/tagitem.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32      id;
    const char *str;
} FallbackCatalogEntry;

struct Library             *LocaleBase         = NULL;
struct LocaleIFace         *ILocale            = NULL;
struct Library             *CodesetsBase       = NULL;
struct CodesetsIFace       *ICodesets          = NULL;
static struct Catalog      *s_catalog          = NULL;
static struct Locale       *s_locale           = NULL;
static char                 s_active_language[128] = "english (built-in)";
static FallbackCatalogEntry *s_fallback_entries = NULL;
static size_t               s_fallback_count   = 0;
static char                *s_fallback_pool    = NULL;
static char                 s_fallback_lang[64] = "";

static uint32 read_be32(const uint8 *p)
{
    return ((uint32)p[0] << 24) | ((uint32)p[1] << 16) |
           ((uint32)p[2] << 8)  | (uint32)p[3];
}

static int cmp_fallback_entry(const void *a, const void *b)
{
    const FallbackCatalogEntry *ea = (const FallbackCatalogEntry *)a;
    const FallbackCatalogEntry *eb = (const FallbackCatalogEntry *)b;
    if (ea->id < eb->id) return -1;
    if (ea->id > eb->id) return 1;
    return 0;
}

static CONST_STRPTR fallback_lookup(ULONG id, CONST_STRPTR def_str)
{
    size_t lo = 0;
    size_t hi = s_fallback_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        uint32 mid_id = s_fallback_entries[mid].id;
        if (mid_id == (uint32)id) {
            return (CONST_STRPTR)s_fallback_entries[mid].str;
        }
        if (mid_id < (uint32)id) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return def_str;
}

static void locale_free_fallback(void)
{
    free(s_fallback_entries);
    s_fallback_entries = NULL;
    s_fallback_count   = 0;
    free(s_fallback_pool);
    s_fallback_pool    = NULL;
    s_fallback_lang[0] = '\0';
}

typedef struct {
    ULONG id;
    CONST_STRPTR str;
} StringEntry;

static const StringEntry s_builtins[] = {
    { MSG_WIN_TITLE,           "VortexBrowser" },
    { MSG_DL_WIN_TITLE,        "Downloads - VortexBrowser" },
    { MSG_PW_WIN_TITLE,        "Password Manager - VortexBrowser" },
    { MSG_PREFS_WIN_TITLE,     "Settings - VortexBrowser" },

    { MSG_MENU_PROJECT,        "Project" },
    { MSG_MENU_OPEN_URL,       "Open URL..." },
    { MSG_MENU_OPEN_FILE,      "Open Local File..." },
    { MSG_MENU_SAVE_SOURCE,    "Save HTML Source..." },
    { MSG_MENU_PRINT,          "Print Page..." },
    { MSG_MENU_ABOUT,          "About VortexBrowser..." },
    { MSG_MENU_QUIT,           "Quit" },

    { MSG_MENU_NAVIGATION,     "Navigation" },
    { MSG_MENU_BACK,           "Back" },
    { MSG_MENU_FORWARD,        "Forward" },
    { MSG_MENU_RELOAD,         "Reload" },
    { MSG_MENU_STOP,           "Stop Loading" },
    { MSG_MENU_HOME,           "Home" },
    { MSG_MENU_TAB_NEW,        "Open New Tab" },
    { MSG_MENU_TAB_CLOSE,      "Close Current Tab" },

    { MSG_MENU_BOOKMARKS,      "Bookmarks" },
    { MSG_MENU_BOOKMARKS_BAR,  "Bookmarks Toolbar" },
    { MSG_MENU_ADD_BOOKMARK,   "Add Bookmark" },
    { MSG_MENU_ADD_QUICKLINK,  "Add to QuickLinks" },

    { MSG_MENU_VIEW,           "View" },
    { MSG_MENU_ZOOM_IN,        "Zoom In (+)" },
    { MSG_MENU_ZOOM_OUT,       "Zoom Out (-)" },
    { MSG_MENU_ZOOM_RESET,     "Default Zoom (100%)" },
    { MSG_MENU_FULLSCREEN,     "Toggle Fullscreen" },
    { MSG_MENU_DARK_MODE,      "Dark Mode" },

    { MSG_MENU_SETTINGS,       "Settings" },
    { MSG_MENU_PREFERENCES,    "Preferences..." },
    { MSG_MENU_PASSWORDS,      "Password Manager..." },
    { MSG_MENU_DOWNLOADS,      "Download Manager..." },

    { MSG_MENU_EDIT,           "Edit" },
    { MSG_MENU_CUT,            "Cut" },
    { MSG_MENU_COPY,           "Copy" },
    { MSG_MENU_PASTE,          "Paste" },

    { MSG_BTN_BACK,            "Back" },
    { MSG_BTN_FORWARD,         "Forward" },
    { MSG_BTN_RELOAD,          "Reload" },
    { MSG_BTN_STOP,            "Stop" },
    { MSG_BTN_HOME,            "Home" },
    { MSG_BTN_GO,              "Go" },
    { MSG_BTN_DOWNLOADS,       "Downloads" },
    { MSG_BTN_CONNECT,         "Connect" },
    { MSG_BTN_DISCONNECT,      "Disconnect" },
    { MSG_BTN_CLEAR,           "X" },
    { MSG_CHOOSER_BOOKMARKS,   "Bookmarks" },

    { MSG_HINT_BACK,           "Navigate Back (Amiga+Left)" },
    { MSG_HINT_FORWARD,        "Navigate Forward (Amiga+Right)" },
    { MSG_HINT_RELOAD,         "Reload Page (Amiga+R)" },
    { MSG_HINT_STOP,           "Stop Loading (Esc)" },
    { MSG_HINT_HOME,           "Open Home Page (Amiga+H)" },
    { MSG_HINT_CLEAR_URL,      "Clear Address Bar" },
    { MSG_HINT_GO,             "Open URL or start web search" },
    { MSG_HINT_DOWNLOADS,      "Open Download Manager (Amiga+J)" },
    { MSG_HINT_CONNECT,        "Connect to Server" },
    { MSG_HINT_ADD_QL,         "Add Current Page to QuickLinks" },
    { MSG_HINT_REM_QL,         "Remove QuickLink" },
    { MSG_HINT_NEW_TAB,        "Open New Tab (Amiga+T)" },
    { MSG_HINT_CLOSE_TAB,      "Close Current Tab (Amiga+W)" },

    { MSG_DL_COL_FILE,         "File" },
    { MSG_DL_COL_STATUS,       "Status" },
    { MSG_DL_COL_SIZE,         "Size" },
    { MSG_DL_COL_PERCENT,      "%" },
    { MSG_DL_COL_SPEED,        "Speed" },
    { MSG_DL_COL_ETA,          "Time Left" },
    { MSG_DL_COL_TARGET,       "Target Path" },

    { MSG_DL_STATUS_NONE,      "No active downloads" },
    { MSG_DL_STATUS_ACTIVE,    "%d active download%s (%s) | %d completed" },
    { MSG_DL_STATUS_ALL_DONE,  "All downloads completed (%d files)" },

    { MSG_DL_BTN_OPEN_FILE,    "Open File" },
    { MSG_DL_HINT_OPEN_FILE,   "Execute downloaded file with default viewer (or double-click)" },
    { MSG_DL_BTN_OPEN_DIR,     "Open Drawer" },
    { MSG_DL_HINT_OPEN_DIR,    "Open target drawer on Workbench" },
    { MSG_DL_BTN_CANCEL,       "Cancel" },
    { MSG_DL_HINT_CANCEL,      "Cancel and abort running download" },
    { MSG_DL_BTN_REMOVE,       "Delete Entry" },
    { MSG_DL_HINT_REMOVE,      "Remove selected entry from list" },
    { MSG_DL_BTN_CLEANUP,      "Clean Up" },
    { MSG_DL_HINT_CLEANUP,     "Remove all completed and canceled downloads from list" },
    { MSG_DL_BTN_CLOSE,        "Close" },
    { MSG_DL_HINT_CLOSE,       "Close Download Manager window (downloads continue in background)" },

    { MSG_STATUS_READY,        "Ready." },
    { MSG_STATUS_CONNECTING,   "Connecting to server %s:%u..." },
    { MSG_STATUS_CONNECTED,    "Connected to %s:%u" },
    { MSG_STATUS_DISCONNECTED, "Disconnected from server" },
    { MSG_STATUS_LOADING,      "Loading: %s" },
    { MSG_SEARCH_PROMPT,       "Search (%s): %s" },

    { MSG_CTX_OPEN_LINK,       "Open Link" },
    { MSG_CTX_OPEN_NEW_TAB,    "Open Link in New Tab" },
    { MSG_CTX_FAV_LINK,        "Add Link to Bookmarks" },
    { MSG_CTX_DOWNLOAD_LINK,   "Download Link Target..." },
    { MSG_CTX_COPY_LINK,       "Copy Link URL" },
    { MSG_CTX_COPY_IMAGE_URL,  "Copy Image URL" },
    { MSG_CTX_DOWNLOAD_IMAGE,  "Download Image..." },
    { MSG_CTX_MAIN_MENU,       "Main Menu..." },

    { MSG_GEN_YES,             "Yes" },
    { MSG_GEN_NO,              "No" },
    { MSG_GEN_OK,              "OK" },
    { MSG_GEN_CANCEL,          "Cancel" },

    { MSG_PREFS_SAVE,          "Save" },
    { MSG_PREFS_USE,           "Use" },
    { MSG_PREFS_CANCEL,        "Cancel" },
    { MSG_PREFS_TAB_SERVER,    "Server" },
    { MSG_PREFS_TAB_DISPLAY,   "Display" },
    { MSG_PREFS_TAB_BROWSER,   "Browser" },
    { MSG_PREFS_TAB_QUICKDIAL, "Quick Dial" },
    { MSG_PREFS_TAB_BOOKMARKS, "Bookmarks" },
    { MSG_PREFS_TAB_DOWNLOADS, "Downloads" },
    { MSG_PREFS_TAB_SYSTEM,    "System" },
    { MSG_PREFS_TAB_ABOUT,     "About" },
    { MSG_PREFS_ADD,           "Add" },
    { MSG_PREFS_UPDATE,        "Update" },
    { MSG_PREFS_REMOVE,        "Delete" },
    { MSG_PREFS_MOVE_UP,       "Move Up" },
    { MSG_PREFS_MOVE_DOWN,     "Move Down" },
    { MSG_PREFS_CHOOSE_DIR,    "Choose Drawer..." },

    { MSG_MENU_NEW_HOME,          "Home / Startpage" },
    { MSG_MENU_SERVER_CFG,        "Configure Server..." },
    { MSG_MENU_SERVER_CONNECT,    "Connect to Server" },
    { MSG_MENU_SERVER_DISCONNECT, "Disconnect" },
    { MSG_MENU_STREAM_RES_NATIVE, "Stream: 1:1 Native (Window Size)" },
    { MSG_MENU_STREAM_RES_720P,   "Stream: 720p HD (1280x720) [30-50 FPS]" },
    { MSG_MENU_STREAM_RES_540P,   "Stream: 540p qHD (960x540) [Turbo]" },
    { MSG_MENU_STREAM_RES_1080P,  "Stream: 1080p FHD (1920x1080)" },
    { MSG_MENU_FPS_15,            "FPS: 15 FPS (Power Saving / Slow CPU)" },
    { MSG_MENU_FPS_25,            "FPS: 25 FPS (Standard / PAL Smooth)" },
    { MSG_MENU_FPS_30,            "FPS: 30 FPS (Recommended for YouTube / NTSC)" },
    { MSG_MENU_FPS_50,            "FPS: 50 FPS (PAL High-Speed)" },
    { MSG_MENU_FPS_60,            "FPS: 60 FPS (Maximum / Gaming / PiStorm)" },
    { MSG_MENU_VIDEO_AUTO,        "Video Stream: Auto (H.264 via va.library)" },
    { MSG_MENU_VIDEO_LZ4,         "Video Stream: Dirty-Rect Only (LZ4)" },
    { MSG_MENU_VIDEO_H264,        "Video Stream: H.264 Only (Always)" },
    { MSG_MENU_TOGGLE_DARK,       "Toggle Dark Mode" },
    { MSG_MENU_TOGGLE_SCREEN,     "Toggle Fullscreen / Screen (F11)" },
    { MSG_MENU_TOGGLE_STATUSBAR,  "Toggle Status Bar" },
    { MSG_POPUP_MAIN_TITLE,       "VortexBrowser Menu" },
    { MSG_POPUP_SAVE_SETTINGS,    "Save Settings" },
    { MSG_POPUP_OPT_TITLE,        "VortexBrowser Options" },
    { MSG_POPUP_OPT_PROMPT,       "Choose action:" },
    { MSG_POPUP_OPT_BUTTONS,      "Configure Server|Connect|Disconnect|Home|Add Bookmark|About|Cancel" },
    { MSG_BTN_SEARCH,             "Search" },
    { MSG_HINT_FAVICON,           "Website Favicon (Click for Password Manager)" },
    { MSG_HINT_URL,               "Enter web address (URL) and press Enter" },
    { MSG_HINT_SEARCH_CHOOSER,    "Choose search engine (Default: DuckDuckGo)" },
    { MSG_HINT_SEARCH_INPUT,      "Enter search terms and press Enter" },
    { MSG_HINT_SEARCH_GO,         "Execute Search" },

    { MSG_QL_POPUP_TITLE_FMT,     "Quick Dial: %.40s" },
    { MSG_QL_POPUP_OPEN_CUR,      "Open in Current Tab" },
    { MSG_QL_POPUP_OPEN_NEW,      "Open in New Tab" },
    { MSG_QL_POPUP_COPY_URL,      "Copy URL to Clipboard" },
    { MSG_QL_POPUP_EDIT,          "Customize Quick Dial (Prefs)..." },
    { MSG_QL_POPUP_REMOVE,        "Remove This Button" },
    { MSG_QL_POPUP_REQ_TEXT,      "Choose action for Quick Dial button:\n%s" },
    { MSG_QL_POPUP_REQ_BTNS,      "Open in Tab|New Tab|Copy URL|Remove|Cancel" },
    { MSG_QL_ADD_REQ_TITLE,       "Add to Quick Dial" },
    { MSG_QL_ADD_REQ_TEXT,        "Do you want to add this page to Quick Dial?\n\nTitle: %s\nURL: %s" },
    { MSG_QL_ADD_REQ_BTNS,        "Add|Customize Quick Dial...|Cancel" },
    { MSG_CTX_HDR_LINK,           "Link: %.50s" },
    { MSG_CTX_HDR_MEDIA,          "Image / Media" },
    { MSG_CTX_OPEN_MEDIA,         "View Image / Media" },
    { MSG_CTX_FAV_PAGE,           "Add Current Page to Bookmarks (+Fav)" },
    { MSG_CTX_DL_PAGE,            "Download Current Address (ASL)..." },
    { MSG_CTX_DARK_ON,            "Dark Theme" },
    { MSG_CTX_LIGHT_ON,           "Light Theme" },
    { MSG_CTX_DARK_MODE_FULL,     "Dark Theme (Dark Mode)" },
    { MSG_CTX_LIGHT_MODE_FULL,    "Light Theme (Light Mode)" },
    { MSG_CTX_REQ_TITLE,          "VortexBrowser Context Menu" },
    { MSG_CTX_REQ_LINK_TEXT,      "Link: %s" },
    { MSG_CTX_REQ_LINK_BTNS,      "Open Link|New Tab|Close Tab|Download (ASL)|Copy URL|Main Menu|Back|Cancel" },
    { MSG_CTX_REQ_PAGE_TEXT,      "Choose page action:" },
    { MSG_CTX_REQ_PAGE_BTNS,      "Back|Forward|Reload|Home|New Tab|Close Tab|Main Menu|Cancel" },
    { MSG_SRV_DLG_TITLE,          "Configure Server" },
    { MSG_SRV_DLG_SCRTITLE,       "VortexBrowser Settings" },
    { MSG_SRV_DLG_PROMPT,         "Enter VortexBrowser server address (IP:Port):" },
    { MSG_SRV_DLG_HINT,           "Server IP and port (e.g. 127.0.0.1:7788)" },
    { MSG_SRV_DLG_CONNECT,        "Connect" },

    { MSG_ABOUT_CONNECTED,        "Connected" },
    { MSG_ABOUT_DISCONNECTED,     "Disconnected" },
    { MSG_ABOUT_YUV_HW,           "Planar YUV420p 12bpp (Hardware DMA+CompositeTags)" },
    { MSG_ABOUT_YUV_SW,           "Planar YUV420p 12bpp (Software RGB565)" },
    { MSG_ABOUT_SCR_CUSTOM,       "Custom RTG Screen" },
    { MSG_ABOUT_SCR_WB,           "Workbench Screen" },
    { MSG_ABOUT_ALTIVEC_YES,      "AltiVec 128-Bit SIMD (Enabled - X1000/G4)" },
    { MSG_ABOUT_ALTIVEC_NO,       "Scalar Baseline (Safe for X5000/A1222+/Sam460/G3)" },
    { MSG_ABOUT_SEC_SERVER,       "VortexBrowser for AmigaOS 4\nVersion 0.99 RC3\n\n=== SERVER INFORMATION ===\nServer Address: %s:%u\nConnection Status: %s\nServer Engine: Chromium (Linux Headless Streaming)\n\n" },
    { MSG_ABOUT_SEC_GRAPHICS,     "=== RESOLUTION & GRAPHICS ===\nScreen: %ux%u (%u-bit, %s)\nWindow Size: %ux%u (Pos: %d,%d)\nBrowser Viewport: %ux%u px (1:1 Auto-Sync)\nServer Stream: %ux%u (%s)\nDecoded Frames: %u\nActive Language: %s\n\n" },
    { MSG_ABOUT_SEC_AUDIO,        "=== AUDIO & OPTIMIZATION ===\nAudio Engine: AHI 16-Bit Stereo (44100 Hz CD Quality PCM Low-Latency)\nCPU Target: PowerPC Universal (Dynamic CPU Detection)\nVector SIMD: %s\nCompression: LZ4 Realtime Stream Decompressor" },
    { MSG_ABOUT_TITLE,            "About VortexBrowser" },
    { MSG_CPU_INCOMPAT_TITLE,     "VortexBrowser - Incompatible CPU" },
    { MSG_CPU_INCOMPAT_BODY,      "This build ('VortexBrowser-AltiVec') requires a CPU with an AltiVec/VMX SIMD unit (e.g. AmigaOne X1000 or G4).\n\nOn systems such as AmigaOne X5000, A1222+, Sam460 or G3, please launch the standard 'VortexBrowser' build!" },

    { MSG_STATUS_READY_STREAM,       "Ready - Stream System: %s | %u kbps" },
    { MSG_STATUS_CONNECTED_STREAM,   "Connected to %s:%u - Stream: %s | %u kbps" },
    { MSG_STATUS_CONN_SYNC,          "Connected to %s:%u - Synchronizing viewport..." },
    { MSG_STATUS_CONN_FMT,           "Connected (%ux%u %s %s) | %s" },
    { MSG_STATUS_CONN_H264,          "Connected (%ux%u H.264 VA-API Hardware) | %s" },
    { MSG_STATUS_CONN_LZ4,           "Connected (%ux%u LZ4 2D Dirty-Rect) | %s" },
    { MSG_STATUS_NO_BSDSOCKET,       "Error: bsdsocket.library not available!" },
    { MSG_STATUS_NOT_CONNECTED,      "Not connected. Please configure server (Amiga+1)." },
    { MSG_STATUS_HOST_RESOLVE_ERR,   "Could not resolve host: %s" },
    { MSG_STATUS_SOCKET_ERR,         "Socket creation failed!" },
    { MSG_STATUS_CONN_FAILED,        "Connection to %s:%u failed!" },
    { MSG_STATUS_PAGE_LOADED,        "Page loaded: %s" },
    { MSG_STATUS_LOADING_PAGE,       "Loading page: %s ..." },
    { MSG_STATUS_LOADING_STOPPED,    "Loading stopped." },
    { MSG_STATUS_CREDENTIALS_FILLED, "Credentials filled for '%s'." },
    { MSG_STATUS_CLIPBOARD_COPIED,   "Text copied to clipboard (%u chars)" },
    { MSG_STATUS_SEARCH_ENGINE_SEL,  "Search engine selected: %s" },
    { MSG_STATUS_BOOKMARK_ADDED,     "Added to bookmarks: %s" },
    { MSG_STATUS_BOOKMARK_SAVED,     "Added to bookmarks (ENVARC:): %s" },
    { MSG_STATUS_SETTINGS_SAVED,     "Settings permanently saved (ENVARC:)." },
    { MSG_STATUS_STREAM_RES_NATIVE,  "Stream Resolution: 1:1 Native (Window Size)" },
    { MSG_STATUS_STREAM_RES_720P,    "Stream Resolution: 1280x720 (720p HD - 30-50 FPS)" },
    { MSG_STATUS_STREAM_RES_540P,    "Stream Resolution: 960x540 (540p qHD - Turbo)" },
    { MSG_STATUS_STREAM_RES_1080P,   "Stream Resolution: 1920x1080 (1080p Full HD)" },
    { MSG_STATUS_FPS_SET,            "Frame rate set: %u FPS" },
    { MSG_STATUS_THEME_SET,          "Color theme: %s" },
    { MSG_STATUS_THEME_DARK,         "Dark (Dark Mode)" },
    { MSG_STATUS_THEME_LIGHT,        "Light (Default)" },
    { MSG_STATUS_STREAM_AUTO,        "Streaming: Auto (H.264 on video via va.library, else LZ4)" },
    { MSG_STATUS_STREAM_LZ4,         "Streaming: Dirty-Rect / LZ4 Only (Classic)" },
    { MSG_STATUS_STREAM_H264,        "Streaming: H.264 Only (Continuous Video Stream)" },
    { MSG_STATUS_FONT_SIZE,          "Browser font size: %u px" },
    { MSG_STATUS_FONT_RESET,         "Browser font size reset to default (16 px / 100%%)." },
    { MSG_STATUS_RES_CUSTOM,         "Resolution: %s" },
    { MSG_STATUS_RES_AUTO,           "Resolution: Automatic" },
    { MSG_STATUS_MAX_TABS,           "Maximum %d tabs allowed." },
    { MSG_STATUS_TAB_SWITCH,         "Tab %d: %s" },
    { MSG_STATUS_SCREEN_ACTIVE,      "Screen activated: %s (%ux%u)" },
    { MSG_STATUS_SCREEN_CUSTOM,      "Custom Screen" },
    { MSG_STATUS_QL_STARTED,         "Quick Dial: %s" },
    { MSG_STATUS_QL_COPIED,          "URL copied: %s" },
    { MSG_STATUS_QL_ADDED,           "Quick Dial added: %s" },
    { MSG_STATUS_QL_FULL,            "Quick Dial bar full (max %d)!" },
    { MSG_STATUS_QL_NONE,            "No Quick Dial button to remove." },
    { MSG_STATUS_QL_REMOVED_HOME,    "Quick Dial removed: %s (Home loaded)" },
    { MSG_STATUS_QL_REMOVED,         "Quick Dial removed: %s" },
    { MSG_STATUS_CTX_OPENING,        "Opening context menu (%d, %d)..." },
    { MSG_STATUS_OPENING_NEW_TAB,    "Opening new tab: %s" },
    { MSG_STATUS_START_ASL_DL,       "Starting ASL download: %s" },
    { MSG_STATUS_LINK_COPIED,        "Link copied to clipboard: %s" },
    { MSG_STATUS_LINK_BOOKMARKED,    "Link added to bookmarks: %s" },
    { MSG_STATUS_START_MEDIA_DL,     "Starting media download: %s" },
    { MSG_STATUS_PAGE_BOOKMARKED,    "Page added to bookmarks: %s" },
    { MSG_STATUS_START_DL,           "Starting download: %s" },
    { MSG_STATUS_READY_SHORT,        "Ready" },

    { MSG_NET_UPLOAD_TITLE,      "VortexBrowser: Select file to upload..." },
    { MSG_NET_UPLOAD_POS,        "Select" },
    { MSG_NET_UPLOAD_CANCELLED,  "File upload cancelled." },
    { MSG_NET_UPLOAD_OPEN_ERR,   "Error opening file: %s" },
    { MSG_NET_UPLOADING,         "Uploading: %s (%u KB)..." },
    { MSG_NET_UPLOADED,          "File uploaded: %s (%u KB)" },
    { MSG_NET_SAVE_PW_TITLE,     "Save Password" },
    { MSG_NET_SAVE_PW_BODY,      "Do you want to save the password for\n'%s' (User: %s)\nin the secure Password Manager?" },
    { MSG_NET_SAVE_PW_BTNS,      "Save|Not now" },
    { MSG_NET_PW_SAVED,          "Password saved for '%s'." },
    { MSG_DL_SAVE_TITLE,         "VortexBrowser: Save download as..." },
    { MSG_DL_SAVE_POS,           "Save" },
    { MSG_DL_STATUS_RUNNING,     "Running" },
    { MSG_DL_STATUS_DONE,        "Done" },
    { MSG_DL_STATUS_CANCELLED,   "Cancelled" },
    { MSG_DL_STATUS_ERROR,       "Error" },
    { MSG_DL_ETA_CANCELLED,      "Canc." },
    { MSG_FAV_DEFAULT_PORTAL,    "VortexBrowser Portal (Home)" },
    { MSG_FAV_GOOGLE_SEARCH,     "Google Search" },
    { MSG_WEB_PAGE_FALLBACK,     "Website" },

    { MSG_PW_NO_ENTRIES,         "(No saved passwords)" },
    { MSG_PW_BTN_COPY,           "Copy" },
    { MSG_PW_CHK_REVEAL,         "Show password in plain text" },
    { MSG_PW_BTN_AUTOLOGIN,      "Log in on page (Auto-Login)" },
    { MSG_PW_HINT_AUTOLOGIN,     "Automatically fill and submit credentials in the current web form" },
    { MSG_PW_BTN_SAVE,           "Save / New" },
    { MSG_PW_HINT_SAVE,          "Save the above credentials encrypted" },
    { MSG_PW_BTN_DELETE,         "Delete" },
    { MSG_PW_HINT_DELETE,        "Remove selected credentials" },
    { MSG_PW_BTN_CLOSE,          "Close" },
    { MSG_PW_LBL_SAVED,          "Saved Logins:" },
    { MSG_PW_LBL_DOMAIN,         "Website / Domain:" },
    { MSG_PW_LBL_USER,           "Username / E-Mail:" },
    { MSG_PW_LBL_PASS,           "Password:" },
    { MSG_PW_STATUS_SENT,        "Auto-Login sent: %s (%s)" },
    { MSG_PW_STATUS_USER_COPIED, "Username copied to clipboard!" },
    { MSG_PW_STATUS_PASS_COPIED, "Password copied to clipboard!" },
    { MSG_PW_STATUS_SAVED,       "Password saved: %s (%s)" },
    { MSG_PW_STATUS_DELETED,     "Credentials deleted." },
    { MSG_PW_STATUS_NO_PAGE,     "No active website for Auto-Login." },
    { MSG_PW_STATUS_AUTOLOGIN,   "Auto-Login executed for '%s' (%s)..." },
    { MSG_PW_STATUS_NOT_FOUND,   "No login found for domain. Password Manager opened." },

    { MSG_PREFS_PTAB_SERVER,      "Server & Streaming" },
    { MSG_PREFS_PTAB_QUICKDIAL,   "Quick Dial" },
    { MSG_PREFS_PTAB_DISPLAY,     "Display & Resolution" },
    { MSG_PREFS_PTAB_FONT,        "Font & Charset" },
    { MSG_PREFS_PTAB_BOOKMARKS,   "Bookmarks" },
    { MSG_PREFS_PTAB_DOWNLOADS,   "Downloads" },
    { MSG_PREFS_SRV_PRESET_0,     "Secondary Server (127.0.0.1:9757)" },
    { MSG_PREFS_SRV_PRESET_1,     "Default Server (127.0.0.1:7788)" },
    { MSG_PREFS_SRV_PRESET_2,     "Custom (Manual Address)" },
    { MSG_PREFS_SMODE_AUTO,       "Auto (Adaptive Video Optimization: 25-30 FPS YUV420P)" },
    { MSG_PREFS_SMODE_LZ4,        "Dirty-Rect / LZ4 Only (Classic, Minimal Bandwidth)" },
    { MSG_PREFS_SMODE_H264,       "H.264 (Experimental Bitstream)" },
    { MSG_PREFS_SRES_NATIVE,      "1:1 Window Size (Default / Native)" },
    { MSG_PREFS_SRES_720P,        "720p HD (1280x720) [30-50 FPS / Video-Boost]" },
    { MSG_PREFS_SRES_540P,        "540p qHD (960x540) [Turbo]" },
    { MSG_PREFS_SRES_1080P,       "1080p Full HD (1920x1080)" },
    { MSG_PREFS_FPS_15,           "15 FPS (Power Saving / Slow CPU)" },
    { MSG_PREFS_FPS_25,           "25 FPS (Standard / PAL Smooth)" },
    { MSG_PREFS_FPS_30,           "30 FPS (Recommended for YouTube / NTSC)" },
    { MSG_PREFS_FPS_50,           "50 FPS (PAL High-Speed)" },
    { MSG_PREFS_FPS_60,           "60 FPS (Maximum / Gaming / PiStorm)" },
    { MSG_PREFS_SRV_GROUP,        "Streaming Server & Connection Options" },
    { MSG_PREFS_HINT_SRV_PRESET,  "Choose preconfigured server or customize manually" },
    { MSG_PREFS_HINT_SRV_HOST,    "IP address or hostname of the streaming server" },
    { MSG_PREFS_HINT_SRV_PORT,    "TCP port of the streaming server (e.g. 9757 or 7788)" },
    { MSG_PREFS_HINT_STREAM_RES,  "Choose server resolution (scaled via GPU/Composite)" },
    { MSG_PREFS_HINT_FPS,         "Set streaming frame rate (FPS)" },
    { MSG_PREFS_HINT_JPEG_Q,      "JPEG stream quality (Default: 100% = maximum sharpness)" },
    { MSG_PREFS_HINT_STREAM_MODE, "Streaming mode: 'Auto' switches to H.264 via va.library on video" },
    { MSG_PREFS_HINT_BITRATE,     "H.264 video stream bitrate (1000 to 10000 kbps / 10 Mbps, Default: 4000)" },
    { MSG_PREFS_CHK_VIDBOOST,     "Dynamic JPEG quality on video (Video-Boost)" },
    { MSG_PREFS_HINT_VIDBOOST,    "Temporarily adjust quality to 60% during video playback for 30-50 FPS" },
    { MSG_PREFS_CHK_AUTOSAVE_SRV, "Automatically save current server on exit" },
    { MSG_PREFS_HINT_AUTOSAVE_SRV,"Remember last used server for next startup" },
    { MSG_PREFS_LBL_SRV_SELECT,   "Server Selection:" },
    { MSG_PREFS_LBL_SRV_HOST,     "Server IP / Host:" },
    { MSG_PREFS_LBL_SRV_PORT,     "Port:" },
    { MSG_PREFS_LBL_STREAM_RES,   "Stream Resolution:" },
    { MSG_PREFS_LBL_FPS,          "Frame Rate (FPS):" },
    { MSG_PREFS_LBL_JPEG_Q,       "JPEG Quality:" },
    { MSG_PREFS_LBL_VID_STREAM,   "Video Streaming:" },
    { MSG_PREFS_LBL_VID_BITRATE,  "Video Bitrate:" },
    { MSG_PREFS_LBL_VID_OPT,      "Video Optimization:" },
    { MSG_PREFS_LBL_ON_EXIT,      "On Exit:" },
    { MSG_PREFS_SRV_NOTE,         "Default quality is 100% (lossless). 720p/540p increases FPS significantly." },
    { MSG_PREFS_QL_GROUP,         "Quick Dial Bar (Variable Buttons with Icons)" },
    { MSG_PREFS_QL_COL_TITLE,     "Title / Name" },
    { MSG_PREFS_QL_COL_URL,       "Address (URL)" },
    { MSG_PREFS_QL_COL_ICON,      "Icon" },
    { MSG_PREFS_QL_BTN_ADD,       "New / Add" },
    { MSG_PREFS_QL_LBL_TITLE,     "Title / Name:" },
    { MSG_PREFS_QL_LBL_URL,       "Address (URL):" },
    { MSG_PREFS_QL_LBL_ICON,      "AISS Icon Name:" },
    { MSG_PREFS_QL_TIP1,          "Tip: Buttons in the bar have icons and expand automatically." },
    { MSG_PREFS_QL_TIP2,          "New buttons can also be added directly with the [+] toolbar button." },
    { MSG_PREFS_RES_0,            "1024 x 768 (Standard XGA)" },
    { MSG_PREFS_RES_1,            "1280 x 720 (HD 720p)" },
    { MSG_PREFS_RES_2,            "1920 x 1080 (Full HD)" },
    { MSG_PREFS_RES_3,            "800 x 600 (SVGA)" },
    { MSG_PREFS_RES_4,            "1280 x 1024 (SXGA)" },
    { MSG_PREFS_RES_5,            "Fit Automatically (Window Size)" },
    { MSG_PREFS_RES_6,            "Custom (Manual W x H)" },
    { MSG_PREFS_THEME_0,          "Light (Default)" },
    { MSG_PREFS_THEME_1,          "Dark (Dark Mode)" },
    { MSG_PREFS_THEME_2,          "Automatic (System)" },
    { MSG_PREFS_SCR_0,            "Workbench (Window Mode)" },
    { MSG_PREFS_SCR_1,            "Custom Public Screen (Manual Resolution / Fullscreen)" },
    { MSG_PREFS_SCR_2,            "Clone Workbench (Custom Screen like Workbench)" },
    { MSG_PREFS_DISP_GROUP,       "Central Resolution & Screen Mode" },
    { MSG_PREFS_DISP_INTRO,       "One resolution for Screen, Amiga Window and Web Browser:" },
    { MSG_PREFS_LBL_SCR_MODE,     "Screen Mode:" },
    { MSG_PREFS_LBL_RTG_MODE,     "RTG ScreenMode:" },
    { MSG_PREFS_BTN_SCR_ASL,      "Choose Screen Mode (ASL)..." },
    { MSG_PREFS_LBL_RESOLUTION,   "Resolution:" },
    { MSG_PREFS_LBL_CUSTOM_W,     "Custom W:" },
    { MSG_PREFS_LBL_CUSTOM_H,     "H:" },
    { MSG_PREFS_LBL_SCROLL,       "Mouse Wheel & Scrolling:" },
    { MSG_PREFS_CHK_SOFTSCROLL,   "Smooth Scrolling (Softscrolling)" },
    { MSG_PREFS_LBL_STATUSBAR,    "Status Bar:" },
    { MSG_PREFS_CHK_STATUSBAR,    "Show Status Bar (Bottom)" },
    { MSG_PREFS_LBL_THEME,        "Color Scheme (Theme):" },
    { MSG_PREFS_LBL_TAB_MEDIA,    "Tab Media:" },
    { MSG_PREFS_CHK_PAUSE_MEDIA,  "Pause video/audio when switching tabs" },
    { MSG_PREFS_DISP_HINT,        "Note: Toggle between Screen and Workbench anytime with Amiga+M." },
    { MSG_PREFS_GCHAR_0,          "ISO-8859-1 (Western European / Amiga Standard)" },
    { MSG_PREFS_GCHAR_1,          "UTF-8 (Unicode)" },
    { MSG_PREFS_GCHAR_2,          "Windows-1252 (ANSI)" },
    { MSG_PREFS_GCHAR_3,          "ISO-8859-15 (Western Europe with Euro)" },
    { MSG_PREFS_GSIZE_0,          "Default (Workbench System Font)" },
    { MSG_PREFS_GSIZE_1,          "Small (8 pt)" },
    { MSG_PREFS_GSIZE_2,          "Medium (10 pt)" },
    { MSG_PREFS_GSIZE_3,          "Large (12 pt)" },
    { MSG_PREFS_GSIZE_4,          "Very Large (14 pt)" },
    { MSG_PREFS_WCHAR_0,          "UTF-8 (International Standard)" },
    { MSG_PREFS_WCHAR_1,          "ISO-8859-1 (Western European)" },
    { MSG_PREFS_WCHAR_2,          "Windows-1252 (Western Europe / Windows)" },
    { MSG_PREFS_WCHAR_3,          "Auto-Detect" },
    { MSG_PREFS_WSIZE_0,          "16 px (Default)" },
    { MSG_PREFS_WSIZE_1,          "18 px (Large)" },
    { MSG_PREFS_WSIZE_2,          "20 px (Very Large)" },
    { MSG_PREFS_WSIZE_3,          "22 px (Extra Large)" },
    { MSG_PREFS_WSIZE_4,          "24 px (Huge)" },
    { MSG_PREFS_WSIZE_5,          "14 px (Compact)" },
    { MSG_PREFS_WSIZE_6,          "12 px (Small)" },
    { MSG_PREFS_WZOOM_0,          "100% (Normal)" },
    { MSG_PREFS_WZOOM_1,          "110% (Slightly Enlarged)" },
    { MSG_PREFS_WZOOM_2,          "125% (Large - recommended for 1024x768 / HD)" },
    { MSG_PREFS_WZOOM_3,          "150% (Very Large)" },
    { MSG_PREFS_WZOOM_4,          "175% (Extra Large)" },
    { MSG_PREFS_WZOOM_5,          "200% (Maximum)" },
    { MSG_PREFS_WZOOM_6,          "90% (Compact)" },
    { MSG_PREFS_WZOOM_7,          "75% (Very Small)" },
    { MSG_PREFS_WFAM_0,           "Sans-Serif (Default / Grotesk)" },
    { MSG_PREFS_WFAM_1,           "Serif (Antiqua / Book Font)" },
    { MSG_PREFS_WFAM_2,           "Monospace (Fixed Width)" },
    { MSG_PREFS_SE_DDG,           "DuckDuckGo (Default)" },
    { MSG_PREFS_FONT_GROUP,       "Font, Character Set & Search Engine" },
    { MSG_PREFS_HDR_WEB,          "Headless Web Browser (Remote Rendering & Websites):" },
    { MSG_PREFS_LBL_WEB_SIZE,     "Browser Font Size:" },
    { MSG_PREFS_LBL_WEB_ZOOM,     "Web Zoom & Scaling:" },
    { MSG_PREFS_LBL_WEB_FAM,      "Web Font Family:" },
    { MSG_PREFS_LBL_WEB_CHAR,     "Web Charset:" },
    { MSG_PREFS_LBL_SEARCH,       "Default Search Engine:" },
    { MSG_PREFS_HDR_GUI,          "Application / AmigaOS Interface (GUI):" },
    { MSG_PREFS_LBL_GUI_CHAR,     "GUI Charset:" },
    { MSG_PREFS_LBL_GUI_SIZE,     "GUI Font Size:" },
    { MSG_PREFS_FAV_GROUP,        "Bookmark Management" },
    { MSG_PREFS_FAV_LBL_TITLE,    "Title:" },
    { MSG_PREFS_DL_GROUP,         "Download Settings" },
    { MSG_PREFS_DL_HINT_DIR,      "Choose download directory via file requester" },
    { MSG_PREFS_DL_ASK_CHK,       "Always ask for destination drawer (ASL Requester)" },
    { MSG_PREFS_DL_OPEN_BTN,      "Open Download Manager now..." },
    { MSG_PREFS_DL_OPEN_HINT,     "Opens the Download Manager window" },
    { MSG_PREFS_DL_LBL_DIR,       "Default Drawer:" },
    { MSG_PREFS_DL_LBL_ASK,       "File Requester:" },
    { MSG_PREFS_DL_LBL_MGR,       "Download Manager:" },
    { MSG_PREFS_DL_NOTE,          "When prompting is disabled, files are saved directly to the default drawer." },
    { MSG_PREFS_HINT_SAVE,        "Save settings permanently to ENVARC: and ENV: and apply" },
    { MSG_PREFS_HINT_USE,         "Apply settings temporarily for this session" },
    { MSG_PREFS_HINT_CANCEL,      "Discard changes and close window" },
    { MSG_PREFS_ASL_DL_TITLE,     "Select Download Directory" },
    { MSG_PREFS_ASL_DL_POS,       "Select" },
    { MSG_PREFS_ASL_SCR_TITLE,    "Select Screen Mode" },
    { MSG_PREFS_QL_NEW_LINK,      "New Link" },
    { MSG_TAB_FALLBACK_LINK,      "Link" },
};

#define NUM_BUILTINS (sizeof(s_builtins) / sizeof(s_builtins[0]))

CONST_STRPTR GetVString(ULONG id)
{
    CONST_STRPTR def_str = NULL;
    for (size_t i = 0; i < NUM_BUILTINS; i++) {
        if (s_builtins[i].id == id) {
            def_str = s_builtins[i].str;
            break;
        }
    }

    if (!def_str) {
        def_str = "";
    }

    if (ILocale && s_catalog) {
        return ILocale->GetCatalogStr(s_catalog, id, def_str);
    }

    if (s_fallback_entries && s_fallback_count > 0) {
        return fallback_lookup(id, def_str);
    }

    return def_str;
}

void vb_utf8_to_local(const char *utf8_str, char *dst, size_t dst_size)
{
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!utf8_str || utf8_str[0] == '\0') return;

    if (ICodesets) {
        STRPTR res = ICodesets->CodesetsUTF8ToStr(
            CSA_Source,        (Tag)utf8_str,
            CSA_Dest,          (Tag)dst,
            CSA_DestLen,       (Tag)dst_size,
            CSA_AllocIfNeeded, FALSE,
            TAG_DONE);
        if (res) {
            dst[dst_size - 1] = '\0';
            return;
        }
    }

    /* Fallback UTF-8 to ISO-8859-1 (Latin-1) */
    const unsigned char *s = (const unsigned char *)utf8_str;
    size_t d = 0;
    while (*s && d + 1 < dst_size) {
        if (*s < 0x80) {
            dst[d++] = (char)*s++;
        } else if ((*s & 0xE0) == 0xC0 && *(s + 1)) {
            uint32_t cp = ((*s & 0x1F) << 6) | (*(s + 1) & 0x3F);
            s += 2;
            if (cp <= 0xFF) {
                dst[d++] = (char)(unsigned char)cp;
            } else {
                dst[d++] = '?';
            }
        } else if ((*s & 0xF0) == 0xE0 && *(s + 1) && *(s + 2)) {
            uint32_t cp = ((*s & 0x0F) << 12) | ((*(s + 1) & 0x3F) << 6) | (*(s + 2) & 0x3F);
            s += 3;
            if (cp == 0x20AC) {
                dst[d++] = (char)0xA4; /* Euro in ISO-8859-15 */
            } else if (cp == 0x2013 || cp == 0x2014) {
                dst[d++] = '-';
            } else if (cp == 0x2018 || cp == 0x2019) {
                dst[d++] = '\'';
            } else if (cp == 0x201C || cp == 0x201D) {
                dst[d++] = '"';
            } else if (cp == 0x2026) {
                if (d + 3 < dst_size) {
                    dst[d++] = '.'; dst[d++] = '.'; dst[d++] = '.';
                } else {
                    dst[d++] = '.';
                }
            } else {
                dst[d++] = '?';
            }
        } else if ((*s & 0xF8) == 0xF0 && *(s + 1) && *(s + 2) && *(s + 3)) {
            s += 4;
            dst[d++] = '?';
        } else {
            s++;
        }
    }
    dst[d] = '\0';
}

void vb_local_to_utf8(const char *local_str, char *dst, size_t dst_size)
{
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!local_str || local_str[0] == '\0') return;

    if (ICodesets) {
        UTF8 *res = ICodesets->CodesetsUTF8Create(
            CSA_Source,        (Tag)local_str,
            CSA_Dest,          (Tag)dst,
            CSA_DestLen,       (Tag)dst_size,
            CSA_AllocIfNeeded, FALSE,
            TAG_DONE);
        if (res) {
            dst[dst_size - 1] = '\0';
            return;
        }
    }

    /* Fallback ISO-8859-1 to UTF-8 */
    const unsigned char *s = (const unsigned char *)local_str;
    size_t d = 0;
    while (*s && d + 1 < dst_size) {
        if (*s < 0x80) {
            dst[d++] = (char)*s++;
        } else {
            if (d + 2 >= dst_size) break;
            dst[d++] = (char)(0xC0 | (*s >> 6));
            dst[d++] = (char)(0x80 | (*s & 0x3F));
            s++;
        }
    }
    dst[d] = '\0';
}

static char ascii_tolower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static BOOL ascii_eq_ci(const char *a, const char *b)
{
    size_t i;
    if (!a || !b) return FALSE;
    for (i = 0; a[i] != '\0' && b[i] != '\0'; i++) {
        if (ascii_tolower(a[i]) != ascii_tolower(b[i])) return FALSE;
    }
    return a[i] == b[i];
}

/* Normalize a locale language name (e.g. "German_ISO-8859-15", "français", "de_DE")
 * into a lowercase ASCII base name for matching catalog aliases. */
static void normalize_lang_token(const char *src, char *dst, size_t dst_sz)
{
    size_t out = 0;
    const unsigned char *p = (const unsigned char *)src;
    if (!dst || dst_sz == 0) return;
    dst[0] = '\0';
    if (!src) return;

    while (*p && (*p == ' ' || *p == '\t')) p++;

    while (*p && out + 1 < dst_sz) {
        unsigned char c = *p;
        if (c == '.' || c == '@' || c == '/' || c == ',' || c == '\r' || c == '\n' || c == ' ') {
            break;
        }
        /* Handle UTF-8 sequences for ç (C3 A7) and ñ (C3 B1) and other Latin accents */
        if (c == 0xC3 && p[1] != '\0') {
            unsigned char n = p[1];
            if (n == 0xA7 || n == 0x87) {
                dst[out++] = 'c';
                p += 2;
                continue;
            }
            if (n == 0xB1 || n == 0x91) {
                dst[out++] = 'n';
                p += 2;
                continue;
            }
            if (n >= 0xA0 && n <= 0xA5) { dst[out++] = 'a'; p += 2; continue; }
            if (n >= 0xA8 && n <= 0xAB) { dst[out++] = 'e'; p += 2; continue; }
            if (n >= 0xAC && n <= 0xAF) { dst[out++] = 'i'; p += 2; continue; }
            if (n >= 0xB2 && n <= 0xB6) { dst[out++] = 'o'; p += 2; continue; }
            if (n >= 0xB9 && n <= 0xBC) { dst[out++] = 'u'; p += 2; continue; }
        }
        /* Handle ISO-8859-1 accented bytes directly */
        if (c == 0xE7 || c == 0xC7) {
            dst[out++] = 'c';
            p++;
            continue;
        }
        if (c == 0xF1 || c == 0xD1) {
            dst[out++] = 'n';
            p++;
            continue;
        }
        if (c == '_' || c == '-') {
            if (ascii_eq_ci(dst, "de") || ascii_eq_ci(dst, "en") ||
                ascii_eq_ci(dst, "fr") || ascii_eq_ci(dst, "it") ||
                ascii_eq_ci(dst, "es") ||
                ascii_eq_ci(dst, "german") || ascii_eq_ci(dst, "deutsch") ||
                ascii_eq_ci(dst, "english") || ascii_eq_ci(dst, "french") ||
                ascii_eq_ci(dst, "francais") || ascii_eq_ci(dst, "italian") ||
                ascii_eq_ci(dst, "italiano") || ascii_eq_ci(dst, "spanish") ||
                ascii_eq_ci(dst, "espanol")) {
                break;
            }
        }
        dst[out++] = ascii_tolower((char)c);
        p++;
    }
    dst[out] = '\0';
}

static struct Catalog *try_open_catalog_sys(const char *lang_name)
{
    struct TagItem tags[3];
    struct Catalog *cat;
    int n = 0;

    if (!ILocale) return NULL;

    tags[n].ti_Tag  = OC_BuiltInLanguage;
    tags[n].ti_Data = (Tag)"english";
    n++;

    /* Omit OC_BuiltInCodeSet per AmigaOS 4 locale.doc so locale.library
     * converts CSET=4 catalogs to the user's active charset automatically
     * (e.g. ISO-8859-15 id 111) instead of leaving raw charset handling to the app. */
    if (lang_name && lang_name[0]) {
        tags[n].ti_Tag  = OC_Language;
        tags[n].ti_Data = (Tag)lang_name;
        n++;
    }

    tags[n].ti_Tag  = TAG_DONE;
    tags[n].ti_Data = 0;

    cat = ILocale->OpenCatalogA(NULL, "VortexBrowser.catalog", tags);
    if (!cat && s_locale) {
        cat = ILocale->OpenCatalogA(s_locale, "VortexBrowser.catalog", tags);
    }
    return cat;
}

static uint8 *read_amiga_catalog_blob(const char *path, size_t *out_len, int32 *out_err)
{
    BPTR fh;
    uint8 *buf = NULL;
    size_t cap = 0;
    size_t len = 0;
    enum { MAX_CATALOG_BYTES = 512 * 1024 };

    *out_len = 0;
    if (!IDOS || !path || !path[0]) return NULL;

    fh = IDOS->Open(path, MODE_OLDFILE);
    if (!fh) {
        if (out_err) *out_err = IDOS->IoErr();
        return NULL;
    }

    cap = 24576;
    buf = (uint8 *)malloc(cap);
    if (!buf) {
        IDOS->Close(fh);
        return NULL;
    }

    for (;;) {
        int32 avail;
        int32 got;
        if (len == cap) {
            size_t new_cap = cap * 2;
            uint8 *nb;
            if (new_cap > MAX_CATALOG_BYTES) {
                free(buf);
                IDOS->Close(fh);
                return NULL;
            }
            nb = (uint8 *)realloc(buf, new_cap);
            if (!nb) {
                free(buf);
                IDOS->Close(fh);
                return NULL;
            }
            buf = nb;
            cap = new_cap;
        }
        avail = (int32)(cap - len);
        got = IDOS->Read(fh, buf + len, avail);
        if (got < 0) {
            if (out_err) *out_err = IDOS->IoErr();
            free(buf);
            IDOS->Close(fh);
            return NULL;
        }
        if (got == 0) break;
        len += (size_t)got;
    }

    IDOS->Close(fh);
    *out_len = len;
    return buf;
}

static size_t bounded_cstr_len(const uint8 *p, size_t max_len)
{
    size_t n = 0;
    while (n < max_len && p[n] != '\0') n++;
    return n;
}

static BOOL parse_ctlg_blob(const uint8 *blob, size_t blob_len)
{
    size_t pos = 12;
    size_t strs_off = 0;
    size_t strs_size = 0;
    size_t off;
    size_t count = 0;
    size_t pool_bytes = 0;
    FallbackCatalogEntry *entries = NULL;
    char *pool = NULL;
    size_t pool_pos = 0;
    size_t idx = 0;
    char parsed_lang[64];

    if (!blob || blob_len < 12) return FALSE;
    if (memcmp(blob, "FORM", 4) != 0 || memcmp(blob + 8, "CTLG", 4) != 0) return FALSE;

    parsed_lang[0] = '\0';

    while (pos + 8 <= blob_len) {
        uint32 cid = read_be32(blob + pos);
        uint32 csz = read_be32(blob + pos + 4);
        size_t data_off = pos + 8;
        uint32 step;

        if ((size_t)csz > blob_len - data_off) break;

        if (cid == 0x4C414E47u) { /* 'LANG' */
            size_t n = bounded_cstr_len(blob + data_off, csz);
            if (n >= sizeof(parsed_lang)) n = sizeof(parsed_lang) - 1;
            memcpy(parsed_lang, blob + data_off, n);
            parsed_lang[n] = '\0';
        } else if (cid == 0x53545253u) { /* 'STRS' */
            strs_off  = data_off;
            strs_size = (size_t)csz;
        }

        step = csz + (csz & 1u);
        if (step < csz || (size_t)step > blob_len - data_off) break;
        pos = data_off + (size_t)step;
    }

    if (strs_size < 8) return FALSE;

    /* Pass 1: validate STRS records and measure total string storage */
    off = 0;
    while (off + 8 <= strs_size) {
        uint32 slen   = read_be32(blob + strs_off + off + 4);
        uint32 padded = (slen + 3u) & ~3u;
        size_t slen_actual;
        size_t adv;

        if ((size_t)slen > strs_size - (off + 8)) break;
        slen_actual = bounded_cstr_len(blob + strs_off + off + 8, slen);
        if (pool_bytes + slen_actual + 1 < pool_bytes) return FALSE;
        pool_bytes += slen_actual + 1;
        count++;

        if (padded < slen || (size_t)padded > strs_size - (off + 8)) {
            adv = 8 + (size_t)slen;
        } else {
            adv = 8 + (size_t)padded;
        }
        off += adv;
    }

    if (count == 0) return FALSE;

    entries = (FallbackCatalogEntry *)malloc(count * sizeof(FallbackCatalogEntry));
    pool    = (char *)malloc(pool_bytes);
    if (!entries || !pool) {
        free(entries);
        free(pool);
        return FALSE;
    }

    /* Pass 2: copy strings cleanly with guaranteed NUL terminators */
    off = 0;
    while (off + 8 <= strs_size && idx < count) {
        uint32 sid    = read_be32(blob + strs_off + off);
        uint32 slen   = read_be32(blob + strs_off + off + 4);
        uint32 padded = (slen + 3u) & ~3u;
        size_t slen_actual;
        size_t adv;

        if ((size_t)slen > strs_size - (off + 8)) break;
        slen_actual = bounded_cstr_len(blob + strs_off + off + 8, slen);
        memcpy(pool + pool_pos, blob + strs_off + off + 8, slen_actual);
        pool[pool_pos + slen_actual] = '\0';

        entries[idx].id  = sid;
        entries[idx].str = pool + pool_pos;
        pool_pos += slen_actual + 1;
        idx++;

        if (padded < slen || (size_t)padded > strs_size - (off + 8)) {
            adv = 8 + (size_t)slen;
        } else {
            adv = 8 + (size_t)padded;
        }
        off += adv;
    }

    if (idx == 0) {
        free(entries);
        free(pool);
        return FALSE;
    }

    qsort(entries, idx, sizeof(FallbackCatalogEntry), cmp_fallback_entry);

    locale_free_fallback();
    s_fallback_entries = entries;
    s_fallback_count   = idx;
    s_fallback_pool    = pool;
    snprintf(s_fallback_lang, sizeof(s_fallback_lang), "%s", parsed_lang);
    return TRUE;
}

static BOOL try_load_ctlg_direct(const char *lang_name, int32 *out_err)
{
    const char *patterns[] = {
        "PROGDIR:Catalogs/%s/VortexBrowser.catalog",
        "PROGDIR:catalogs/%s/VortexBrowser.catalog",
        "LOCALE:Catalogs/%s/VortexBrowser.catalog",
        "LOCALE:catalogs/%s/VortexBrowser.catalog",
        "Catalogs/%s/VortexBrowser.catalog"
    };
    char path[256];
    size_t i;

    if (!IDOS || !lang_name || !lang_name[0]) return FALSE;

    for (i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
        size_t blob_len = 0;
        uint8 *blob;
        snprintf(path, sizeof(path), patterns[i], lang_name);
        blob = read_amiga_catalog_blob(path, &blob_len, out_err);
        if (blob) {
            BOOL ok = parse_ctlg_blob(blob, blob_len);
            free(blob);
            if (ok) return TRUE;
        }
    }

    return FALSE;
}

static void add_lang_candidate(const char **list, size_t *count, size_t max_count, const char *cand)
{
    size_t i;
    if (!cand || !cand[0] || *count >= max_count) return;
    for (i = 0; i < *count; i++) {
        if (ascii_eq_ci(list[i], cand)) return;
    }
    list[(*count)++] = cand;
}

/* Try resolving one preferred language entry (including OS4 standard vs legacy names).
 * Returns TRUE if resolved (either a catalog was opened or built-in English matched). */
static BOOL try_resolve_language(const char *raw_lang, const char *source_tag, int32 *out_err)
{
    char norm[64];
    const char *candidates[6];
    size_t cand_count = 0;
    size_t i;

    if (!raw_lang || !raw_lang[0]) return FALSE;

    normalize_lang_token(raw_lang, norm, sizeof(norm));
    if (!norm[0]) return FALSE;

    if (ascii_eq_ci(norm, "english") || ascii_eq_ci(norm, "en")) {
        snprintf(s_active_language, sizeof(s_active_language), "english (built-in)");
        return TRUE;
    }

    /* Try canonical catalog directory names before raw token with charset suffix */
    add_lang_candidate(candidates, &cand_count, 6, norm);
    if (ascii_eq_ci(norm, "german") || ascii_eq_ci(norm, "deutsch") || ascii_eq_ci(norm, "de")) {
        add_lang_candidate(candidates, &cand_count, 6, "german");
        add_lang_candidate(candidates, &cand_count, 6, "deutsch");
    } else if (ascii_eq_ci(norm, "french") || ascii_eq_ci(norm, "francais") || ascii_eq_ci(norm, "fr")) {
        add_lang_candidate(candidates, &cand_count, 6, "french");
        add_lang_candidate(candidates, &cand_count, 6, "francais");
    } else if (ascii_eq_ci(norm, "italian") || ascii_eq_ci(norm, "italiano") || ascii_eq_ci(norm, "it")) {
        add_lang_candidate(candidates, &cand_count, 6, "italian");
        add_lang_candidate(candidates, &cand_count, 6, "italiano");
    } else if (ascii_eq_ci(norm, "spanish") || ascii_eq_ci(norm, "espanol") || ascii_eq_ci(norm, "es")) {
        add_lang_candidate(candidates, &cand_count, 6, "spanish");
        add_lang_candidate(candidates, &cand_count, 6, "espanol");
    }
    add_lang_candidate(candidates, &cand_count, 6, raw_lang);

    for (i = 0; i < cand_count; i++) {
        s_catalog = try_open_catalog_sys(candidates[i]);
        if (s_catalog) {
            const char *cat_lang = (const char *)s_catalog->cat_Language;
            const char *name = (cat_lang && cat_lang[0]) ? cat_lang : candidates[i];
            if (source_tag && source_tag[0]) {
                snprintf(s_active_language, sizeof(s_active_language), "%s (%s)", name, source_tag);
            } else {
                snprintf(s_active_language, sizeof(s_active_language), "%s", name);
            }
            return TRUE;
        }
    }

    for (i = 0; i < cand_count; i++) {
        if (try_load_ctlg_direct(candidates[i], out_err)) {
            const char *name = s_fallback_lang[0] ? s_fallback_lang : candidates[i];
            snprintf(s_active_language, sizeof(s_active_language), "%s (%sdirect)",
                     name, (source_tag && source_tag[0]) ? "ENV/" : "");
            return TRUE;
        }
    }

    return FALSE;
}

static size_t trim_trailing_ws(char *buf, int32 len)
{
    size_t n = (len > 0) ? (size_t)len : 0;
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' ||
                     buf[n - 1] == ' '  || buf[n - 1] == '\t')) {
        buf[--n] = '\0';
    }
    return n;
}

void locale_init_catalog(void)
{
    APTR old_win = NULL;
    char env_lang[64];
    char diag_pref[64];
    int32 env_len = -1;
    BOOL resolved = FALSE;
    int32 diag_err = 0;

    diag_pref[0] = '\0';
    locale_free_fallback();
    snprintf(s_active_language, sizeof(s_active_language), "english (built-in)");

    if (!LocaleBase) {
        LocaleBase = IExec->OpenLibrary("locale.library", 50);
        if (!LocaleBase) {
            LocaleBase = IExec->OpenLibrary("locale.library", 38);
        }
    }
    if (LocaleBase && !ILocale) {
        ILocale = (struct LocaleIFace *)IExec->GetInterface(LocaleBase, "main", 1, NULL);
    }

    if (ILocale) {
        s_locale = ILocale->OpenLocale(NULL);
    }

    if (IDOS) old_win = IDOS->SetProcWindow((APTR)-1);

    /* 1. Optional user override via ENV:VortexBrowser/language */
    if (IDOS) {
        env_len = IDOS->GetVar("VortexBrowser/language", env_lang, sizeof(env_lang), 0);
        if (env_len > 0 && trim_trailing_ws(env_lang, env_len) > 0) {
            snprintf(diag_pref, sizeof(diag_pref), "%s", env_lang);
            resolved = try_resolve_language(env_lang, "ENV", &diag_err);
        }
    }

    /* 2. Canonical OS4 OpenCatalogA without OC_Language or OC_BuiltInCodeSet */
    if (!resolved && ILocale) {
        s_catalog = try_open_catalog_sys(NULL);
        if (s_catalog) {
            const char *cat_lang = (const char *)s_catalog->cat_Language;
            snprintf(s_active_language, sizeof(s_active_language), "%s",
                     (cat_lang && cat_lang[0]) ? cat_lang : "catalog");
            resolved = TRUE;
        }
    }

    /* 3. Walk s_locale->loc_PrefLanguages[0..9] without aborting on NULL holes */
    if (!resolved && s_locale) {
        int i;
        for (i = 0; i < 10; i++) {
            const char *pref = (const char *)s_locale->loc_PrefLanguages[i];
            if (!pref || !pref[0]) continue;
            if (!diag_pref[0]) {
                snprintf(diag_pref, sizeof(diag_pref), "%s", pref);
            }
            if (try_resolve_language(pref, NULL, &diag_err)) {
                resolved = TRUE;
                break;
            }
        }
    }

    /* 4. Fallback system sources: loc_LanguageName and ENV:LanguageName / ENV:Language */
    if (!resolved && s_locale && s_locale->loc_LanguageName && s_locale->loc_LanguageName[0]) {
        const char *lname = (const char *)s_locale->loc_LanguageName;
        if (!diag_pref[0]) {
            snprintf(diag_pref, sizeof(diag_pref), "%s", lname);
        }
        resolved = try_resolve_language(lname, NULL, &diag_err);
    }

    if (!resolved && IDOS) {
        const char *env_vars[2] = { "LanguageName", "Language" };
        size_t i;
        for (i = 0; i < 2 && !resolved; i++) {
            env_len = IDOS->GetVar(env_vars[i], env_lang, sizeof(env_lang), 0);
            if (env_len > 0 && trim_trailing_ws(env_lang, env_len) > 0) {
                if (!diag_pref[0]) {
                    snprintf(diag_pref, sizeof(diag_pref), "%s", env_lang);
                }
                resolved = try_resolve_language(env_lang, NULL, &diag_err);
            }
        }
    }

    if (!resolved && diag_pref[0]) {
        snprintf(s_active_language, sizeof(s_active_language),
                 "english (built-in; pref=%.24s, err=%ld)", diag_pref, (long)diag_err);
    }

    if (IDOS) IDOS->SetProcWindow(old_win);

    DBG_LOG("[Locale] Active language: %s\n", s_active_language);

    if (!CodesetsBase) {
        CodesetsBase = IExec->OpenLibrary("codesets.library", 6);
        if (!CodesetsBase) {
            CodesetsBase = IExec->OpenLibrary("codesets.library", 5);
        }
    }
    if (CodesetsBase && !ICodesets) {
        ICodesets = (struct CodesetsIFace *)IExec->GetInterface(CodesetsBase, "main", 1, NULL);
    }
}

const char *locale_get_active_language(void)
{
    return s_active_language;
}

void locale_close_catalog(void)
{
    locale_free_fallback();
    if (ICodesets) {
        IExec->DropInterface((struct Interface *)ICodesets);
        ICodesets = NULL;
    }
    if (CodesetsBase) {
        IExec->CloseLibrary(CodesetsBase);
        CodesetsBase = NULL;
    }
    if (ILocale && s_catalog) {
        ILocale->CloseCatalog(s_catalog);
        s_catalog = NULL;
    }
    if (ILocale && s_locale) {
        ILocale->CloseLocale(s_locale);
        s_locale = NULL;
    }
    if (ILocale) {
        IExec->DropInterface((struct Interface *)ILocale);
        ILocale = NULL;
    }
    if (LocaleBase) {
        IExec->CloseLibrary(LocaleBase);
        LocaleBase = NULL;
    }
}
