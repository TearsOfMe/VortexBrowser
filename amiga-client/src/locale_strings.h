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

/* Extended Menu, Popup, Search & Dialogs */
#define MSG_MENU_NEW_HOME          600
#define MSG_MENU_SERVER_CFG        601
#define MSG_MENU_SERVER_CONNECT    602
#define MSG_MENU_SERVER_DISCONNECT 603
#define MSG_MENU_STREAM_RES_NATIVE 604
#define MSG_MENU_STREAM_RES_720P   605
#define MSG_MENU_STREAM_RES_540P   606
#define MSG_MENU_STREAM_RES_1080P  607
#define MSG_MENU_FPS_15            608
#define MSG_MENU_FPS_25            609
#define MSG_MENU_FPS_30            610
#define MSG_MENU_FPS_50            611
#define MSG_MENU_FPS_60            612
#define MSG_MENU_VIDEO_AUTO        613
#define MSG_MENU_VIDEO_LZ4         614
#define MSG_MENU_VIDEO_H264        615
#define MSG_MENU_TOGGLE_DARK       616
#define MSG_MENU_TOGGLE_SCREEN     617
#define MSG_MENU_TOGGLE_STATUSBAR  618
#define MSG_POPUP_MAIN_TITLE       619
#define MSG_POPUP_SAVE_SETTINGS    620
#define MSG_POPUP_OPT_TITLE        621
#define MSG_POPUP_OPT_PROMPT       622
#define MSG_POPUP_OPT_BUTTONS      623
#define MSG_BTN_SEARCH             624
#define MSG_HINT_FAVICON           625
#define MSG_HINT_URL               626
#define MSG_HINT_SEARCH_CHOOSER    627
#define MSG_HINT_SEARCH_INPUT      628
#define MSG_HINT_SEARCH_GO         629

/* QuickLink Popup, Context Menu & Server Dialog */
#define MSG_QL_POPUP_TITLE_FMT     630
#define MSG_QL_POPUP_OPEN_CUR      631
#define MSG_QL_POPUP_OPEN_NEW      632
#define MSG_QL_POPUP_COPY_URL      633
#define MSG_QL_POPUP_EDIT          634
#define MSG_QL_POPUP_REMOVE        635
#define MSG_QL_POPUP_REQ_TEXT      636
#define MSG_QL_POPUP_REQ_BTNS      637
#define MSG_QL_ADD_REQ_TITLE       638
#define MSG_QL_ADD_REQ_TEXT        639
#define MSG_QL_ADD_REQ_BTNS        640
#define MSG_CTX_HDR_LINK           641
#define MSG_CTX_HDR_MEDIA          642
#define MSG_CTX_OPEN_MEDIA         643
#define MSG_CTX_FAV_PAGE           644
#define MSG_CTX_DL_PAGE            645
#define MSG_CTX_DARK_ON            646
#define MSG_CTX_LIGHT_ON           647
#define MSG_CTX_DARK_MODE_FULL     648
#define MSG_CTX_LIGHT_MODE_FULL    649
#define MSG_CTX_REQ_TITLE          650
#define MSG_CTX_REQ_LINK_TEXT      651
#define MSG_CTX_REQ_LINK_BTNS      652
#define MSG_CTX_REQ_PAGE_TEXT      653
#define MSG_CTX_REQ_PAGE_BTNS      654
#define MSG_SRV_DLG_TITLE          655
#define MSG_SRV_DLG_SCRTITLE       656
#define MSG_SRV_DLG_PROMPT         657
#define MSG_SRV_DLG_HINT           658
#define MSG_SRV_DLG_CONNECT        659

/* About Requester & CPU Compatibility */
#define MSG_ABOUT_CONNECTED        660
#define MSG_ABOUT_DISCONNECTED     661
#define MSG_ABOUT_YUV_HW           662
#define MSG_ABOUT_YUV_SW           663
#define MSG_ABOUT_SCR_CUSTOM       664
#define MSG_ABOUT_SCR_WB           665
#define MSG_ABOUT_ALTIVEC_YES      666
#define MSG_ABOUT_ALTIVEC_NO       667
#define MSG_ABOUT_SEC_SERVER       668
#define MSG_ABOUT_SEC_GRAPHICS     669
#define MSG_ABOUT_SEC_AUDIO        670
#define MSG_ABOUT_TITLE            671
#define MSG_CPU_INCOMPAT_TITLE     672
#define MSG_CPU_INCOMPAT_BODY      673

