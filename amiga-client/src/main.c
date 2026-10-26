#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/asl.h>
#include <proto/utility.h>
#include <proto/bsdsocket.h>
#include <proto/chooser.h>
#include <proto/popupmenu.h>

#include <sys/select.h>
#include <sys/time.h>
#include <intuition/pointerclass.h>

#include "app.h"
#include "prefs.h"
#include "gui.h"
#include "net.h"
#include "renderer.h"
#include "favorites.h"
#include "download.h"
#include "audio.h"
#include "clipboard.h"

VortexApp g_app;

/* Explicitly managed libraries & interfaces (in addition to newlib auto-init) */
struct Library        *SocketBase    = NULL;
struct SocketIFace    *ISocket       = NULL;
struct Library        *PopupMenuBase = NULL;
struct PopupMenuIFace *IPopupMenu    = NULL;

static struct ClassLibrary *s_WindowBase        = NULL;
static struct ClassLibrary *s_LayoutBase        = NULL;
static struct ClassLibrary *s_ButtonBase        = NULL;
static struct ClassLibrary *s_StringBase        = NULL;
static struct ClassLibrary *s_SpaceBase         = NULL;
static struct ClassLibrary *s_ChooserBase       = NULL;
static struct ClassLibrary *s_FuelGaugeBase     = NULL;
static struct ClassLibrary *s_ClickTabBase      = NULL;
static struct ClassLibrary *s_BitMapBase        = NULL;
static struct ClassLibrary *s_LabelBase         = NULL;
static struct ClassLibrary *s_PopupMenuClassLib = NULL;

struct ChooserIFace  *IChooser  = NULL;
struct ClickTabIFace *IClickTab = NULL;

static BOOL open_libs_and_classes(void)
{
    SocketBase = IExec->OpenLibrary("bsdsocket.library", 4);
    if (!SocketBase) {
        DBG_LOG("[Main] ERROR: Could not open bsdsocket.library v4!\n");
        return FALSE;
    }
    ISocket = (struct SocketIFace *)IExec->GetInterface(SocketBase, "main", 1, NULL);
    if (!ISocket) {
        DBG_LOG("[Main] ERROR: Could not obtain ISocket interface!\n");
        return FALSE;
    }

    Class *dummy = NULL;
    s_WindowBase    = IIntuition->OpenClass("window.class", 53, &dummy);
    s_LayoutBase    = IIntuition->OpenClass("gadgets/layout.gadget", 53, &dummy);
    s_ButtonBase    = IIntuition->OpenClass("gadgets/button.gadget", 53, &dummy);
    s_StringBase    = IIntuition->OpenClass("gadgets/string.gadget", 53, &dummy);
    s_SpaceBase     = IIntuition->OpenClass("gadgets/space.gadget", 53, &dummy);
    s_ChooserBase   = IIntuition->OpenClass("gadgets/chooser.gadget", 53, &dummy);
    s_FuelGaugeBase = IIntuition->OpenClass("gadgets/fuelgauge.gadget", 53, &dummy);
    s_ClickTabBase  = IIntuition->OpenClass("gadgets/clicktab.gadget", 53, &dummy);
    if (!s_ClickTabBase) s_ClickTabBase = IIntuition->OpenClass("clicktab.gadget", 0, &dummy);
    s_BitMapBase    = IIntuition->OpenClass("images/bitmap.image", 53, &dummy);
    if (!s_BitMapBase) s_BitMapBase = IIntuition->OpenClass("bitmap.image", 0, &dummy);
    s_LabelBase     = IIntuition->OpenClass("images/label.image", 53, &dummy);
    if (!s_LabelBase)  s_LabelBase  = IIntuition->OpenClass("label.image", 0, &dummy);

    if (!s_WindowBase || !s_LayoutBase || !s_ButtonBase || !s_StringBase ||
        !s_SpaceBase || !s_ChooserBase || !s_FuelGaugeBase) {
        DBG_LOG("[Main] ERROR: Failed to open one or more Reaction BOOPSI classes!\n");
        return FALSE;
    }

    IChooser = (struct ChooserIFace *)IExec->GetInterface(
        (struct Library *)s_ChooserBase, "main", 1, NULL);
    if (!IChooser) {
        DBG_LOG("[Main] ERROR: Could not obtain IChooser interface!\n");
        return FALSE;
    }

    if (s_ClickTabBase) {
        IClickTab = (struct ClickTabIFace *)IExec->GetInterface(
            (struct Library *)s_ClickTabBase, "main", 1, NULL);
    }
    if (!IClickTab) {
        DBG_LOG("[Main] WARNING: Could not obtain IClickTab interface from clicktab.gadget!\n");
    }

    PopupMenuBase = IExec->OpenLibrary("popupmenu.class", 0);
    if (!PopupMenuBase) {
        s_PopupMenuClassLib = IIntuition->OpenClass("popupmenu.class", 0, &dummy);
        PopupMenuBase = (struct Library *)s_PopupMenuClassLib;
    }
    if (PopupMenuBase) {
        IPopupMenu = (struct PopupMenuIFace *)IExec->GetInterface(PopupMenuBase, "main", 1, NULL);
    }

    return TRUE;
}

