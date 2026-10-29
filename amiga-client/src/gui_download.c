#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <proto/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/listbrowser.h>
#include <gadgets/listbrowser.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <classes/window.h>

#include "app.h"
#include "download.h"
#include "gui_download.h"

extern struct ListBrowserIFace *IListBrowser;

enum {
    GID_DL_LIST = 300,
    GID_DL_STATUS_TXT,
    GID_DL_BTN_OPEN_FILE,
    GID_DL_BTN_OPEN_DIR,
    GID_DL_BTN_CANCEL,
    GID_DL_BTN_REMOVE,
    GID_DL_BTN_CLEAR,
    GID_DL_BTN_CLOSE
};

static void format_size(uint64_t bytes, char *out, size_t out_len)
{
    if (bytes >= (1048576ULL * 1024ULL)) {
        snprintf(out, out_len, "%.2f GB", (double)bytes / (1048576.0 * 1024.0));
    } else if (bytes >= 1048576ULL) {
        snprintf(out, out_len, "%.1f MB", (double)bytes / 1048576.0);
    } else if (bytes >= 1024ULL) {
        snprintf(out, out_len, "%.1f KB", (double)bytes / 1024.0);
    } else {
        snprintf(out, out_len, "%llu B", (unsigned long long)bytes);
    }
}

static void format_speed(uint32_t bps, char *out, size_t out_len)
{
    if (bps >= 1048576) {
        snprintf(out, out_len, "%.1f MB/s", (double)bps / 1048576.0);
    } else if (bps >= 1024) {
        snprintf(out, out_len, "%.1f KB/s", (double)bps / 1024.0);
    } else {
        snprintf(out, out_len, "%u B/s", (unsigned)bps);
    }
}

static void open_file(const char *path)
{
    if (!path || path[0] == '\0') return;
    char cmd[600];
    snprintf(cmd, sizeof(cmd), "WBRun \"%s\"", path);
    DBG_LOG("[Download] Executing file: %s\n", cmd);
    IDOS->SystemTags(cmd, SYS_Asynch, TRUE, TAG_DONE);
}

static void open_drawer(const char *path)
{
    if (!path || path[0] == '\0') return;
    char drawer[512];
    snprintf(drawer, sizeof(drawer), "%s", path);

    CONST_STRPTR fp = IDOS->FilePart(drawer);
    if (fp && fp > drawer) {
        drawer[fp - drawer] = '\0';
    } else {
        char *colon = strchr(drawer, ':');
        if (colon) *(colon + 1) = '\0';
    }

    char cmd[600];
    snprintf(cmd, sizeof(cmd), "WBRun \"%s\"", drawer[0] ? drawer : "RAM:");
    DBG_LOG("[Download] Opening drawer: %s\n", cmd);
    IDOS->SystemTags(cmd, SYS_Asynch, TRUE, TAG_DONE);
}

