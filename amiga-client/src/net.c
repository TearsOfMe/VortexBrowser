#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/bsdsocket.h>
#include <intuition/pointerclass.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>

#include <sys/select.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/errno.h>

#include "net.h"
#include "gui.h"
#include "renderer.h"
#include "download.h"
#include "audio.h"
#include "clipboard.h"

static BOOL send_all(LONG sock, const void *buf, uint32_t len)
{
    const uint8_t *ptr = (const uint8_t *)buf;
    uint32_t remaining = len;

    while (remaining > 0) {
        LONG sent = ISocket->send(sock, (APTR)ptr, (LONG)remaining, 0);
        if (sent < 0) {
            LONG err = ISocket->Errno();
            if (err == 4 /* EINTR */ || err == 35 /* EAGAIN */ || err == 11) {
                continue;
            }
            return FALSE;
        }
        if (sent == 0) {
            return FALSE;
        }
        ptr       += (uint32_t)sent;
        remaining -= (uint32_t)sent;
    }
    return TRUE;
}

static BOOL recv_exact(LONG sock, void *buf, uint32_t len)
{
    uint8_t *ptr = (uint8_t *)buf;
    uint32_t remaining = len;

    while (remaining > 0) {
        LONG got = ISocket->recv(sock, (APTR)ptr, (LONG)remaining, 0);
        if (got < 0) {
            LONG err = ISocket->Errno();
            if (err == 4 /* EINTR */ || err == 35 /* EAGAIN */ || err == 11) {
                continue;
            }
            return FALSE;
        }
        if (got == 0) {
            return FALSE;
        }
        ptr       += (uint32_t)got;
        remaining -= (uint32_t)got;
    }
    return TRUE;
}

BOOL net_connect(VortexApp *app, const char *host, uint16_t port)
{
    net_disconnect(app);

    if (!ISocket) {
        app_set_status("Fehler: bsdsocket.library nicht verfuegbar!");
        return FALSE;
    }

    if (host && host[0] != '\0' && host != app->server_host) {
        strncpy(app->server_host, host, sizeof(app->server_host) - 1);
        app->server_host[sizeof(app->server_host) - 1] = '\0';
    }
    if (port > 0) {
        app->server_port = port;
    }

    app_set_status("Verbinde mit %s:%u ...", app->server_host, (unsigned)app->server_port);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(app->server_port);

    in_addr_t ip = ISocket->inet_addr((STRPTR)app->server_host);
    if (ip == INADDR_NONE) {
        struct hostent *he = ISocket->gethostbyname((STRPTR)app->server_host);
        if (!he || !he->h_addr_list || !he->h_addr_list[0]) {
            app_set_status("Host konnte nicht aufgeloest werden: %s", app->server_host);
            return FALSE;
        }
        memcpy(&addr.sin_addr, he->h_addr_list[0], sizeof(struct in_addr));
    } else {
        addr.sin_addr.s_addr = ip;
    }

    LONG sock = ISocket->socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        app_set_status("Socket-Erstellung fehlgeschlagen!");
        return FALSE;
    }

    int flag = 1;
    ISocket->setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (APTR)&flag, sizeof(flag));

    int rcvbuf = 512 * 1024;
    ISocket->setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (APTR)&rcvbuf, sizeof(rcvbuf));

    LONG nonblock = 1;
    ISocket->IoctlSocket(sock, FIONBIO, (APTR)&nonblock);

    BOOL conn_ok = FALSE;
    LONG rc = ISocket->connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (rc == 0) {
        conn_ok = TRUE;
    } else {
        LONG err = ISocket->Errno();
        if (err == EINPROGRESS || err == EWOULDBLOCK || err == 36 || err == 35) {
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(sock, &wfds);
            struct timeval tv;
            tv.tv_sec  = 2;
            tv.tv_usec = 0;
            ULONG sigs = SIGBREAKF_CTRL_C;
            LONG sel = ISocket->WaitSelect(sock + 1, NULL, &wfds, NULL, &tv, &sigs);
            if (sel > 0 && FD_ISSET(sock, &wfds)) {
                int so_err = 0;
                socklen_t so_len = sizeof(so_err);
                if (ISocket->getsockopt(sock, SOL_SOCKET, SO_ERROR, (APTR)&so_err, &so_len) == 0 && so_err == 0) {
                    conn_ok = TRUE;
                }
            }
        }
    }

    nonblock = 0;
    ISocket->IoctlSocket(sock, FIONBIO, (APTR)&nonblock);

    if (!conn_ok) {
        ISocket->CloseSocket(sock);
        /* If port 7788 or RasPi failed, automatically fall back to Host-PC 192.168.178.60:9757 */
        if (strcmp(app->server_host, VORTEXBROWSE_HOSTPC_HOST) != 0 || app->server_port != VORTEXBROWSE_HOSTPC_PORT) {
            strncpy(app->server_host, VORTEXBROWSE_HOSTPC_HOST, sizeof(app->server_host) - 1);
            app->server_host[sizeof(app->server_host) - 1] = '\0';
            app->server_port = VORTEXBROWSE_HOSTPC_PORT;
            return net_connect(app, app->server_host, app->server_port);
        }
        app_set_status("Verbindung zu %s:%u fehlgeschlagen!", app->server_host, (unsigned)app->server_port);
        return FALSE;
    }

    app->sock_fd              = sock;
    app->connected            = TRUE;
    app->renderer.reported_w  = 0;
    app->renderer.reported_h  = 0;
    app->renderer.format_sent = FALSE;
    DBG_LOG("[Net] Connected to server at %s:%u (sock=%ld)\n",
           app->server_host, (unsigned)app->server_port, (long)sock);
    app_set_status("Verbunden mit %s:%u - Synchronisiere Viewport...",
                   app->server_host, (unsigned)app->server_port);

    /* Immediately sync Amiga window's space.gadget size or fixed resolution to server */
    prefs_apply(app);
    renderer_sync_viewport_size(app);
    net_send_url(app, "@REFRESH");

    return TRUE;
}