/* Extended Status Bar & Actions */
#define MSG_STATUS_READY_STREAM       685
#define MSG_STATUS_CONNECTED_STREAM   686
#define MSG_STATUS_CONN_SYNC          687
#define MSG_STATUS_CONN_FMT           688
#define MSG_STATUS_CONN_H264          689
#define MSG_STATUS_CONN_LZ4           690
#define MSG_STATUS_NO_BSDSOCKET       691
#define MSG_STATUS_NOT_CONNECTED      692
#define MSG_STATUS_HOST_RESOLVE_ERR   693
#define MSG_STATUS_SOCKET_ERR         694
#define MSG_STATUS_CONN_FAILED        695
#define MSG_STATUS_PAGE_LOADED        696
#define MSG_STATUS_LOADING_PAGE       697
#define MSG_STATUS_LOADING_STOPPED    698
#define MSG_STATUS_CREDENTIALS_FILLED 699
#define MSG_STATUS_CLIPBOARD_COPIED   700
#define MSG_STATUS_SEARCH_ENGINE_SEL  701
#define MSG_STATUS_BOOKMARK_ADDED     702
#define MSG_STATUS_BOOKMARK_SAVED     703
#define MSG_STATUS_SETTINGS_SAVED     704
#define MSG_STATUS_STREAM_RES_NATIVE  705
#define MSG_STATUS_STREAM_RES_720P    706
#define MSG_STATUS_STREAM_RES_540P    707
#define MSG_STATUS_STREAM_RES_1080P   708
#define MSG_STATUS_FPS_SET            709
#define MSG_STATUS_THEME_SET          710
#define MSG_STATUS_THEME_DARK         711
#define MSG_STATUS_THEME_LIGHT        712
#define MSG_STATUS_STREAM_AUTO        713
#define MSG_STATUS_STREAM_LZ4         714
#define MSG_STATUS_STREAM_H264        715
#define MSG_STATUS_FONT_SIZE          716
#define MSG_STATUS_FONT_RESET         717
#define MSG_STATUS_RES_CUSTOM         718
#define MSG_STATUS_RES_AUTO           719
#define MSG_STATUS_MAX_TABS           720
#define MSG_STATUS_TAB_SWITCH         721
#define MSG_STATUS_SCREEN_ACTIVE      722
#define MSG_STATUS_SCREEN_CUSTOM      723
#define MSG_STATUS_QL_STARTED         724
#define MSG_STATUS_QL_COPIED          725
#define MSG_STATUS_QL_ADDED           726
#define MSG_STATUS_QL_FULL            727
#define MSG_STATUS_QL_NONE            728
#define MSG_STATUS_QL_REMOVED_HOME    729
#define MSG_STATUS_QL_REMOVED         730
#define MSG_STATUS_CTX_OPENING        731
#define MSG_STATUS_OPENING_NEW_TAB    732
#define MSG_STATUS_START_ASL_DL       733
#define MSG_STATUS_LINK_COPIED        734
#define MSG_STATUS_LINK_BOOKMARKED    735
#define MSG_STATUS_START_MEDIA_DL     736
#define MSG_STATUS_PAGE_BOOKMARKED    737
#define MSG_STATUS_START_DL           738
#define MSG_STATUS_READY_SHORT        739

/* Network, Download & Favorites */
#define MSG_NET_UPLOAD_TITLE      740
#define MSG_NET_UPLOAD_POS        741
#define MSG_NET_UPLOAD_CANCELLED  742
#define MSG_NET_UPLOAD_OPEN_ERR   743
#define MSG_NET_UPLOADING         744
#define MSG_NET_UPLOADED          745
#define MSG_NET_SAVE_PW_TITLE     746
#define MSG_NET_SAVE_PW_BODY      747
#define MSG_NET_SAVE_PW_BTNS      748
#define MSG_NET_PW_SAVED          749
#define MSG_DL_SAVE_TITLE         750
#define MSG_DL_SAVE_POS           751
#define MSG_DL_STATUS_RUNNING     752
#define MSG_DL_STATUS_DONE        753
#define MSG_DL_STATUS_CANCELLED   754
#define MSG_DL_STATUS_ERROR       755
#define MSG_DL_ETA_CANCELLED      756
#define MSG_FAV_DEFAULT_PORTAL    757
#define MSG_FAV_GOOGLE_SEARCH     758
#define MSG_WEB_PAGE_FALLBACK     759