static void update_button_states(VortexApp *app)
{
    if (!app || !app->dl_win || !app->dl_gad_listbrowser || !IListBrowser) return;

    struct Node *sel_node = NULL;
    IIntuition->GetAttr(LISTBROWSER_SelectedNode, app->dl_gad_listbrowser, (uint32 *)&sel_node);

    DownloadEntry *e = NULL;
    if (sel_node) {
        APTR ud = NULL;
        IListBrowser->GetListBrowserNodeAttrs(sel_node, LBNA_UserData, &ud, TAG_DONE);
        e = (DownloadEntry *)ud;
    }

    BOOL can_open_file = (e != NULL && e->status == DL_STATUS_COMPLETED && e->dest_path[0] != '\0');
    BOOL can_open_dir  = (e != NULL && e->dest_path[0] != '\0');
    BOOL can_cancel    = (e != NULL && e->status == DL_STATUS_DOWNLOADING);
    BOOL can_remove    = (e != NULL);

    BOOL has_completed = FALSE;
    for (int i = 0; i < app->download_mgr.count; i++) {
        if (app->download_mgr.entries[i].status != DL_STATUS_DOWNLOADING) {
            has_completed = TRUE;
            break;
        }
    }

    if (app->dl_gad_btn_open_file) {
        IIntuition->SetGadgetAttrs((struct Gadget *)app->dl_gad_btn_open_file, app->dl_win, NULL,
                                   GA_Disabled, !can_open_file, TAG_DONE);
        IIntuition->RefreshGList((struct Gadget *)app->dl_gad_btn_open_file, app->dl_win, NULL, 1);
    }
    if (app->dl_gad_btn_open_dir) {
        IIntuition->SetGadgetAttrs((struct Gadget *)app->dl_gad_btn_open_dir, app->dl_win, NULL,
                                   GA_Disabled, !can_open_dir, TAG_DONE);
        IIntuition->RefreshGList((struct Gadget *)app->dl_gad_btn_open_dir, app->dl_win, NULL, 1);
    }
    if (app->dl_gad_btn_cancel) {
        IIntuition->SetGadgetAttrs((struct Gadget *)app->dl_gad_btn_cancel, app->dl_win, NULL,
                                   GA_Disabled, !can_cancel, TAG_DONE);
        IIntuition->RefreshGList((struct Gadget *)app->dl_gad_btn_cancel, app->dl_win, NULL, 1);
    }
    if (app->dl_gad_btn_remove) {
        IIntuition->SetGadgetAttrs((struct Gadget *)app->dl_gad_btn_remove, app->dl_win, NULL,
                                   GA_Disabled, !can_remove, TAG_DONE);
        IIntuition->RefreshGList((struct Gadget *)app->dl_gad_btn_remove, app->dl_win, NULL, 1);
    }
    if (app->dl_gad_btn_clear) {
        IIntuition->SetGadgetAttrs((struct Gadget *)app->dl_gad_btn_clear, app->dl_win, NULL,
                                   GA_Disabled, !has_completed, TAG_DONE);
        IIntuition->RefreshGList((struct Gadget *)app->dl_gad_btn_clear, app->dl_win, NULL, 1);
    }
}

