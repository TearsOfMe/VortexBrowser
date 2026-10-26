#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/asl.h>
#include <libraries/asl.h>

#include "download.h"
#include "net.h"

void download_init(VortexApp *app)
{
    memset(&app->download, 0, sizeof(DownloadState));
}

void download_cleanup(VortexApp *app)
{
    if (app->download.file_handle) {
        IDOS->Close(app->download.file_handle);
        app->download.file_handle = ZERO;
    }
    app->download.active = FALSE;
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
        strncpy(out, "download.bin", max_len - 1);
        out[max_len - 1] = '\0';
    }
}

void download_handle_start(VortexApp *app, const MsgDownloadStart *msg)
{
    DownloadState *dl = &app->download;

    /* Close any previously unfinished download */
    download_cleanup(app);

    dl->download_id   = VB_BE32(msg->download_id);
    dl->total_size    = VB_BE64(msg->total_size);
    dl->received_size = 0;

    char raw_name[256];
    memcpy(raw_name, msg->suggested_filename, 255);
    raw_name[255] = '\0';
    sanitize_filename(raw_name, dl->filename, sizeof(dl->filename));

    DBG_LOG("[Download] Incoming download id=%u: '%s' (%llu bytes)\n",
           (unsigned)dl->download_id,
           dl->filename,
           (unsigned long long)dl->total_size);

    char chosen_path[512];
    chosen_path[0] = '\0';
    BOOL accepted = FALSE;

    struct FileRequester *freq = (struct FileRequester *)IAsl->AllocAslRequestTags(
        ASL_FileRequest,
        ASLFR_Window,        app->win,
        ASLFR_SleepWindow,   TRUE,
        ASLFR_TitleText,     "VortexBrowse: Download speichern unter...",
        ASLFR_PositiveText,  "Speichern",
        ASLFR_NegativeText,  "Abbrechen",
        ASLFR_DoSaveMode,    TRUE,
        ASLFR_RejectIcons,   TRUE,
        ASLFR_InitialDrawer, "RAM:",
        ASLFR_InitialFile,   dl->filename,
        TAG_DONE);

    if (app->autotest) {
        /* In automated QEMU test mode, verify ASL allocation and save directly to autotest_dl_dir */
        const char *base_dir = (app->autotest_dl_dir[0] != '\0') ? app->autotest_dl_dir : "RAM:";
        strncpy(chosen_path, base_dir, sizeof(chosen_path) - 1);
        chosen_path[sizeof(chosen_path) - 1] = '\0';
        IDOS->AddPart(chosen_path, dl->filename, sizeof(chosen_path));
        accepted = TRUE;
        if (freq) {
            IAsl->FreeAslRequest(freq);
        }
    } else if (freq) {
        if (IAsl->AslRequest(freq, NULL)) {
            if (freq->fr_File && freq->fr_File[0] != '\0') {
                strncpy(chosen_path, freq->fr_Drawer ? freq->fr_Drawer : "", sizeof(chosen_path) - 1);
                chosen_path[sizeof(chosen_path) - 1] = '\0';
                IDOS->AddPart(chosen_path, freq->fr_File, sizeof(chosen_path));
                accepted = TRUE;
            }
        }
        IAsl->FreeAslRequest(freq);
    }

    if (!accepted || chosen_path[0] == '\0') {
        DBG_LOG("[Download] Download id=%u cancelled by user.\n", (unsigned)dl->download_id);
        app_set_status("Download abgebrochen: %s", dl->filename);
        app_set_gauge(0);
        net_send_download_response(app, dl->download_id, 0);
        return;
    }

    dl->file_handle = IDOS->Open(chosen_path, MODE_NEWFILE);
    if (!dl->file_handle) {
        DBG_LOG("[Download] ERROR: Could not open '%s' for writing!\n", chosen_path);
        app_set_status("Fehler beim Erstellen von: %s", chosen_path);
        app_set_gauge(0);
        net_send_download_response(app, dl->download_id, 0);
        return;
    }

    strncpy(dl->dest_path, chosen_path, sizeof(dl->dest_path) - 1);
    dl->dest_path[sizeof(dl->dest_path) - 1] = '\0';
    dl->active = TRUE;

    app_set_gauge(0);
    app_set_status("Starte Download: %s (%llu KB)...",
                   dl->filename,
                   (unsigned long long)(dl->total_size / 1024ULL));

    net_send_download_response(app, dl->download_id, 1);
}

void download_handle_chunk(VortexApp *app, const MsgDownloadChunk *hdr, const uint8_t *data)
{
    DownloadState *dl = &app->download;
    uint32_t id  = VB_BE32(hdr->download_id);
    uint32_t len = VB_BE32(hdr->chunk_len);

    if (!dl->active || dl->download_id != id || !dl->file_handle) {
        return;
    }

    int32 written = IDOS->Write(dl->file_handle, (APTR)data, (int32)len);
    if (written > 0) {
        dl->received_size += (uint64_t)written;
    }

    int pct = 0;
    if (dl->total_size > 0) {
        pct = (int)((dl->received_size * 100ULL) / dl->total_size);
        if (pct > 100) pct = 100;
    }
    app_set_gauge(pct);
    app_set_status("Lade herunter: %s (%llu / %llu KB, %d%%)",
                   dl->filename,
                   (unsigned long long)(dl->received_size / 1024ULL),
                   (unsigned long long)(dl->total_size / 1024ULL),
                   pct);
}

void download_handle_complete(VortexApp *app, const MsgDownloadComplete *msg)
{
    DownloadState *dl = &app->download;
    uint32_t id = VB_BE32(msg->download_id);

    if (!dl->active || dl->download_id != id) {
        return;
    }

    if (dl->file_handle) {
        IDOS->Close(dl->file_handle);
        dl->file_handle = ZERO;
    }
    dl->active = FALSE;

    char comment[160];
    snprintf(comment, sizeof(comment), "Downloaded via VortexBrowse from %.100s", app->current_url);
    IDOS->SetComment(dl->dest_path, comment);

    app_set_gauge(100);
    app_set_status("Download abgeschlossen: %s (%llu Bytes)",
                   dl->dest_path,
                   (unsigned long long)dl->received_size);

    DBG_LOG("[Download] Completed id=%u -> '%s' (%llu bytes)\n",
           (unsigned)id,
           dl->dest_path,
           (unsigned long long)dl->received_size);
}
