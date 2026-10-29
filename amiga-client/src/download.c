#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/asl.h>
#include <proto/intuition.h>
#include <proto/listbrowser.h>
#include <gadgets/listbrowser.h>
#include <libraries/asl.h>

#include "download.h"
#include "gui_download.h"
#include "net.h"

extern struct ListBrowserIFace *IListBrowser;

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

void download_init(VortexApp *app)
{
    if (!app) return;
    memset(&app->download, 0, sizeof(DownloadState));
    memset(&app->download_mgr, 0, sizeof(DownloadManager));
    app->dl_cur_sel = -1;
    IExec->NewList(&app->dl_node_list);
    app->dl_col_info = NULL;
}

void download_cleanup(VortexApp *app)
{
    if (!app) return;

    gui_close_download_manager(app);

    if (IListBrowser) {
        IListBrowser->FreeListBrowserList(&app->dl_node_list);
        if (app->dl_col_info) {
            IListBrowser->FreeLBColumnInfo(app->dl_col_info);
            app->dl_col_info = NULL;
        }
    }

    if (app->download.file_handle) {
        IDOS->Close(app->download.file_handle);
        app->download.file_handle = ZERO;
    }
    app->download.active = FALSE;

    for (int i = 0; i < app->download_mgr.count; i++) {
        if (app->download_mgr.entries[i].file_handle) {
            IDOS->Close(app->download_mgr.entries[i].file_handle);
            app->download_mgr.entries[i].file_handle = ZERO;
        }
    }
}

DownloadEntry *download_find_by_id(VortexApp *app, uint32_t id)
{
    if (!app) return NULL;
    for (int i = 0; i < app->download_mgr.count; i++) {
        if (app->download_mgr.entries[i].id == id) {
            return &app->download_mgr.entries[i];
        }
    }
    return NULL;
}

static void sanitize_filename(const char *in, char *out, size_t max_len)
{
    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && j + 1 < max_len; i++) {
        char c = in[i];
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
        out[j++] = c;
    }
    out[j] = '\0';
    if (j == 0) {
        snprintf(out, max_len, "download.bin");
    }
}