void net_disconnect(VortexApp *app)
{
    if (app->sock_fd >= 0 && ISocket) {
        ISocket->CloseSocket(app->sock_fd);
    }
    app->sock_fd   = -1;
    app->connected = FALSE;
}

BOOL net_handle_incoming(VortexApp *app)
{
    if (!app->connected || app->sock_fd < 0) {
        return FALSE;
    }

    uint8_t msg_type = 0;
    if (!recv_exact(app->sock_fd, &msg_type, 1)) {
        DBG_LOG("[Net] Server closed connection.\n");
        net_disconnect(app);
        app_set_status("Verbindung getrennt.");
        return FALSE;
    }

    switch (msg_type) {
        case MSG_FRAME: {
            MsgFrame hdr;
            if (!recv_exact(app->sock_fd, &hdr, sizeof(MsgFrame))) {
                net_disconnect(app);
                return FALSE;
            }
            uint32_t payload_len = VB_BE32(hdr.payload_len);
            if (payload_len == 0 || payload_len > (16 * 1024 * 1024)) {
                DBG_LOG("[Net] Invalid MSG_FRAME payload_len=%u\n", (unsigned)payload_len);
                net_disconnect(app);
                return FALSE;
            }

            RendererState *r = &app->renderer;
            if (r->lz4_comp_cap < payload_len) {
                uint8_t *nb = (uint8_t *)realloc(r->lz4_comp_buf, payload_len);
                if (!nb) {
                    net_disconnect(app);
                    return FALSE;
                }
                r->lz4_comp_buf = nb;
                r->lz4_comp_cap = payload_len;
            }

            if (!recv_exact(app->sock_fd, r->lz4_comp_buf, payload_len)) {
                net_disconnect(app);
                return FALSE;
            }

            if (renderer_decode_and_present(app, &hdr, r->lz4_comp_buf, payload_len)) {
                if (r->frames_decoded == 1) {
                    /* Immediately flush first frame to screen RastPort and refresh space gadget */
                    renderer_flush_dirty(app);
                    if (app->win && app->gad_space) {
                        IIntuition->RefreshGList((struct Gadget *)app->gad_space, app->win, NULL, 1);
                    }
                }
                if (!app->download.active && (r->frames_decoded == 1 || (r->frames_decoded % 10) == 0)) {
                    const char *fmt_name = (hdr.format == FMT_YUV420P)
                        ? (r->src_bm_is_yuv ? "YUV420 HW-GPU 12bpp" : "YUV420 SW 12bpp")
                        : ((hdr.format == FMT_RGB565) ? "RGB565 16bpp" : "ARGB32");
                    app_set_status("Verbunden (%ux%u %s LZ4, %u KB/Frame, Frame #%u) | %s",
                                   (unsigned)VB_BE16(hdr.width),
                                   (unsigned)VB_BE16(hdr.height),
                                   fmt_name,
                                   (unsigned)(payload_len / 1024U),
                                   (unsigned)r->frames_decoded,
                                   app->current_url[0] ? app->current_url : "Portal");
                }
            }
            break;
        }

        case MSG_DOWNLOAD_START: {
            MsgDownloadStart dl_start;
            if (!recv_exact(app->sock_fd, &dl_start, sizeof(MsgDownloadStart))) {
                net_disconnect(app);
                return FALSE;
            }
            download_handle_start(app, &dl_start);
            break;
        }

        case MSG_DOWNLOAD_CHUNK: {
            MsgDownloadChunk chunk_hdr;
            if (!recv_exact(app->sock_fd, &chunk_hdr, sizeof(MsgDownloadChunk))) {
                net_disconnect(app);
                return FALSE;
            }
            uint32_t clen = VB_BE32(chunk_hdr.chunk_len);
            if (clen > (4 * 1024 * 1024)) {
                net_disconnect(app);
                return FALSE;
            }
            static uint8_t s_chunk_buf[VORTEXBROWSE_DOWNLOAD_CHUNK_SIZE];
            uint8_t *buf = (clen <= sizeof(s_chunk_buf)) ? s_chunk_buf : (uint8_t *)malloc(clen);
            if (!buf) {
                net_disconnect(app);
                return FALSE;
            }
            if (!recv_exact(app->sock_fd, buf, clen)) {
                if (buf != s_chunk_buf) free(buf);
                net_disconnect(app);
                return FALSE;
            }
            download_handle_chunk(app, &chunk_hdr, buf);
            if (buf != s_chunk_buf) {
                free(buf);
            }
            break;
        }

        case MSG_DOWNLOAD_COMPLETE: {
            MsgDownloadComplete dl_comp;
            if (!recv_exact(app->sock_fd, &dl_comp, sizeof(MsgDownloadComplete))) {
                net_disconnect(app);
                return FALSE;
            }
            download_handle_complete(app, &dl_comp);
            break;
        }

        case MSG_AUDIO: {
            MsgAudio audio_hdr;
            if (!recv_exact(app->sock_fd, &audio_hdr, sizeof(MsgAudio))) {
                net_disconnect(app);
                return FALSE;
            }
            uint32_t plen = VB_BE32(audio_hdr.payload_len);
            if (plen == 0 || plen > (256 * 1024)) {
                net_disconnect(app);
                return FALSE;
            }
            static uint8_t s_audio_buf[VB_AUDIO_BUF_SIZE * 2];
            uint8_t *abuf = (plen <= sizeof(s_audio_buf)) ? s_audio_buf : (uint8_t *)malloc(plen);
            if (!abuf) {
                net_disconnect(app);
                return FALSE;
            }
            if (!recv_exact(app->sock_fd, abuf, plen)) {
                if (abuf != s_audio_buf) free(abuf);
                net_disconnect(app);
                return FALSE;
            }
            audio_handle_packet(app, &audio_hdr, abuf, plen);
            if (abuf != s_audio_buf) {
                free(abuf);
            }
            break;
        }

        case MSG_NAV_UPDATE: {
            MsgNavUpdate nav;
            if (!recv_exact(app->sock_fd, &nav, sizeof(MsgNavUpdate))) {
                net_disconnect(app);
                return FALSE;
            }
            nav.url[sizeof(nav.url) - 1] = '\0';
            if (nav.url[0] != '\0') {
                if (strncmp(nav.url, "http://127.0.0.1:", 17) == 0) {
                    app->current_url[0] = '\0';
                } else {
                    strncpy(app->current_url, nav.url, sizeof(app->current_url) - 1);
                    app->current_url[sizeof(app->current_url) - 1] = '\0';
                }
                gui_update_url_bar(app, nav.url);
                app_set_status("Seite geladen: %s",
                               app->current_url[0] ? app->current_url : "Portal");
            }
            break;
        }

        case MSG_CONTEXT_MENU: {
            MsgContextMenu ctx;
            if (!recv_exact(app->sock_fd, &ctx, sizeof(MsgContextMenu))) {
                net_disconnect(app);
                return FALSE;
            }
            ctx.link_url[sizeof(ctx.link_url) - 1]   = '\0';
            ctx.link_text[sizeof(ctx.link_text) - 1] = '\0';
            ctx.media_url[sizeof(ctx.media_url) - 1] = '\0';
            memcpy(&app->ctx_menu, &ctx, sizeof(MsgContextMenu));
            app->ctx_menu_ready = TRUE;
            break;
        }

        case MSG_PAGE_PROGRESS: {
            MsgProgress prog;
            if (!recv_exact(app->sock_fd, &prog, sizeof(MsgProgress))) {
                net_disconnect(app);
                return FALSE;
            }
            prog.status[sizeof(prog.status) - 1] = '\0';
            app_set_gauge(prog.percent);
            if (prog.status[0] != '\0') {
                if (strncmp(prog.status, "Link: ", 6) == 0) {
                    if (!app->is_hovering_link) {
                        app->is_hovering_link = TRUE;
                        if (app->win) {
                            IIntuition->SetWindowPointer(app->win, WA_PointerType, POINTERTYPE_LINK, TAG_DONE);
                            IIntuition->SetWindowAttrs(app->win, WA_RMBTrap, TRUE, TAG_DONE);
                        }
                    }
                } else if (app->is_hovering_link) {
                    app->is_hovering_link = FALSE;
                    if (app->win) {
                        IIntuition->SetWindowPointer(app->win, WA_PointerType, POINTERTYPE_NORMAL, TAG_DONE);
                        IIntuition->SetWindowAttrs(app->win, WA_RMBTrap, FALSE, TAG_DONE);
                    }
                }
                app_set_status("%s", prog.status);
            }
            break;
        }

        case MSG_CLIPBOARD: {
            MsgClipboard clip_hdr;
            if (!recv_exact(app->sock_fd, &clip_hdr, sizeof(MsgClipboard))) {
                net_disconnect(app);
                return FALSE;
            }
            uint32_t tlen = VB_BE32(clip_hdr.text_len);
            if (tlen > 0 && tlen <= 1024 * 1024) { /* up to 1MB text */
                char *clip_buf = (char *)malloc(tlen + 1);
                if (clip_buf) {
                    if (recv_exact(app->sock_fd, clip_buf, tlen)) {
                        clip_buf[tlen] = '\0';
                        clipboard_write_text(clip_buf);
                        app_set_status("Text in Zwischenablage kopiert (%u Zeichen)", (unsigned)tlen);
                    }
                    free(clip_buf);
                }
            }
            break;
        }

        default:
            DBG_LOG("[Net] Unknown msg_type=0x%02x\n", (unsigned)msg_type);
            break;
    }

    return TRUE;
}

