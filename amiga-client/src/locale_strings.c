#include "locale_strings.h"
#include <proto/exec.h>
#include <proto/locale.h>
#include <proto/codesets.h>
#include <libraries/codesets.h>
#include <proto/dos.h>
#include <libraries/locale.h>
#include <stddef.h>
#include <stdint.h>

struct Library        *LocaleBase   = NULL;
struct LocaleIFace    *ILocale      = NULL;
struct Library        *CodesetsBase = NULL;
struct CodesetsIFace  *ICodesets    = NULL;
static struct Catalog *s_catalog    = NULL;
static struct Locale  *s_locale     = NULL;

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

void locale_init_catalog(void)
{
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
        /* Try opening VortexBrowser.catalog */
        s_catalog = ILocale->OpenCatalog(s_locale, "VortexBrowser.catalog",
            OC_BuiltInLanguage, (Tag)"english",
            OC_BuiltInCodeSet,  4,
            TAG_DONE);
        if (!s_catalog) {
            /* Try PROGDIR:catalogs/ if not in system LOCALE: path */
            s_catalog = ILocale->OpenCatalog(s_locale, "PROGDIR:catalogs/VortexBrowser.catalog",
                OC_BuiltInLanguage, (Tag)"english",
                OC_BuiltInCodeSet,  4,
                TAG_DONE);
        }
    }

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

void locale_close_catalog(void)
{
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