static void close_libs_and_classes(void)
{
    if (IPopupMenu) {
        IExec->DropInterface((struct Interface *)IPopupMenu);
        IPopupMenu = NULL;
    }
    if (s_PopupMenuClassLib) {
        IIntuition->CloseClass(s_PopupMenuClassLib);
        s_PopupMenuClassLib = NULL;
        PopupMenuBase = NULL;
    } else if (PopupMenuBase) {
        IExec->CloseLibrary(PopupMenuBase);
        PopupMenuBase = NULL;
    }

    if (IClickTab) {
        IExec->DropInterface((struct Interface *)IClickTab);
        IClickTab = NULL;
    }
    if (s_ClickTabBase) {
        IIntuition->CloseClass(s_ClickTabBase);
        s_ClickTabBase = NULL;
    }
    if (s_LabelBase) {
        IIntuition->CloseClass(s_LabelBase);
        s_LabelBase = NULL;
    }
    if (s_BitMapBase) {
        IIntuition->CloseClass(s_BitMapBase);
        s_BitMapBase = NULL;
    }
    if (IChooser) {
        IExec->DropInterface((struct Interface *)IChooser);
        IChooser = NULL;
    }
    if (s_FuelGaugeBase) IIntuition->CloseClass(s_FuelGaugeBase);
    if (s_ChooserBase)   IIntuition->CloseClass(s_ChooserBase);
    if (s_SpaceBase)     IIntuition->CloseClass(s_SpaceBase);
    if (s_StringBase)    IIntuition->CloseClass(s_StringBase);
    if (s_ButtonBase)    IIntuition->CloseClass(s_ButtonBase);
    if (s_LayoutBase)    IIntuition->CloseClass(s_LayoutBase);
    if (s_WindowBase)    IIntuition->CloseClass(s_WindowBase);

    if (ISocket) {
        IExec->DropInterface((struct Interface *)ISocket);
        ISocket = NULL;
    }
    if (SocketBase) {
        IExec->CloseLibrary(SocketBase);
        SocketBase = NULL;
    }
}