BOOL net_has_pending_data(VortexApp *app)
{
    if (!app->connected || app->sock_fd < 0 || !ISocket) {
        return FALSE;
    }
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(app->sock_fd, &rfds);
    struct timeval tv;
    tv.tv_sec  = 0;
    tv.tv_usec = 0;
    LONG r = ISocket->WaitSelect(app->sock_fd + 1, &rfds, NULL, NULL, &tv, NULL);
    return (r > 0 && FD_ISSET(app->sock_fd, &rfds)) ? TRUE : FALSE;
}

BOOL net_send_input(
    VortexApp  *app,
    uint8_t     event_type,
    int16_t     x,
    int16_t     y,
    int16_t     delta_y,
    uint16_t    key_code,
    const char *text_or_url
)
{
    if (!app->connected || app->sock_fd < 0) {
        return FALSE;
    }

    uint8_t buf[1 + sizeof(MsgInput)];
    buf[0] = MSG_INPUT;

    MsgInput *msg = (MsgInput *)(buf + 1);
    memset(msg, 0, sizeof(MsgInput));
    msg->event_type = event_type;
    msg->x          = (int16_t)VB_BE16((uint16_t)x);
    msg->y          = (int16_t)VB_BE16((uint16_t)y);
    msg->delta_y    = (int16_t)VB_BE16((uint16_t)delta_y);
    msg->key_code   = VB_BE16(key_code);

    if (text_or_url) {
        strncpy(msg->text_or_url, text_or_url, sizeof(msg->text_or_url) - 1);
        msg->text_or_url[sizeof(msg->text_or_url) - 1] = '\0';
    }

    if (!send_all(app->sock_fd, buf, sizeof(buf))) {
        net_disconnect(app);
        return FALSE;
    }
    return TRUE;
}