void download_handle_start(VortexApp *app, const MsgDownloadStart *msg)
{
    if (!app || !msg) return;

    uint32_t dl_id    = VB_BE32(msg->download_id);
    uint64_t dl_total = VB_BE64(msg->total_size);

    char raw_name[256];
    memcpy(raw_name, msg->suggested_filename, 255);
    raw_name[255] = '\0';

    char safe_name[256];
    sanitize_filename(raw_name, safe_name, sizeof(safe_name));

    DBG_LOG("[Download] Incoming download id=%u: '%s' (%llu bytes)\n",
           (unsigned)dl_id, safe_name, (unsigned long long)dl_total);

    char chosen_path[512];
    chosen_path[0] = '\0';
    BOOL accepted = FALSE;

    if (app->autotest) {
        const char *base_dir = (app->autotest_dl_dir[0] != '\0') ? app->autotest_dl_dir : "RAM:";
        snprintf(chosen_path, sizeof(chosen_path), "%s", base_dir);
        IDOS->AddPart(chosen_path, safe_name, sizeof(chosen_path));
        accepted = TRUE;
    } else if (!app->prefs.always_ask_download && app->prefs.download_dir[0] != '\0') {
        /* Direct download to configured directory without requester */
        snprintf(chosen_path, sizeof(chosen_path), "%s", app->prefs.download_dir);
        IDOS->AddPart(chosen_path, safe_name, sizeof(chosen_path));
        accepted = TRUE;
    } else {
        const char *init_drawer = (app->prefs.download_dir[0] != '\0') ? app->prefs.download_dir : "RAM:";
        struct FileRequester *freq = (struct FileRequester *)IAsl->AllocAslRequestTags(
            ASL_FileRequest,
            ASLFR_Window,        app->win,
            ASLFR_SleepWindow,   TRUE,
            ASLFR_TitleText,     "VortexBrowser: Download speichern unter...",
            ASLFR_PositiveText,  "Speichern",
            ASLFR_NegativeText,  "Abbrechen",
            ASLFR_DoSaveMode,    TRUE,
            ASLFR_RejectIcons,   TRUE,
            ASLFR_InitialDrawer, (Tag)init_drawer,
            ASLFR_InitialFile,   (Tag)safe_name,
            TAG_DONE);

        if (freq) {
            if (IAsl->AslRequest(freq, NULL)) {
                if (freq->fr_File && freq->fr_File[0] != '\0') {
                    snprintf(chosen_path, sizeof(chosen_path), "%s", freq->fr_Drawer ? freq->fr_Drawer : "");
                    IDOS->AddPart(chosen_path, freq->fr_File, sizeof(chosen_path));
                    accepted = TRUE;
                }
            }
            IAsl->FreeAslRequest(freq);
        }
    }

    /* Add entry to DownloadManager */
    int idx = app->download_mgr.count;
    if (idx >= VB_MAX_DOWNLOADS) {
        /* Drop oldest completed/cancelled entry if full */
        int drop_idx = -1;
        for (int i = 0; i < app->download_mgr.count; i++) {
            if (app->download_mgr.entries[i].status != DL_STATUS_DOWNLOADING) {
                drop_idx = i;
                break;
            }
        }
        if (drop_idx >= 0) {
            download_remove_entry(app, drop_idx);
            idx = app->download_mgr.count;
        } else {
            idx = VB_MAX_DOWNLOADS - 1;
        }
    }

    DownloadEntry *e = &app->download_mgr.entries[idx];
    memset(e, 0, sizeof(DownloadEntry));
    e->id             = dl_id;
    e->total_size     = dl_total;
    e->received_size  = 0;
    e->start_seconds  = (uint32_t)time(NULL);
    e->last_calc_secs = e->start_seconds;
    e->speed_bps      = 0;
    snprintf(e->filename, sizeof(e->filename), "%s", safe_name);
    snprintf(e->dest_path, sizeof(e->dest_path), "%s", chosen_path);
    snprintf(e->eta_str, sizeof(e->eta_str), "--:--");

    if (!accepted || chosen_path[0] == '\0') {
        DBG_LOG("[Download] Download id=%u cancelled by user.\n", (unsigned)dl_id);
        e->status = DL_STATUS_CANCELLED;
        snprintf(e->eta_str, sizeof(e->eta_str), "Abgebr.");
        if (idx == app->download_mgr.count) app->download_mgr.count++;
        app_set_status("Download abgebrochen: %s", safe_name);
        app_set_gauge(0);
        net_send_download_response(app, dl_id, 0);
    } else {
        BPTR fh = IDOS->Open(chosen_path, MODE_NEWFILE);
        if (!fh) {
            DBG_LOG("[Download] ERROR: Could not open '%s' for writing!\n", chosen_path);
            e->status = DL_STATUS_ERROR;
            snprintf(e->eta_str, sizeof(e->eta_str), "Fehler");
            if (idx == app->download_mgr.count) app->download_mgr.count++;
            app_set_status("Fehler beim Erstellen von: %s", chosen_path);
            app_set_gauge(0);
            net_send_download_response(app, dl_id, 0);
        } else {
            e->file_handle = fh;
            e->status      = DL_STATUS_DOWNLOADING;
            if (idx == app->download_mgr.count) app->download_mgr.count++;

            /* Update legacy app->download state */
            app->download.active        = TRUE;
            app->download.download_id   = dl_id;
            app->download.total_size    = dl_total;
            app->download.received_size = 0;
            app->download.file_handle   = fh;
            snprintf(app->download.filename, sizeof(app->download.filename), "%s", safe_name);
            snprintf(app->download.dest_path, sizeof(app->download.dest_path), "%s", chosen_path);

            app_set_gauge(0);
            app_set_status("Starte Download: %s (%llu KB)...", safe_name,
                           (unsigned long long)(dl_total / 1024ULL));

            net_send_download_response(app, dl_id, 1);
        }
    }

    /* Allocate ListBrowser node */
    if (IListBrowser) {
        char tot_str[32];
        format_size(dl_total, tot_str, sizeof(tot_str));
        const char *status_str = (e->status == DL_STATUS_DOWNLOADING) ? "Laufend" :
                                 (e->status == DL_STATUS_COMPLETED)   ? "Fertig" :
                                 (e->status == DL_STATUS_CANCELLED)   ? "Abgebrochen" : "Fehler";

        struct Node *node = IListBrowser->AllocListBrowserNode(7,
            LBNA_UserData, (Tag)e,
            LBNA_Column, 0,
                LBNCA_CopyText, TRUE,
                LBNCA_Text, (Tag)e->filename,
            LBNA_Column, 1,
                LBNCA_CopyText, TRUE,
                LBNCA_Text, (Tag)status_str,
            LBNA_Column, 2,
                LBNCA_CopyText, TRUE,
                LBNCA_Text, (Tag)(tot_str[0] ? tot_str : "-"),
                LBNCA_HorizJustify, LCJ_RIGHT,
            LBNA_Column, 3,
                LBNCA_CopyText, TRUE,
                LBNCA_Text, (Tag)"0%",
                LBNCA_HorizJustify, LCJ_RIGHT,
            LBNA_Column, 4,
                LBNCA_CopyText, TRUE,
                LBNCA_Text, (Tag)"-",
                LBNCA_HorizJustify, LCJ_RIGHT,
            LBNA_Column, 5,
                LBNCA_CopyText, TRUE,
                LBNCA_Text, (Tag)e->eta_str,
                LBNCA_HorizJustify, LCJ_CENTER,
            LBNA_Column, 6,
                LBNCA_CopyText, TRUE,
                LBNCA_Text, (Tag)e->dest_path,
            TAG_DONE);

        e->lb_node = node;
        if (node) {
            if (app->dl_win && app->dl_gad_listbrowser) {
                IIntuition->SetGadgetAttrs((struct Gadget *)app->dl_gad_listbrowser, app->dl_win, NULL,
                                           LISTBROWSER_Labels, (Tag)NULL,
                                           TAG_DONE);
            }
            IExec->AddTail(&app->dl_node_list, node);
            if (app->dl_win && app->dl_gad_listbrowser) {
                IIntuition->SetGadgetAttrs((struct Gadget *)app->dl_gad_listbrowser, app->dl_win, NULL,
                                           LISTBROWSER_Labels, (Tag)&app->dl_node_list,
                                           LISTBROWSER_SelectedNode, (Tag)node,
                                           LISTBROWSER_MakeNodeVisible, (Tag)node,
                                           TAG_DONE);
            }
        }
    }

    /* Open Download Manager so user has full visibility and control */
    gui_open_download_manager(app);
    gui_download_manager_update(app);
}