/* Password Manager GUI */
#define MSG_PW_NO_ENTRIES         770
#define MSG_PW_BTN_COPY           771
#define MSG_PW_CHK_REVEAL         772
#define MSG_PW_BTN_AUTOLOGIN      773
#define MSG_PW_HINT_AUTOLOGIN     774
#define MSG_PW_BTN_SAVE           775
#define MSG_PW_HINT_SAVE          776
#define MSG_PW_BTN_DELETE         777
#define MSG_PW_HINT_DELETE        778
#define MSG_PW_BTN_CLOSE          779
#define MSG_PW_LBL_SAVED          780
#define MSG_PW_LBL_DOMAIN         781
#define MSG_PW_LBL_USER           782
#define MSG_PW_LBL_PASS           783
#define MSG_PW_STATUS_SENT        784
#define MSG_PW_STATUS_USER_COPIED 785
#define MSG_PW_STATUS_PASS_COPIED 786
#define MSG_PW_STATUS_SAVED       787
#define MSG_PW_STATUS_DELETED     788
#define MSG_PW_STATUS_NO_PAGE     789
#define MSG_PW_STATUS_AUTOLOGIN   790
#define MSG_PW_STATUS_NOT_FOUND   791

/* Preferences Window */
#define MSG_PREFS_PTAB_SERVER     800
#define MSG_PREFS_PTAB_QUICKDIAL  801
#define MSG_PREFS_PTAB_DISPLAY    802
#define MSG_PREFS_PTAB_FONT       803
#define MSG_PREFS_PTAB_BOOKMARKS  804
#define MSG_PREFS_PTAB_DOWNLOADS  805
#define MSG_PREFS_SRV_PRESET_0    806
#define MSG_PREFS_SRV_PRESET_1    807
#define MSG_PREFS_SRV_PRESET_2    808
#define MSG_PREFS_SMODE_AUTO      809
#define MSG_PREFS_SMODE_LZ4       810
#define MSG_PREFS_SMODE_H264      811
#define MSG_PREFS_SRES_NATIVE     812
#define MSG_PREFS_SRES_720P       813
#define MSG_PREFS_SRES_540P       814
#define MSG_PREFS_SRES_1080P      815
#define MSG_PREFS_FPS_15          816
#define MSG_PREFS_FPS_25          817
#define MSG_PREFS_FPS_30          818
#define MSG_PREFS_FPS_50          819
#define MSG_PREFS_FPS_60          820
#define MSG_PREFS_SRV_GROUP       821
#define MSG_PREFS_HINT_SRV_PRESET 822
#define MSG_PREFS_HINT_SRV_HOST   823
#define MSG_PREFS_HINT_SRV_PORT   824
#define MSG_PREFS_HINT_STREAM_RES 825
#define MSG_PREFS_HINT_FPS        826
#define MSG_PREFS_HINT_JPEG_Q     827
#define MSG_PREFS_HINT_STREAM_MODE 828
#define MSG_PREFS_HINT_BITRATE    829
#define MSG_PREFS_CHK_VIDBOOST    830
#define MSG_PREFS_HINT_VIDBOOST   831
#define MSG_PREFS_CHK_AUTOSAVE_SRV 832
#define MSG_PREFS_HINT_AUTOSAVE_SRV 833
#define MSG_PREFS_LBL_SRV_SELECT  834
#define MSG_PREFS_LBL_SRV_HOST    835
#define MSG_PREFS_LBL_SRV_PORT    836
#define MSG_PREFS_LBL_STREAM_RES  837
#define MSG_PREFS_LBL_FPS         838
#define MSG_PREFS_LBL_JPEG_Q      839
#define MSG_PREFS_LBL_VID_STREAM  840
#define MSG_PREFS_LBL_VID_BITRATE 841
#define MSG_PREFS_LBL_VID_OPT     842
#define MSG_PREFS_LBL_ON_EXIT     843
#define MSG_PREFS_SRV_NOTE        844
#define MSG_PREFS_QL_GROUP        845
#define MSG_PREFS_QL_COL_TITLE    846
#define MSG_PREFS_QL_COL_URL      847
#define MSG_PREFS_QL_COL_ICON     848
#define MSG_PREFS_QL_BTN_ADD      849
#define MSG_PREFS_QL_LBL_TITLE    850
#define MSG_PREFS_QL_LBL_URL      851
#define MSG_PREFS_QL_LBL_ICON     852
#define MSG_PREFS_QL_TIP1         853
#define MSG_PREFS_QL_TIP2         854
#define MSG_PREFS_RES_0           860
#define MSG_PREFS_RES_1           861
#define MSG_PREFS_RES_2           862
#define MSG_PREFS_RES_3           863
#define MSG_PREFS_RES_4           864
#define MSG_PREFS_RES_5           865
#define MSG_PREFS_RES_6           866
#define MSG_PREFS_THEME_0         867
#define MSG_PREFS_THEME_1         868
#define MSG_PREFS_THEME_2         869
#define MSG_PREFS_SCR_0           870
#define MSG_PREFS_SCR_1           871
#define MSG_PREFS_SCR_2           872
#define MSG_PREFS_DISP_GROUP      873
#define MSG_PREFS_DISP_INTRO      874
#define MSG_PREFS_LBL_SCR_MODE    875
#define MSG_PREFS_LBL_RTG_MODE    876
#define MSG_PREFS_BTN_SCR_ASL     877
#define MSG_PREFS_LBL_RESOLUTION  878
#define MSG_PREFS_LBL_CUSTOM_W    879
#define MSG_PREFS_LBL_CUSTOM_H    880
#define MSG_PREFS_LBL_SCROLL      881
#define MSG_PREFS_CHK_SOFTSCROLL  882
#define MSG_PREFS_LBL_STATUSBAR   883
#define MSG_PREFS_CHK_STATUSBAR   884
#define MSG_PREFS_LBL_THEME       885
#define MSG_PREFS_LBL_TAB_MEDIA   886
#define MSG_PREFS_CHK_PAUSE_MEDIA 887
#define MSG_PREFS_DISP_HINT       888
#define MSG_PREFS_GCHAR_0         890
#define MSG_PREFS_GCHAR_1         891
#define MSG_PREFS_GCHAR_2         892
#define MSG_PREFS_GCHAR_3         893
#define MSG_PREFS_GSIZE_0         894
#define MSG_PREFS_GSIZE_1         895
#define MSG_PREFS_GSIZE_2         896
#define MSG_PREFS_GSIZE_3         897
#define MSG_PREFS_GSIZE_4         898
#define MSG_PREFS_WCHAR_0         899
#define MSG_PREFS_WCHAR_1         900
#define MSG_PREFS_WCHAR_2         901
#define MSG_PREFS_WCHAR_3         902
#define MSG_PREFS_WSIZE_0         903
#define MSG_PREFS_WSIZE_1         904
#define MSG_PREFS_WSIZE_2         905
#define MSG_PREFS_WSIZE_3         906
#define MSG_PREFS_WSIZE_4         907
#define MSG_PREFS_WSIZE_5         908
#define MSG_PREFS_WSIZE_6         909
#define MSG_PREFS_WZOOM_0         910
#define MSG_PREFS_WZOOM_1         911
#define MSG_PREFS_WZOOM_2         912
#define MSG_PREFS_WZOOM_3         913
#define MSG_PREFS_WZOOM_4         914
#define MSG_PREFS_WZOOM_5         915
#define MSG_PREFS_WZOOM_6         916
#define MSG_PREFS_WZOOM_7         917
#define MSG_PREFS_WFAM_0          918
#define MSG_PREFS_WFAM_1          919
#define MSG_PREFS_WFAM_2          920
#define MSG_PREFS_SE_DDG          921
#define MSG_PREFS_FONT_GROUP      922
#define MSG_PREFS_HDR_WEB         923
#define MSG_PREFS_LBL_WEB_SIZE    924
#define MSG_PREFS_LBL_WEB_ZOOM    925
#define MSG_PREFS_LBL_WEB_FAM     926
#define MSG_PREFS_LBL_WEB_CHAR    927
#define MSG_PREFS_LBL_SEARCH      928
#define MSG_PREFS_HDR_GUI         929
#define MSG_PREFS_LBL_GUI_CHAR    930
#define MSG_PREFS_LBL_GUI_SIZE    931
#define MSG_PREFS_FAV_GROUP       932
#define MSG_PREFS_FAV_LBL_TITLE   933
#define MSG_PREFS_DL_GROUP        934
#define MSG_PREFS_DL_HINT_DIR     935
#define MSG_PREFS_DL_ASK_CHK      936
#define MSG_PREFS_DL_OPEN_BTN     937
#define MSG_PREFS_DL_OPEN_HINT    938
#define MSG_PREFS_DL_LBL_DIR      939
#define MSG_PREFS_DL_LBL_ASK      940
#define MSG_PREFS_DL_LBL_MGR      941
#define MSG_PREFS_DL_NOTE         942
#define MSG_PREFS_HINT_SAVE       943
#define MSG_PREFS_HINT_USE        944
#define MSG_PREFS_HINT_CANCEL     945
#define MSG_PREFS_ASL_DL_TITLE    946
#define MSG_PREFS_ASL_DL_POS      947
#define MSG_PREFS_ASL_SCR_TITLE   948
#define MSG_PREFS_QL_NEW_LINK     949
#define MSG_TAB_FALLBACK_LINK     950

/* Get localized string or built-in default */
CONST_STRPTR GetVString(ULONG id);

/* Active catalog language name for diagnostics / About requester */
const char *locale_get_active_language(void);

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