BOOL net_send_url(VortexApp *app, const char *url)
{
    if (!app || !url || url[0] == '\0') {
        return FALSE;
    }
    if (url[0] == '@') {
        if (!app->connected || app->sock_fd < 0) {
            return FALSE;
        }
        return net_send_input(app, INPUT_GOTO_URL, 0, 0, 0, 0, url);
    }
    strncpy(app->current_url, url, sizeof(app->current_url) - 1);
    app->current_url[sizeof(app->current_url) - 1] = '\0';
    if (!app->connected || app->sock_fd < 0) {
        DBG_LOG("[Net] Not connected, auto-connecting to %s:%u before sending URL...\n",
               app->server_host, (unsigned)app->server_port);
        if (!net_connect(app, app->server_host, app->server_port)) {
            app_set_status("Verbindung zu %s:%u fehlgeschlagen!", app->server_host, (unsigned)app->server_port);
            return FALSE;
        }
    }
    app_set_status("Lade Seite: %s ...", url);
    return net_send_input(app, INPUT_GOTO_URL, 0, 0, 0, 0, url);
}

BOOL net_send_click(VortexApp *app, int16_t x, int16_t y, uint16_t button)
{
    return net_send_input(app, INPUT_CLICK, x, y, 0, button, NULL);
}

BOOL net_send_mouse_button(VortexApp *app, int16_t x, int16_t y, uint16_t button, BOOL is_down)
{
    return net_send_input(app, is_down ? INPUT_MOUSE_DOWN : INPUT_MOUSE_UP, x, y, 0, button, NULL);
}