void gui_download_manager_update(VortexApp *app)
{
    if (!app || !IListBrowser) return;

    /* Detach list before updating node attrs if window is open */
    if (app->dl_win && app->dl_gad_listbrowser) {
        IIntuition->SetGadgetAttrs((struct Gadget *)app->dl_gad_listbrowser, app->dl_win, NULL,
                                   LISTBROWSER_Labels, (Tag)NULL,
                                   TAG_DONE);
    }

    int active_count = 0;
    int completed_count = 0;
    uint32_t total_spd = 0;

    for (int i = 0; i < app->download_mgr.count; i++) {
        DownloadEntry *e = &app->download_mgr.entries[i];
        if (!e->lb_node) continue;

        if (e->status == DL_STATUS_DOWNLOADING) {
            active_count++;
            total_spd += e->speed_bps;
        } else if (e->status == DL_STATUS_COMPLETED) {
            completed_count++;
        }

        char size_str[64];
        char rec_str[24], tot_str[24], spd_str[24], pct_str[16];
        format_size(e->received_size, rec_str, sizeof(rec_str));
        format_size(e->total_size, tot_str, sizeof(tot_str));
        format_speed(e->speed_bps, spd_str, sizeof(spd_str));

        int pct = (e->total_size > 0) ? (int)((e->received_size * 100ULL) / e->total_size) : 0;
        if (pct > 100) pct = 100;

        const char *status_str = "Laufend";
        if (e->status == DL_STATUS_COMPLETED) {
            status_str = "Fertig";
            snprintf(pct_str, sizeof(pct_str), "100%%");
            snprintf(size_str, sizeof(size_str), "%s", tot_str[0] ? tot_str : rec_str);
            snprintf(spd_str, sizeof(spd_str), "-");
        } else if (e->status == DL_STATUS_CANCELLED) {
            status_str = "Abgebrochen";
            snprintf(pct_str, sizeof(pct_str), "-");
            snprintf(size_str, sizeof(size_str), "%s", rec_str);
            snprintf(spd_str, sizeof(spd_str), "-");
        } else if (e->status == DL_STATUS_ERROR) {
            status_str = "Fehler";
            snprintf(pct_str, sizeof(pct_str), "-");
            snprintf(size_str, sizeof(size_str), "%s", rec_str);
            snprintf(spd_str, sizeof(spd_str), "-");
        } else {
            snprintf(pct_str, sizeof(pct_str), "%d%%", pct);
            snprintf(size_str, sizeof(size_str), "%s / %s", rec_str, tot_str);
        }

        IListBrowser->SetListBrowserNodeAttrs(e->lb_node,
            LBNA_Column, 1,
                LBNCA_CopyText, TRUE,
                LBNCA_Text, (Tag)status_str,
            LBNA_Column, 2,
                LBNCA_CopyText, TRUE,
                LBNCA_Text, (Tag)size_str,
            LBNA_Column, 3,
                LBNCA_CopyText, TRUE,
                LBNCA_Text, (Tag)pct_str,
            LBNA_Column, 4,
                LBNCA_CopyText, TRUE,
                LBNCA_Text, (Tag)spd_str,
            LBNA_Column, 5,
                LBNCA_CopyText, TRUE,
                LBNCA_Text, (Tag)e->eta_str,
            TAG_DONE);
    }

    if (app->dl_win && app->dl_gad_listbrowser) {
        IIntuition->SetGadgetAttrs((struct Gadget *)app->dl_gad_listbrowser, app->dl_win, NULL,
                                   LISTBROWSER_Labels, (Tag)&app->dl_node_list,
                                   TAG_DONE);
    }

    /* Update status bar summary */
    if (app->dl_win && app->dl_gad_status_txt) {
        char summary[128];
        if (active_count > 0) {
            char spd_buf[32];
            format_speed(total_spd, spd_buf, sizeof(spd_buf));
            snprintf(summary, sizeof(summary), "%d aktiver Download%s (%s) | %d abgeschlossen",
                     active_count, (active_count > 1 ? "s" : ""), spd_buf, completed_count);
        } else if (completed_count > 0) {
            snprintf(summary, sizeof(summary), "Alle Downloads abgeschlossen (%d Dateien)", completed_count);
        } else {
            snprintf(summary, sizeof(summary), "Keine aktiven Downloads");
        }

        IIntuition->SetGadgetAttrs((struct Gadget *)app->dl_gad_status_txt, app->dl_win, NULL,
                                   GA_Text, (Tag)summary,
                                   TAG_DONE);
        IIntuition->RefreshGList((struct Gadget *)app->dl_gad_status_txt, app->dl_win, NULL, 1);
    }

    /* Update button states based on currently selected item */
    update_button_states(app);
}