void download_handle_chunk(VortexApp *app, const MsgDownloadChunk *hdr, const uint8_t *data)
{
    if (!app || !hdr || !data) return;

    uint32_t id  = VB_BE32(hdr->download_id);
    uint32_t len = VB_BE32(hdr->chunk_len);

    DownloadEntry *e = download_find_by_id(app, id);
    if (!e || e->status != DL_STATUS_DOWNLOADING || !e->file_handle) {
        return;
    }

    int32 written = IDOS->Write(e->file_handle, (APTR)data, (int32)len);
    if (written > 0) {
        e->received_size += (uint64_t)written;
    }

    uint32_t now = (uint32_t)time(NULL);
    uint32_t elapsed = (now >= e->start_seconds) ? (now - e->start_seconds) : 0;
    if (elapsed > 0) {
        e->speed_bps = (uint32_t)(e->received_size / (uint64_t)elapsed);
    }

    int pct = 0;
    if (e->total_size > 0) {
        pct = (int)((e->received_size * 100ULL) / e->total_size);
        if (pct > 100) pct = 100;
    }

    if (e->total_size > e->received_size && e->speed_bps > 0) {
        uint64_t rem_bytes = e->total_size - e->received_size;
        uint32_t eta_secs  = (uint32_t)(rem_bytes / (uint64_t)e->speed_bps);
        if (eta_secs >= 3600) {
            snprintf(e->eta_str, sizeof(e->eta_str), "%02u:%02u:%02u",
                     eta_secs / 3600, (eta_secs % 3600) / 60, eta_secs % 60);
        } else {
            snprintf(e->eta_str, sizeof(e->eta_str), "%02u:%02u",
                     eta_secs / 60, eta_secs % 60);
        }
    } else {
        snprintf(e->eta_str, sizeof(e->eta_str), "--:--");
    }

    if (app->download.download_id == id) {
        app->download.received_size = e->received_size;
        app_set_gauge(pct);
        app_set_status("Lade herunter: %s (%llu / %llu KB, %d%%)",
                       e->filename,
                       (unsigned long long)(e->received_size / 1024ULL),
                       (unsigned long long)(e->total_size / 1024ULL),
                       pct);
    }

    /* Rate-limit listbrowser GUI updates to at most once per second while receiving chunks */
    if (now != e->last_calc_secs) {
        e->last_calc_secs = now;
        gui_download_manager_update(app);
    }
}