BOOL net_send_move(VortexApp *app, int16_t x, int16_t y)
{
    return net_send_input(app, INPUT_MOUSE_MOVE, x, y, 0, 0, NULL);
}

BOOL net_send_wheel(VortexApp *app, int16_t x, int16_t y, int16_t delta_y)
{
    return net_send_input(app, INPUT_WHEEL, x, y, delta_y, 0, NULL);
}

BOOL net_send_key(VortexApp *app, uint8_t event_type, uint16_t key_code, const char *key_str)
{
    return net_send_input(app, event_type, 0, 0, 0, key_code, key_str);
}

BOOL net_send_clipboard(VortexApp *app, const char *text)
{
    if (!app || !app->connected || app->sock_fd < 0 || !text) {
        return FALSE;
    }
    uint32_t tlen = (uint32_t)strlen(text);
    uint8_t hdr[1 + sizeof(MsgClipboard)];
    hdr[0] = MSG_CLIPBOARD;
    MsgClipboard *clip = (MsgClipboard *)(hdr + 1);
    clip->text_len = VB_BE32(tlen);
    if (!send_all(app->sock_fd, hdr, sizeof(hdr))) {
        net_disconnect(app);
        return FALSE;
    }
    if (tlen > 0) {
        if (!send_all(app->sock_fd, text, tlen)) {
            net_disconnect(app);
            return FALSE;
        }
    }
    return TRUE;
}

BOOL net_send_download_response(VortexApp *app, uint32_t download_id, uint8_t accepted)
{
    if (!app->connected || app->sock_fd < 0) {
        return FALSE;
    }

    uint8_t buf[1 + sizeof(MsgDownloadResponse)];
    buf[0] = MSG_DOWNLOAD_RESPONSE;

    MsgDownloadResponse *resp = (MsgDownloadResponse *)(buf + 1);
    memset(resp, 0, sizeof(MsgDownloadResponse));
    resp->download_id = VB_BE32(download_id);
    resp->accepted    = accepted;

    return send_all(app->sock_fd, buf, sizeof(buf));
}