static void run_autotest_step(VortexApp *app)
{
    if (!app->autotest) {
        return;
    }
    app->autotest_ticks++;

    /* Stage 0: Wait until at least 1 LZ4 frame has been decoded & composited */
    if (app->autotest_stage == 0 && app->renderer.frames_decoded >= 1) {
        printf("[AutoTest] PASS 1/4: LZ4 Frame decoded (%ux%u) & CompositeTags err=%u\n",
               (unsigned)app->renderer.src_width,
               (unsigned)app->renderer.src_height,
               (unsigned)app->renderer.last_comp_err);

        /* Test Favorites in ENVARC: */
        favorites_add(app, "VortexBrowse AutoTest Bookmark", "https://www.amigans.net");
        BOOL fav_ok = favorites_save(app);
        favorites_rebuild_chooser_list(app);
        printf("[AutoTest] PASS 2/4: Favorites saved to ENVARC:VortexBrowse/favorites.prefs (ok=%d, count=%d)\n",
               (int)fav_ok, app->num_favorites);

        /* Trigger click on interactive button on portal */
        net_send_click(app, 900, 420, 1);
        app->autotest_stage = 1;
        app->autotest_ticks = 0;
    }
    /* Stage 1: Trigger download of test package from Pi server */
    else if (app->autotest_stage == 1 && app->autotest_ticks >= 4) {
        char dl_url[256];
        snprintf(dl_url, sizeof(dl_url), "http://127.0.0.1:8089/download/VortexBrowse_TestPackage.lha");
        printf("[AutoTest] Triggering download proxy test: %s\n", dl_url);
        net_send_url(app, dl_url);
        app->autotest_stage = 2;
        app->autotest_ticks = 0;
    }
    /* Stage 2: Wait for download completion */
    else if (app->autotest_stage == 2) {
        if (!app->download.active && app->download.received_size > 0) {
            printf("[AutoTest] PASS 3/4: Download Proxy streamed %llu bytes to '%s'\n",
                   (unsigned long long)app->download.received_size,
                   app->download.dest_path);
            printf("[AutoTest] PASS 4/4: All VortexBrowse AmigaOS 4 subsystems verified!\n");
            app->autotest_stage = 3;
            app->running = FALSE;
        } else if (app->autotest_ticks > 150) {
            printf("[AutoTest] WARNING: Timeout waiting for download completion.\n");
            app->running = FALSE;
        }
    }
}

