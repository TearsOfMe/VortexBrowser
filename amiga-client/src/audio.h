#ifndef VORTEXREMOTEBROWSER_AUDIO_H
#define VORTEXREMOTEBROWSER_AUDIO_H

#include "app.h"

BOOL   audio_init(VortexApp *app);
void   audio_cleanup(VortexApp *app);
void   audio_poll(VortexApp *app);
uint32 audio_get_signal_mask(VortexApp *app);

void audio_handle_packet(
    VortexApp      *app,
    const MsgAudio *hdr,
    const uint8_t  *payload,
    uint32_t        payload_len
);

#endif /* VORTEXREMOTEBROWSER_AUDIO_H */
