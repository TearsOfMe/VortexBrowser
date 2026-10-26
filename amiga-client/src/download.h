#ifndef VORTEXBROWSE_DOWNLOAD_H
#define VORTEXBROWSE_DOWNLOAD_H

#include "app.h"

void download_init(VortexApp *app);
void download_cleanup(VortexApp *app);

void download_handle_start(VortexApp *app, const MsgDownloadStart *msg);
void download_handle_chunk(VortexApp *app, const MsgDownloadChunk *hdr, const uint8_t *data);
void download_handle_complete(VortexApp *app, const MsgDownloadComplete *msg);

#endif /* VORTEXBROWSE_DOWNLOAD_H */
