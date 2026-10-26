#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/bsdsocket.h>
#include <proto/muimaster.h>
#include <inline4/muimaster.h>
#include <proto/popupmenu.h>
#include <libraries/mui.h>

#include <sys/select.h>
#include <sys/time.h>

#include "app.h"
#include "gui_mui.h"
#include "net.h"
#include "renderer.h"
#include "favorites.h"
#include "download.h"
#include "audio.h"

VortexApp g_app;

/* Required interfaces */
struct Library        *SocketBase    = NULL;
struct SocketIFace    *ISocket       = NULL;
struct Library        *MUIMasterBase = NULL;
struct MUIMasterIFace *IMUIMaster    = NULL;
struct Library        *PopupMenuBase = NULL;
struct PopupMenuIFace *IPopupMenu    = NULL;

/* Define IChooser dummy as NULL so favorites.c links cleanly in MUI build */
struct ChooserIFace   *IChooser      = NULL;

static struct ClassLibrary *s_PopupMenuClassLib = NULL;

#ifndef DoMethod
#define DoMethod(obj, ...) IIntuition->IDoMethod((Object *)(obj), __VA_ARGS__)
#endif

static BOOL open_libs(void)
{
    SocketBase = IExec->OpenLibrary("bsdsocket.library", 4);
    if (!SocketBase) {
        printf("[Main-MUI] ERROR: Could not open bsdsocket.library v4!\n");
        return FALSE;
    }
    ISocket = (struct SocketIFace *)IExec->GetInterface(SocketBase, "main", 1, NULL);
    if (!ISocket) {
        printf("[Main-MUI] ERROR: Could not obtain ISocket interface!\n");
        return FALSE;
    }

    MUIMasterBase = IExec->OpenLibrary("muimaster.library", 19);
    if (!MUIMasterBase) {
        printf("[Main-MUI] ERROR: Could not open muimaster.library v19!\n");
        return FALSE;
    }
    IMUIMaster = (struct MUIMasterIFace *)IExec->GetInterface(MUIMasterBase, "main", 1, NULL);
    if (!IMUIMaster) {
        printf("[Main-MUI] ERROR: Could not obtain IMUIMaster interface!\n");
        return FALSE;
    }

    Class *dummy = NULL;
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

static void close_libs(void)
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

    if (IMUIMaster) {
        IExec->DropInterface((struct Interface *)IMUIMaster);
        IMUIMaster = NULL;
    }
    if (MUIMasterBase) {
        IExec->CloseLibrary(MUIMasterBase);
        MUIMasterBase = NULL;
    }

    if (ISocket) {
        IExec->DropInterface((struct Interface *)ISocket);
        ISocket = NULL;
    }
    if (SocketBase) {
        IExec->CloseLibrary(SocketBase);
        SocketBase = NULL;
    }
}

