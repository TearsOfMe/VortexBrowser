#ifndef LOCALE_STRINGS_H
#define LOCALE_STRINGS_H

#include <exec/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Message IDs */
#define MSG_WIN_TITLE           1
#define MSG_DL_WIN_TITLE        2
#define MSG_PW_WIN_TITLE        3
#define MSG_PREFS_WIN_TITLE     4

#define MSG_MENU_PROJECT        10
#define MSG_MENU_OPEN_URL       11
#define MSG_MENU_OPEN_FILE      12
#define MSG_MENU_SAVE_SOURCE    13
#define MSG_MENU_PRINT          14
#define MSG_MENU_ABOUT          15
#define MSG_MENU_QUIT           16

#define MSG_MENU_NAVIGATION     20
#define MSG_MENU_BACK           21
#define MSG_MENU_FORWARD        22
#define MSG_MENU_RELOAD         23
#define MSG_MENU_STOP           24
#define MSG_MENU_HOME           25
#define MSG_MENU_TAB_NEW        26
#define MSG_MENU_TAB_CLOSE      27

#define MSG_MENU_BOOKMARKS      30
#define MSG_MENU_BOOKMARKS_BAR  31
#define MSG_MENU_ADD_BOOKMARK   32
#define MSG_MENU_ADD_QUICKLINK  33

#define MSG_MENU_VIEW           40
#define MSG_MENU_ZOOM_IN        41
#define MSG_MENU_ZOOM_OUT       42
#define MSG_MENU_ZOOM_RESET     43
#define MSG_MENU_FULLSCREEN     44
#define MSG_MENU_DARK_MODE      45

#define MSG_MENU_SETTINGS       50
#define MSG_MENU_PREFERENCES    51
#define MSG_MENU_PASSWORDS      52
#define MSG_MENU_DOWNLOADS      53

#define MSG_MENU_EDIT           60
#define MSG_MENU_CUT            61
#define MSG_MENU_COPY           62
#define MSG_MENU_PASTE          63

#define MSG_BTN_BACK            100
#define MSG_BTN_FORWARD         101
#define MSG_BTN_RELOAD          102
#define MSG_BTN_STOP            103
#define MSG_BTN_HOME            104
#define MSG_BTN_GO              105
#define MSG_BTN_DOWNLOADS       106
#define MSG_BTN_CONNECT         107
#define MSG_BTN_DISCONNECT      108
#define MSG_BTN_CLEAR           109
#define MSG_CHOOSER_BOOKMARKS   110

#define MSG_HINT_BACK           120
#define MSG_HINT_FORWARD        121
#define MSG_HINT_RELOAD         122
#define MSG_HINT_STOP           123
#define MSG_HINT_HOME           124
#define MSG_HINT_CLEAR_URL      125
#define MSG_HINT_GO             126
#define MSG_HINT_DOWNLOADS      127
#define MSG_HINT_CONNECT        128
#define MSG_HINT_ADD_QL         129
#define MSG_HINT_REM_QL         130
#define MSG_HINT_NEW_TAB        131
#define MSG_HINT_CLOSE_TAB      132

#define MSG_DL_COL_FILE         200
#define MSG_DL_COL_STATUS       201
#define MSG_DL_COL_SIZE         202
#define MSG_DL_COL_PERCENT      203
#define MSG_DL_COL_SPEED        204
#define MSG_DL_COL_ETA          205
#define MSG_DL_COL_TARGET       206

#define MSG_DL_STATUS_NONE      210
#define MSG_DL_STATUS_ACTIVE    211
#define MSG_DL_STATUS_ALL_DONE  212

#define MSG_DL_BTN_OPEN_FILE    220
#define MSG_DL_HINT_OPEN_FILE   221
#define MSG_DL_BTN_OPEN_DIR     222
#define MSG_DL_HINT_OPEN_DIR    223
#define MSG_DL_BTN_CANCEL       224
#define MSG_DL_HINT_CANCEL      225
#define MSG_DL_BTN_REMOVE       226
#define MSG_DL_HINT_REMOVE      227
#define MSG_DL_BTN_CLEANUP      228
#define MSG_DL_HINT_CLEANUP     229
#define MSG_DL_BTN_CLOSE        230
#define MSG_DL_HINT_CLOSE       231

#define MSG_STATUS_READY        300
#define MSG_STATUS_CONNECTING   301
#define MSG_STATUS_CONNECTED    302
#define MSG_STATUS_DISCONNECTED 303
#define MSG_STATUS_LOADING      304
#define MSG_SEARCH_PROMPT       310

#define MSG_CTX_OPEN_LINK       400
#define MSG_CTX_OPEN_NEW_TAB    401
#define MSG_CTX_FAV_LINK        402
#define MSG_CTX_DOWNLOAD_LINK   403
#define MSG_CTX_COPY_LINK       404
#define MSG_CTX_COPY_IMAGE_URL  405
#define MSG_CTX_DOWNLOAD_IMAGE  406
#define MSG_CTX_MAIN_MENU       407

#define MSG_GEN_YES             500
#define MSG_GEN_NO              501
#define MSG_GEN_OK              502
#define MSG_GEN_CANCEL          503

#define MSG_PREFS_SAVE          510
#define MSG_PREFS_USE           511
#define MSG_PREFS_CANCEL        512
#define MSG_PREFS_TAB_SERVER    520
#define MSG_PREFS_TAB_DISPLAY   521
#define MSG_PREFS_TAB_BROWSER   522
#define MSG_PREFS_TAB_QUICKDIAL 523
#define MSG_PREFS_TAB_BOOKMARKS 524
#define MSG_PREFS_TAB_DOWNLOADS 525
#define MSG_PREFS_TAB_SYSTEM    526
#define MSG_PREFS_TAB_ABOUT     527
#define MSG_PREFS_ADD           530
#define MSG_PREFS_UPDATE        531
#define MSG_PREFS_REMOVE        532
#define MSG_PREFS_MOVE_UP       533
#define MSG_PREFS_MOVE_DOWN     534
#define MSG_PREFS_CHOOSE_DIR    535

/* Get localized string or built-in default */
CONST_STRPTR GetVString(ULONG id);

/* Character encoding translation helpers (codesets.library with ISO-8859-1 fallback) */
void vb_utf8_to_local(const char *utf8_str, char *dst, size_t dst_size);
void vb_local_to_utf8(const char *local_str, char *dst, size_t dst_size);

/* Initialize and close locale / catalog support */
void locale_init_catalog(void);
void locale_close_catalog(void);

#ifdef __cplusplus
}
#endif

#endif /* LOCALE_STRINGS_H */
