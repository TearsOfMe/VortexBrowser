#ifndef VORTEXBROWSE_NET_H
#define VORTEXBROWSE_NET_H

#include "app.h"

BOOL net_connect(VortexApp *app, const char *host, uint16_t port);
void net_disconnect(VortexApp *app);

/* Process incoming packet(s) from the Pi server */
BOOL net_handle_incoming(VortexApp *app);
BOOL net_has_pending_data(VortexApp *app);

/* Send input & control packets to the Pi server */
BOOL net_send_input(
    VortexApp  *app,
    uint8_t     event_type,
    int16_t     x,
    int16_t     y,
    int16_t     delta_y,
    uint16_t    key_code,
    const char *text_or_url
);

BOOL net_send_url(VortexApp *app, const char *url);
BOOL net_send_click(VortexApp *app, int16_t x, int16_t y, uint16_t button);
BOOL net_send_mouse_button(VortexApp *app, int16_t x, int16_t y, uint16_t button, BOOL is_down);
BOOL net_send_move(VortexApp *app, int16_t x, int16_t y);
BOOL net_send_wheel(VortexApp *app, int16_t x, int16_t y, int16_t delta_y);
BOOL net_send_key(VortexApp *app, uint8_t event_type, uint16_t key_code, const char *key_str);
BOOL net_send_clipboard(VortexApp *app, const char *text);
BOOL net_send_download_response(VortexApp *app, uint32_t download_id, uint8_t accepted);

#endif /* VORTEXBROWSE_NET_H */
