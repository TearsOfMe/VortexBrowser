#ifndef VORTEXBROWSE_DOWNLOAD_H
#define VORTEXBROWSE_DOWNLOAD_H

#include "app.h"

void download_init(VortexApp *app);
void download_cleanup(VortexApp *app);

void download_handle_start(VortexApp *app, const MsgDownloadStart *msg);
void download_handle_chunk(VortexApp *app, const MsgDownloadChunk *hdr, const uint8_t *data);
void download_handle_complete(VortexApp *app, const MsgDownloadComplete *msg);

void download_cancel(VortexApp *app, uint32_t download_id);
void download_remove_entry(VortexApp *app, int index);
void download_clear_completed(VortexApp *app);
DownloadEntry *download_find_by_id(VortexApp *app, uint32_t id);

#endif /* VORTEXBROWSE_DOWNLOAD_H */