void download_handle_complete(VortexApp *app, const MsgDownloadComplete *msg)
{
    if (!app || !msg) return;

    uint32_t id = VB_BE32(msg->download_id);
    DownloadEntry *e = download_find_by_id(app, id);
    if (!e) return;

    if (e->file_handle) {
        IDOS->Close(e->file_handle);
        e->file_handle = ZERO;
    }
    e->status = DL_STATUS_COMPLETED;
    snprintf(e->eta_str, sizeof(e->eta_str), "Fertig");

    if (e->dest_path[0]) {
        char comment[160];
        snprintf(comment, sizeof(comment), "Downloaded via VortexBrowser from %.100s", app->current_url);
        IDOS->SetComment(e->dest_path, comment);
    }

    if (app->download.download_id == id) {
        app->download.active = FALSE;
        app->download.file_handle = ZERO;
        app_set_gauge(100);
        app_set_status("Download abgeschlossen: %s (%llu Bytes)",
                       e->dest_path, (unsigned long long)e->received_size);
    }

    DBG_LOG("[Download] Completed id=%u -> '%s' (%llu bytes)\n",
           (unsigned)id, e->dest_path, (unsigned long long)e->received_size);

    gui_download_manager_update(app);
}

void download_cancel(VortexApp *app, uint32_t download_id)
{
    if (!app) return;

    DownloadEntry *e = download_find_by_id(app, download_id);
    if (e) {
        if (e->file_handle) {
            IDOS->Close(e->file_handle);
            e->file_handle = ZERO;
        }
        e->status = DL_STATUS_CANCELLED;
        snprintf(e->eta_str, sizeof(e->eta_str), "Abgebr.");
    }

    if (app->download.download_id == download_id) {
        if (app->download.file_handle) {
            IDOS->Close(app->download.file_handle);
            app->download.file_handle = ZERO;
        }
        app->download.active = FALSE;
        app_set_gauge(0);
        app_set_status("Download abgebrochen.");
    }

    net_send_download_response(app, download_id, 0);
    gui_download_manager_update(app);
}

void download_remove_entry(VortexApp *app, int index)
{
    if (!app || index < 0 || index >= app->download_mgr.count) return;

    DownloadEntry *e = &app->download_mgr.entries[index];
    if (e->status == DL_STATUS_DOWNLOADING) {
        download_cancel(app, e->id);
    }

    if (e->lb_node && IListBrowser) {
        if (app->dl_win && app->dl_gad_listbrowser) {
            IIntuition->SetGadgetAttrs((struct Gadget *)app->dl_gad_listbrowser, app->dl_win, NULL,
                                       LISTBROWSER_Labels, (Tag)NULL,
                                       TAG_DONE);
        }
        IExec->Remove(e->lb_node);
        IListBrowser->FreeListBrowserNode(e->lb_node);
        e->lb_node = NULL;
        if (app->dl_win && app->dl_gad_listbrowser) {
            IIntuition->SetGadgetAttrs((struct Gadget *)app->dl_gad_listbrowser, app->dl_win, NULL,
                                       LISTBROWSER_Labels, (Tag)&app->dl_node_list,
                                       TAG_DONE);
        }
    }

    for (int i = index; i < app->download_mgr.count - 1; i++) {
        app->download_mgr.entries[i] = app->download_mgr.entries[i + 1];
    }
    app->download_mgr.count--;

    if (app->dl_cur_sel >= app->download_mgr.count) {
        app->dl_cur_sel = app->download_mgr.count - 1;
    }

    gui_download_manager_update(app);
}

void download_clear_completed(VortexApp *app)
{
    if (!app) return;

    if (app->dl_win && app->dl_gad_listbrowser) {
        IIntuition->SetGadgetAttrs((struct Gadget *)app->dl_gad_listbrowser, app->dl_win, NULL,
                                   LISTBROWSER_Labels, (Tag)NULL,
                                   TAG_DONE);
    }

    int write_idx = 0;
    for (int i = 0; i < app->download_mgr.count; i++) {
        DownloadEntry *e = &app->download_mgr.entries[i];
        if (e->status == DL_STATUS_DOWNLOADING) {
            if (write_idx != i) {
                app->download_mgr.entries[write_idx] = *e;
            }
            write_idx++;
        } else {
            if (e->lb_node && IListBrowser) {
                IExec->Remove(e->lb_node);
                IListBrowser->FreeListBrowserNode(e->lb_node);
                e->lb_node = NULL;
            }
        }
    }
    app->download_mgr.count = write_idx;

    if (app->dl_win && app->dl_gad_listbrowser) {
        IIntuition->SetGadgetAttrs((struct Gadget *)app->dl_gad_listbrowser, app->dl_win, NULL,
                                   LISTBROWSER_Labels, (Tag)&app->dl_node_list,
                                   TAG_DONE);
    }

    if (app->dl_cur_sel >= app->download_mgr.count) {
        app->dl_cur_sel = (app->download_mgr.count > 0) ? 0 : -1;
    }

    gui_download_manager_update(app);
}