void gui_open_download_manager(VortexApp *app)
{
    if (!app || !IListBrowser) return;

    if (app->dl_win) {
        IIntuition->WindowToFront(app->dl_win);
        IIntuition->ActivateWindow(app->dl_win);
        gui_download_manager_update(app);
        return;
    }

    struct Screen *target_scr = (app->win && app->win->WScreen) ? app->win->WScreen : app->custom_screen;

    /* Allocate ColumnInfo if not already allocated */
    if (!app->dl_col_info) {
        app->dl_col_info = IListBrowser->AllocLBColumnInfo(7,
            LBCIA_Column, 0,
                LBCIA_Title, "Datei",
                LBCIA_Weight, 26,
                LBCIA_DraggableSeparator, TRUE,
            LBCIA_Column, 1,
                LBCIA_Title, "Status",
                LBCIA_Weight, 12,
                LBCIA_DraggableSeparator, TRUE,
            LBCIA_Column, 2,
                LBCIA_Title, "Größe",
                LBCIA_Weight, 16,
                LBCIA_DraggableSeparator, TRUE,
                LBCIA_HorizJustify, LCJ_RIGHT,
            LBCIA_Column, 3,
                LBCIA_Title, "%",
                LBCIA_Weight, 8,
                LBCIA_DraggableSeparator, TRUE,
                LBCIA_HorizJustify, LCJ_RIGHT,
            LBCIA_Column, 4,
                LBCIA_Title, "Speed",
                LBCIA_Weight, 12,
                LBCIA_DraggableSeparator, TRUE,
                LBCIA_HorizJustify, LCJ_RIGHT,
            LBCIA_Column, 5,
                LBCIA_Title, "Restzeit",
                LBCIA_Weight, 10,
                LBCIA_DraggableSeparator, TRUE,
                LBCIA_HorizJustify, LCJ_CENTER,
            LBCIA_Column, 6,
                LBCIA_Title, "Zielpfad",
                LBCIA_Weight, 16,
                LBCIA_DraggableSeparator, TRUE,
            TAG_DONE);
    }

    app->dl_gad_listbrowser = IIntuition->NewObject(NULL, "listbrowser.gadget",
        GA_ID,                       GID_DL_LIST,
        GA_RelVerify,                TRUE,
        LISTBROWSER_ColumnInfo,      (Tag)app->dl_col_info,
        LISTBROWSER_Labels,          (Tag)&app->dl_node_list,
        LISTBROWSER_ColumnTitles,    TRUE,
        LISTBROWSER_Separators,      TRUE,
        LISTBROWSER_Striping,        LBS_ROWS,
        LISTBROWSER_ShowSelected,    TRUE,
        LISTBROWSER_AutoFit,         FALSE,
        LISTBROWSER_Position,        0,
        TAG_DONE);

    app->dl_gad_status_txt = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,                GID_DL_STATUS_TXT,
        GA_ReadOnly,          TRUE,
        GA_Text,              (Tag)"Keine aktiven Downloads",
        BUTTON_Justification, BCJ_LEFT,
        BUTTON_BevelStyle,    BVS_FIELD,
        TAG_DONE);

    app->dl_gad_btn_open_file = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        GID_DL_BTN_OPEN_FILE,
        GA_RelVerify, TRUE,
        GA_Text,      "Datei öffnen",
        GA_HintInfo,  "Heruntergeladene Datei mit Standard-Programm ausführen (auch Doppelklick in Liste)",
        TAG_DONE);

    app->dl_gad_btn_open_dir = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        GID_DL_BTN_OPEN_DIR,
        GA_RelVerify, TRUE,
        GA_Text,      "Ordner öffnen",
        GA_HintInfo,  "Zielordner der Datei auf der Workbench öffnen",
        TAG_DONE);

    app->dl_gad_btn_cancel = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        GID_DL_BTN_CANCEL,
        GA_RelVerify, TRUE,
        GA_Text,      "Abbrechen",
        GA_HintInfo,  "Laufenden Download stoppen und abbrechen",
        TAG_DONE);

    app->dl_gad_btn_remove = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        GID_DL_BTN_REMOVE,
        GA_RelVerify, TRUE,
        GA_Text,      "Eintrag löschen",
        GA_HintInfo,  "Ausgewählten Eintrag aus der Liste entfernen",
        TAG_DONE);

    app->dl_gad_btn_clear = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        GID_DL_BTN_CLEAR,
        GA_RelVerify, TRUE,
        GA_Text,      "Aufräumen",
        GA_HintInfo,  "Alle abgeschlossenen und abgebrochenen Downloads aus der Liste entfernen",
        TAG_DONE);

    app->dl_gad_btn_close = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        GID_DL_BTN_CLOSE,
        GA_RelVerify, TRUE,
        GA_Text,      "Schließen",
        GA_HintInfo,  "Download-Manager Fenster schließen (Downloads laufen im Hintergrund weiter)",
        TAG_DONE);

    Object *btn_row = IIntuition->NewObject(NULL, "layout.gadget",
        LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
        LAYOUT_SpaceInner,    TRUE,
        LAYOUT_AddChild,      app->dl_gad_btn_open_file,
        CHILD_WeightedWidth,  17,
        LAYOUT_AddChild,      app->dl_gad_btn_open_dir,
        CHILD_WeightedWidth,  17,
        LAYOUT_AddChild,      app->dl_gad_btn_cancel,
        CHILD_WeightedWidth,  16,
        LAYOUT_AddChild,      app->dl_gad_btn_remove,
        CHILD_WeightedWidth,  17,
        LAYOUT_AddChild,      app->dl_gad_btn_clear,
        CHILD_WeightedWidth,  16,
        LAYOUT_AddChild,      app->dl_gad_btn_close,
        CHILD_WeightedWidth,  17,
        TAG_DONE);

    Object *root_layout = IIntuition->NewObject(NULL, "layout.gadget",
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_SpaceInner,    TRUE,
        LAYOUT_DeferLayout,   TRUE,
        LAYOUT_AddChild,      app->dl_gad_listbrowser,
        CHILD_WeightedHeight, 85,
        LAYOUT_AddChild,      app->dl_gad_status_txt,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      btn_row,
        CHILD_WeightedHeight, 0,
        TAG_DONE);

    if (!root_layout) return;

    app->dl_win_obj = IIntuition->NewObject(NULL, "window.class",
        WA_Title,             "Downloads - VortexBrowse",
        WA_ScreenTitle,       "VortexBrowse",
        target_scr ? WA_CustomScreen : TAG_IGNORE, (Tag)target_scr,
        WA_CloseGadget,       TRUE,
        WA_DepthGadget,       TRUE,
        WA_DragBar,           TRUE,
        WA_SizeGadget,        TRUE,
        WA_Activate,          TRUE,
        WA_SmartRefresh,      TRUE,
        WA_InnerWidth,        700,
        WA_InnerHeight,       320,
        WINDOW_Layout,        root_layout,
        TAG_DONE);

    if (!app->dl_win_obj) return;

    app->dl_win = (struct Window *)IIntuition->IDoMethod(app->dl_win_obj, WM_OPEN);
    if (!app->dl_win) {
        IIntuition->DisposeObject(app->dl_win_obj);
        app->dl_win_obj = NULL;
        return;
    }

    gui_download_manager_update(app);
}