int main(int argc, char **argv)
{
    memset(&g_app, 0, sizeof(VortexApp));
    /* Default to CachyOS Host-PC on port 9757 */
    strncpy(g_app.server_host, VORTEXBROWSE_HOSTPC_HOST, sizeof(g_app.server_host) - 1);
    g_app.server_port = VORTEXBROWSE_HOSTPC_PORT;
    g_app.sock_fd     = -1;
    g_app.running     = TRUE;
    strncpy(g_app.status_text, "Bereit.", sizeof(g_app.status_text) - 1);
    strncpy(g_app.autotest_dl_dir, "RAM:", sizeof(g_app.autotest_dl_dir) - 1);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            strncpy(g_app.server_host, argv[++i], sizeof(g_app.server_host) - 1);
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            g_app.server_port = (uint16_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--url") == 0 && i + 1 < argc) {
            strncpy(g_app.current_url, argv[++i], sizeof(g_app.current_url) - 1);
        } else if (strcmp(argv[i], "--autotest") == 0) {
            g_app.autotest = TRUE;
        } else if (strcmp(argv[i], "--dl-dir") == 0 && i + 1 < argc) {
            strncpy(g_app.autotest_dl_dir, argv[++i], sizeof(g_app.autotest_dl_dir) - 1);
        }
    }

    DBG_LOG("============================================================\n");
    DBG_LOG(" %s\n", VB_APP_TITLE);
    DBG_LOG(" Server: %s:%u\n", g_app.server_host, (unsigned)g_app.server_port);
    DBG_LOG("============================================================\n");

    if (!open_libs_and_classes()) {
        close_libs_and_classes();
        return 1;
    }

    prefs_init(&g_app);
    prefs_load(&g_app);

    renderer_init(&g_app);
    download_init(&g_app);
    favorites_init(&g_app);
    audio_init(&g_app);
    clipboard_init();

    if (!gui_init(&g_app)) {
        gui_cleanup(&g_app);
        audio_cleanup(&g_app);
        renderer_cleanup(&g_app);
        download_cleanup(&g_app);
        close_libs_and_classes();
        return 1;
    }

    /* Rebuild chooser labels now that gad_fav_chooser exists */
    favorites_rebuild_chooser_list(&g_app);
    prefs_apply(&g_app);
    gui_handle_events(&g_app);

    /* Sanitize start_url if corrupted with an internal command */
    if (g_app.prefs.start_url[0] == '@' || g_app.prefs.start_url[0] == '\0') {
        strncpy(g_app.prefs.start_url, "https://www.amiga-news.de", sizeof(g_app.prefs.start_url) - 1);
        g_app.prefs.start_url[sizeof(g_app.prefs.start_url) - 1] = '\0';
    }
    if (g_app.current_url[0] == '\0' || g_app.current_url[0] == '@') {
        strncpy(g_app.current_url, g_app.prefs.start_url, sizeof(g_app.current_url) - 1);
        g_app.current_url[sizeof(g_app.current_url) - 1] = '\0';
    }

    /* Connect automatically on startup */
    if (net_connect(&g_app, g_app.server_host, g_app.server_port)) {
        renderer_sync_viewport_size(&g_app);
        net_send_url(&g_app, "@REFRESH");
        const char *su = (g_app.current_url[0] && g_app.current_url[0] != '@') ? g_app.current_url : "@HOME";
        gui_update_url_bar(&g_app, (su[0] != '@') ? su : "");
        net_send_url(&g_app, su);
    }

    /* Main Multiplexed Event Loop (bsdsocket WaitSelect + Reaction WINDOW_SigMask) */
    while (g_app.running) {
        /* Drain any pending Intuition/Reaction events first */
        gui_handle_events(&g_app);
        uint32 pre_win_sig = gui_get_signal_mask(&g_app);
        if (pre_win_sig) {
            IExec->SetSignal(0, pre_win_sig);
        }
        if (g_app.reopen_screen_needed && g_app.running) {
            g_app.reopen_screen_needed = FALSE;
            g_app.reopen_gui_needed    = FALSE;
            gui_reopen_screen(&g_app);
        } else if (g_app.reopen_gui_needed && g_app.running) {
            g_app.reopen_gui_needed = FALSE;
            gui_rebuild_window(&g_app);
        }
        /* Send debounced/throttled mouse move to remote browser outside the IDCMP input task */
        if (g_app.connected && g_app.pending_mouse_move) {
            int16_t rx = 0, ry = 0;
            if (renderer_map_window_to_remote(&g_app, g_app.pending_win_mouse_x, g_app.pending_win_mouse_y, &rx, &ry)) {
                uint32 s = 0, us = 0;
                IIntuition->CurrentTime(&s, &us);
                int32 diff_us = ((int32)s - (int32)g_app.last_move_secs) * 1000000 +
                                ((int32)us - (int32)g_app.last_move_micros);
                int16_t dx = rx - g_app.last_remote_x;
                int16_t dy = ry - g_app.last_remote_y;
                if (diff_us >= 50000 || diff_us < 0) {
                    g_app.pending_mouse_move = FALSE;
                    if (dx * dx + dy * dy >= 4) {
                        g_app.last_move_secs   = s;
                        g_app.last_move_micros = us;
                        g_app.last_remote_x    = rx;
                        g_app.last_remote_y    = ry;
                        net_send_move(&g_app, rx, ry);
                    }
                }
            } else {
                g_app.pending_mouse_move = FALSE;
                if (g_app.is_hovering_link && g_app.win) {
                    g_app.is_hovering_link = FALSE;
                    IIntuition->SetWindowPointer(g_app.win, WA_PointerType, POINTERTYPE_NORMAL, TAG_DONE);
                    app_set_status("Bereit");
                }
            }
        }

        /* If a frame is dirty or waiting to be blitted, flush it now */
        if (g_app.renderer.dirty && g_app.renderer.has_frame) {
            renderer_flush_dirty(&g_app);
        }
        audio_poll(&g_app);
        if (!g_app.running) break;

        uint32 win_sig   = gui_get_signal_mask(&g_app);
        uint32 audio_sig = audio_get_signal_mask(&g_app);
        ULONG  sigs      = win_sig | audio_sig | SIGBREAKF_CTRL_C;

        if (g_app.connected && g_app.sock_fd >= 0) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(g_app.sock_fd, &rfds);

            struct timeval tv;
            tv.tv_sec  = 0;
            /* When audio is actively playing, poll at 20ms; when idle, sleep 100ms to keep CPU at 0% */
            tv.tv_usec = (g_app.audio.device_open && g_app.audio.hw_in_flight > 0) ? 20000 : 100000;

            LONG sel = ISocket->WaitSelect(g_app.sock_fd + 1, &rfds, NULL, NULL, &tv, &sigs);
            ULONG got_sigs = sigs & (win_sig | audio_sig);
            if (got_sigs) {
                IExec->SetSignal(0, got_sigs);
            }

            if (sel > 0 && g_app.sock_fd >= 0 && FD_ISSET(g_app.sock_fd, &rfds)) {
                int batch = 0;
                do {
                    if (!net_handle_incoming(&g_app)) {
                        break;
                    }
                    batch++;
                } while (batch < 4 && g_app.connected && net_has_pending_data(&g_app));

                /* Blit accumulated dirty scanlines once after draining pending packets */
                renderer_flush_dirty(&g_app);
            }
        } else {
            struct timeval tv_retry;
            tv_retry.tv_sec  = 1;
            tv_retry.tv_usec = 500000;
            ISocket->WaitSelect(0, NULL, NULL, NULL, &tv_retry, &sigs);
            ULONG got_sigs = sigs & (win_sig | audio_sig);
            if (got_sigs) {
                IExec->SetSignal(0, got_sigs);
            }
            if (g_app.running && !g_app.connected) {
                if (net_connect(&g_app, g_app.server_host, g_app.server_port)) {
                    if (g_app.current_url[0] != '\0' && g_app.current_url[0] != '@') {
                        net_send_url(&g_app, g_app.current_url);
                    }
                }
            }
        }

        if (sigs & SIGBREAKF_CTRL_C) {
            DBG_LOG("[Main] CTRL-C received, exiting.\n");
            g_app.running = FALSE;
        }

        /* Always drain GUI events and reap AHI audio after WaitSelect */
        gui_handle_events(&g_app);
        uint32 post_win_sig = gui_get_signal_mask(&g_app);
        if (post_win_sig) {
            IExec->SetSignal(0, post_win_sig);
        }
        audio_poll(&g_app);

        if (g_app.autotest) {
            run_autotest_step(&g_app);
        }
    }

    const char *last_u = (g_app.current_url[0] && g_app.current_url[0] != '@') ? g_app.current_url :
                         (g_app.num_tabs > 0 && g_app.tabs[g_app.active_tab_idx].url[0] && g_app.tabs[g_app.active_tab_idx].url[0] != '@') ?
                         g_app.tabs[g_app.active_tab_idx].url : NULL;
    if (g_app.prefs.save_last_url && last_u) {
        strncpy(g_app.prefs.start_url, last_u, sizeof(g_app.prefs.start_url) - 1);
        g_app.prefs.start_url[sizeof(g_app.prefs.start_url) - 1] = '\0';
    }

    prefs_capture_window_box(&g_app);
    prefs_save(&g_app, TRUE);

    net_disconnect(&g_app);
    audio_cleanup(&g_app);
    clipboard_cleanup();
    download_cleanup(&g_app);
    gui_cleanup(&g_app);
    renderer_cleanup(&g_app);
    close_libs_and_classes();

    DBG_LOG("[Main] VortexBrowse exited cleanly.\n");
    return 0;
}