int main(int argc, char **argv)
{
    memset(&g_app, 0, sizeof(VortexApp));
    strncpy(g_app.server_host, "192.168.178.2", sizeof(g_app.server_host) - 1);
    g_app.server_port = 7788;
    g_app.sock_fd     = -1;
    g_app.running     = TRUE;
    strncpy(g_app.status_text, "Bereit.", sizeof(g_app.status_text) - 1);
    strncpy(g_app.autotest_dl_dir, "RAM:", sizeof(g_app.autotest_dl_dir) - 1);

    /* Optional override via AmigaOS ENV variables */
    char env_host[120];
    if (IDOS->GetVar("VortexBrowse/server_host", env_host, sizeof(env_host), GVF_GLOBAL_ONLY) > 0) {
        size_t len = strlen(env_host);
        while (len > 0 && (env_host[len - 1] == '\n' || env_host[len - 1] == '\r' || env_host[len - 1] == ' ')) {
            env_host[--len] = '\0';
        }
        if (len > 0) {
            snprintf(g_app.server_host, sizeof(g_app.server_host), "%s", env_host);
        }
    }

    char env_port[32];
    if (IDOS->GetVar("VortexBrowse/server_port", env_port, sizeof(env_port), GVF_GLOBAL_ONLY) > 0) {
        int p = atoi(env_port);
        if (p > 0 && p <= 65535) {
            g_app.server_port = (uint16_t)p;
        }
    }

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

    printf("============================================================\n");
    printf(" VortexBrowse (MUI Edition) 1.0 (AmigaOS 4.1 PowerPC)\n");
    printf(" Server: %s:%u\n", g_app.server_host, (unsigned)g_app.server_port);
    printf("============================================================\n");

    if (!open_libs()) {
        close_libs();
        return 1;
    }

    renderer_init(&g_app);
    download_init(&g_app);
    favorites_init(&g_app);
    audio_init(&g_app);

    if (!gui_mui_init(&g_app)) {
        audio_cleanup(&g_app);
        renderer_cleanup(&g_app);
        download_cleanup(&g_app);
        close_libs();
        return 1;
    }

    /* Sanitize start_url if corrupted with an internal command */
    if (g_app.prefs.start_url[0] == '@' || g_app.prefs.start_url[0] == '\0') {
        strncpy(g_app.prefs.start_url, "https://www.amiga-news.de", sizeof(g_app.prefs.start_url) - 1);
        g_app.prefs.start_url[sizeof(g_app.prefs.start_url) - 1] = '\0';
    }
    if (g_app.current_url[0] == '\0' || g_app.current_url[0] == '@') {
        strncpy(g_app.current_url, g_app.prefs.start_url, sizeof(g_app.current_url) - 1);
        g_app.current_url[sizeof(g_app.current_url) - 1] = '\0';
    }

    /* Auto-connect on startup */
    if (net_connect(&g_app, g_app.server_host, g_app.server_port)) {
        const char *su = (g_app.current_url[0] && g_app.current_url[0] != '@') ? g_app.current_url : "@HOME";
        gui_update_url_bar(&g_app, (su[0] != '@') ? su : "");
        net_send_url(&g_app, su);
    }

    /* Multiplexed Event Loop (MUI MUIM_Application_NewInput + bsdsocket WaitSelect) */
    ULONG sigs = 0;
    while (g_app.running) {
        ULONG id = DoMethod(g_app.mui_app, MUIM_Application_NewInput, &sigs);
        if (id == MUIV_Application_ReturnID_Quit) {
            g_app.running = FALSE;
            break;
        } else if (id != 0) {
            gui_mui_handle_action(&g_app, id);
        }

        gui_mui_handle_events(&g_app);
        audio_poll(&g_app);
        if (!g_app.running) break;

        ULONG audio_sig = audio_get_signal_mask(&g_app);
        ULONG wait_sigs = sigs | audio_sig | SIGBREAKF_CTRL_C;

        if (g_app.connected && g_app.sock_fd >= 0) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(g_app.sock_fd, &rfds);

            struct timeval tv;
            tv.tv_sec  = 0;
            tv.tv_usec = 20000; /* 20ms tick */

            LONG sel = ISocket->WaitSelect(g_app.sock_fd + 1, &rfds, NULL, NULL, &tv, &wait_sigs);
            if (sel > 0 && g_app.sock_fd >= 0 && FD_ISSET(g_app.sock_fd, &rfds)) {
                int batch = 0;
                do {
                    if (!net_handle_incoming(&g_app)) {
                        break;
                    }
                    batch++;
                } while (batch < 6 && g_app.connected && net_has_pending_data(&g_app));

                gui_mui_flush_dirty(&g_app);
            }
        } else {
            struct timeval tv_retry;
            tv_retry.tv_sec  = 1;
            tv_retry.tv_usec = 500000;
            ISocket->WaitSelect(0, NULL, NULL, NULL, &tv_retry, &wait_sigs);
            if (g_app.running && !g_app.connected) {
                if (net_connect(&g_app, g_app.server_host, g_app.server_port)) {
                    if (g_app.current_url[0] != '\0' && g_app.current_url[0] != '@') {
                        net_send_url(&g_app, g_app.current_url);
                    }
                }
            }
        }

        if (wait_sigs & SIGBREAKF_CTRL_C) {
            printf("[Main-MUI] CTRL-C received, exiting.\n");
            g_app.running = FALSE;
        }

        /* Forward received signals to next iteration's NewInput */
        sigs = wait_sigs;

        gui_mui_handle_events(&g_app);
        audio_poll(&g_app);
    }

    const char *last_u = (g_app.current_url[0] && g_app.current_url[0] != '@') ? g_app.current_url :
                         (g_app.num_tabs > 0 && g_app.tabs[g_app.active_tab_idx].url[0] && g_app.tabs[g_app.active_tab_idx].url[0] != '@') ?
                         g_app.tabs[g_app.active_tab_idx].url : NULL;
    if (g_app.prefs.save_last_url && last_u) {
        strncpy(g_app.prefs.start_url, last_u, sizeof(g_app.prefs.start_url) - 1);
        g_app.prefs.start_url[sizeof(g_app.prefs.start_url) - 1] = '\0';
    }

    if (g_app.prefs.start_url[0] != '\0' && g_app.prefs.start_url[0] != '@') {
        prefs_save(&g_app, TRUE);
    }

    net_disconnect(&g_app);
    audio_cleanup(&g_app);
    download_cleanup(&g_app);
    gui_mui_cleanup(&g_app);
    renderer_cleanup(&g_app);
    close_libs();

    printf("[Main-MUI] VortexBrowse-MUI exited cleanly.\n");
    return 0;
}