void gui_close_download_manager(VortexApp *app)
{
    if (!app || !app->dl_win_obj) return;

    if (app->dl_win && app->dl_gad_listbrowser) {
        IIntuition->SetGadgetAttrs((struct Gadget *)app->dl_gad_listbrowser, app->dl_win, NULL,
                                   LISTBROWSER_Labels, (Tag)NULL,
                                   TAG_DONE);
    }

    IIntuition->DisposeObject(app->dl_win_obj);
    app->dl_win_obj           = NULL;
    app->dl_win               = NULL;
    app->dl_gad_listbrowser   = NULL;
    app->dl_gad_status_txt    = NULL;
    app->dl_gad_btn_open_file = NULL;
    app->dl_gad_btn_open_dir  = NULL;
    app->dl_gad_btn_cancel    = NULL;
    app->dl_gad_btn_remove    = NULL;
    app->dl_gad_btn_clear     = NULL;
    app->dl_gad_btn_close     = NULL;
}

void gui_handle_download_manager_events(VortexApp *app)
{
    if (!app || !app->dl_win_obj) return;

    uint32 result = WMHI_LASTMSG;
    int16  code   = 0;

    while ((result = IIntuition->IDoMethod(app->dl_win_obj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
        switch (result & WMHI_CLASSMASK) {
            case WMHI_CLOSEWINDOW:
                gui_close_download_manager(app);
                return;

            case WMHI_GADGETUP: {
                uint32 gid = result & WMHI_GADGETMASK;

                if (gid == GID_DL_LIST) {
                    uint32 rel_event = 0;
                    IIntuition->GetAttr(LISTBROWSER_RelEvent, app->dl_gad_listbrowser, &rel_event);
                    if (rel_event & LBRE_DOUBLECLICK) {
                        struct Node *sel_node = NULL;
                        IIntuition->GetAttr(LISTBROWSER_SelectedNode, app->dl_gad_listbrowser, (uint32 *)&sel_node);
                        if (sel_node && IListBrowser) {
                            APTR ud = NULL;
                            IListBrowser->GetListBrowserNodeAttrs(sel_node, LBNA_UserData, &ud, TAG_DONE);
                            DownloadEntry *e = (DownloadEntry *)ud;
                            if (e && e->status == DL_STATUS_COMPLETED && e->dest_path[0]) {
                                open_file(e->dest_path);
                            }
                        }
                    } else {
                        update_button_states(app);
                    }
                } else if (gid == GID_DL_BTN_OPEN_FILE) {
                    struct Node *sel_node = NULL;
                    IIntuition->GetAttr(LISTBROWSER_SelectedNode, app->dl_gad_listbrowser, (uint32 *)&sel_node);
                    if (sel_node && IListBrowser) {
                        APTR ud = NULL;
                        IListBrowser->GetListBrowserNodeAttrs(sel_node, LBNA_UserData, &ud, TAG_DONE);
                        DownloadEntry *e = (DownloadEntry *)ud;
                        if (e && e->status == DL_STATUS_COMPLETED && e->dest_path[0]) {
                            open_file(e->dest_path);
                        }
                    }
                } else if (gid == GID_DL_BTN_OPEN_DIR) {
                    struct Node *sel_node = NULL;
                    IIntuition->GetAttr(LISTBROWSER_SelectedNode, app->dl_gad_listbrowser, (uint32 *)&sel_node);
                    if (sel_node && IListBrowser) {
                        APTR ud = NULL;
                        IListBrowser->GetListBrowserNodeAttrs(sel_node, LBNA_UserData, &ud, TAG_DONE);
                        DownloadEntry *e = (DownloadEntry *)ud;
                        if (e && e->dest_path[0]) {
                            open_drawer(e->dest_path);
                        }
                    }
                } else if (gid == GID_DL_BTN_CANCEL) {
                    struct Node *sel_node = NULL;
                    IIntuition->GetAttr(LISTBROWSER_SelectedNode, app->dl_gad_listbrowser, (uint32 *)&sel_node);
                    if (sel_node && IListBrowser) {
                        APTR ud = NULL;
                        IListBrowser->GetListBrowserNodeAttrs(sel_node, LBNA_UserData, &ud, TAG_DONE);
                        DownloadEntry *e = (DownloadEntry *)ud;
                        if (e && e->status == DL_STATUS_DOWNLOADING) {
                            download_cancel(app, e->id);
                        }
                    }
                } else if (gid == GID_DL_BTN_REMOVE) {
                    struct Node *sel_node = NULL;
                    IIntuition->GetAttr(LISTBROWSER_SelectedNode, app->dl_gad_listbrowser, (uint32 *)&sel_node);
                    if (sel_node && IListBrowser) {
                        APTR ud = NULL;
                        IListBrowser->GetListBrowserNodeAttrs(sel_node, LBNA_UserData, &ud, TAG_DONE);
                        DownloadEntry *e = (DownloadEntry *)ud;
                        if (e) {
                            for (int i = 0; i < app->download_mgr.count; i++) {
                                if (&app->download_mgr.entries[i] == e) {
                                    download_remove_entry(app, i);
                                    break;
                                }
                            }
                        }
                    }
                } else if (gid == GID_DL_BTN_CLEAR) {
                    download_clear_completed(app);
                } else if (gid == GID_DL_BTN_CLOSE) {
                    gui_close_download_manager(app);
                    return;
                }
                break;
            }

            default:
                break;
        }
    }
}
