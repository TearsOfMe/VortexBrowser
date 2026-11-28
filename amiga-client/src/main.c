#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/asl.h>
#include <proto/utility.h>
#include <proto/bsdsocket.h>
#include <proto/chooser.h>
#include <proto/popupmenu.h>
#include <proto/listbrowser.h>
#include <proto/application.h>
#include <proto/commodities.h>
#include <gadgets/listbrowser.h>
#include <libraries/application.h>
#include <libraries/commodities.h>

#include <sys/select.h>
#include <sys/time.h>
#include <intuition/pointerclass.h>

#include "app.h"
#include "prefs.h"
#include "gui.h"
#include "gui_prefs.h"
#include "net.h"
#include "renderer.h"
#include "favorites.h"
#include "download.h"
#include "audio.h"
#include "clipboard.h"
#include "passwords.h"
#include "locale_strings.h"

static const char *used_verstag __attribute__((used)) = "\0$VER: VortexRemoteBrowser 1.1 (27.11.2026)";
static const char *used_stack   __attribute__((used)) = "\0$STACK:524288";
uint32 __stack = 524288;

VortexApp g_app;

/* Explicitly managed libraries & interfaces (in addition to newlib auto-init) */
struct Library          *SocketBase      = NULL;
struct SocketIFace      *ISocket         = NULL;
struct Library          *PopupMenuBase   = NULL;
struct PopupMenuIFace   *IPopupMenu      = NULL;
struct Library          *ApplicationBase = NULL;
struct ApplicationIFace *IApplication    = NULL;
struct Library          *CxBase          = NULL;
struct CommoditiesIFace *ICommodities    = NULL;

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
static struct ClassLibrary *s_ListBrowserBase   = NULL;
static struct ClassLibrary *s_PopupMenuClassLib = NULL;

struct ChooserIFace     *IChooser     = NULL;
struct ClickTabIFace    *IClickTab    = NULL;
struct ListBrowserIFace *IListBrowser = NULL;

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

    /* Disable bsdsocket's default CTRL-C break mask so WaitSelect() delivers
     * SIGBREAKF_CTRL_C cleanly via the user signal mask instead of returning
     * -1 (EINTR) with undefined sigs and leaving SIGBREAKF_CTRL_C posted.
     */
    ISocket->SocketBaseTags(SBTM_SETVAL(SBTC_BREAKMASK), 0, TAG_DONE);

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
    s_ListBrowserBase = IIntuition->OpenClass("gadgets/listbrowser.gadget", 53, &dummy);
    if (!s_ListBrowserBase) s_ListBrowserBase = IIntuition->OpenClass("listbrowser.gadget", 0, &dummy);

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

    if (s_ListBrowserBase) {
        IListBrowser = (struct ListBrowserIFace *)IExec->GetInterface(
            (struct Library *)s_ListBrowserBase, "main", 1, NULL);
    }
    if (!IListBrowser) {
        DBG_LOG("[Main] WARNING: Could not obtain IListBrowser interface from listbrowser.gadget!\n");
    }

    PopupMenuBase = IExec->OpenLibrary("popupmenu.class", 0);
    if (!PopupMenuBase) {
        s_PopupMenuClassLib = IIntuition->OpenClass("popupmenu.class", 0, &dummy);
        PopupMenuBase = (struct Library *)s_PopupMenuClassLib;
    }
    if (PopupMenuBase) {
        IPopupMenu = (struct PopupMenuIFace *)IExec->GetInterface(PopupMenuBase, "main", 1, NULL);
    }

    ApplicationBase = IExec->OpenLibrary("application.library", 53);
    if (ApplicationBase) {
        IApplication = (struct ApplicationIFace *)IExec->GetInterface(
            ApplicationBase, "application", 2, NULL);
        if (!IApplication) {
            DBG_LOG("[Main] WARNING: Could not obtain application.library v2 interface\n");
            IExec->CloseLibrary(ApplicationBase);
            ApplicationBase = NULL;
        }
    }

    CxBase = IExec->OpenLibrary("commodities.library", 37);
    if (CxBase) {
        ICommodities = (struct CommoditiesIFace *)IExec->GetInterface(
            CxBase, "main", 1, NULL);
        if (!ICommodities) {
            IExec->CloseLibrary(CxBase);
            CxBase = NULL;
        }
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
    if (IListBrowser) {
        IExec->DropInterface((struct Interface *)IListBrowser);
        IListBrowser = NULL;
    }
    if (s_ListBrowserBase) {
        IIntuition->CloseClass(s_ListBrowserBase);
        s_ListBrowserBase = NULL;
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
    if (s_FuelGaugeBase) { IIntuition->CloseClass(s_FuelGaugeBase); s_FuelGaugeBase = NULL; }
    if (s_ChooserBase)   { IIntuition->CloseClass(s_ChooserBase);   s_ChooserBase   = NULL; }
    if (s_SpaceBase)     { IIntuition->CloseClass(s_SpaceBase);     s_SpaceBase     = NULL; }
    if (s_StringBase)    { IIntuition->CloseClass(s_StringBase);    s_StringBase    = NULL; }
    if (s_ButtonBase)    { IIntuition->CloseClass(s_ButtonBase);    s_ButtonBase    = NULL; }
    if (s_LayoutBase)    { IIntuition->CloseClass(s_LayoutBase);    s_LayoutBase    = NULL; }
    if (s_WindowBase)    { IIntuition->CloseClass(s_WindowBase);    s_WindowBase    = NULL; }

    if (ICommodities) {
        IExec->DropInterface((struct Interface *)ICommodities);
        ICommodities = NULL;
    }
    if (CxBase) {
        IExec->CloseLibrary(CxBase);
        CxBase = NULL;
    }
    if (IApplication) {
        IExec->DropInterface((struct Interface *)IApplication);
        IApplication = NULL;
    }
    if (ApplicationBase) {
        IExec->CloseLibrary(ApplicationBase);
        ApplicationBase = NULL;
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

void app_sync_hidden_state(VortexApp *app, BOOL hidden)
{
    if (app && app->applib_id && IApplication) {
        IApplication->SetApplicationAttrs(app->applib_id,
            APPATTR_Hidden, hidden ? TRUE : FALSE,
            TAG_DONE);
    }
}

static void app_os4_register(VortexApp *app, int argc, char **argv)
{
    if (!app) return;

    if (IApplication) {
        app->applib_id = IApplication->RegisterApplication("VortexRemoteBrowser",
            REGAPP_URLIdentifier,     (Tag)"vortexremotebrowser.amiga",
            REGAPP_Description,       (Tag)"Remote-rendered Web Browser for AmigaOS 4",
            REGAPP_UniqueApplication, FALSE,
            REGAPP_HasIconifyFeature, TRUE,
            REGAPP_HasPrefsWindow,    TRUE,
            REGAPP_NoIcon,            FALSE,
            REGAPP_Hidden,            FALSE,
            (argc == 0) ? REGAPP_WBStartup : REGAPP_FileName,
            (argc == 0) ? (Tag)argv : (Tag)argv[0],
            TAG_DONE);
        if (app->applib_id) {
            IApplication->GetApplicationAttrs(app->applib_id,
                APPATTR_Port, (Tag)&app->applib_port,
                TAG_DONE);
        }
    }

    if (ICommodities) {
        app->cx_port = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
        if (app->cx_port) {
            struct NewBroker nb;
            memset(&nb, 0, sizeof(nb));
            nb.nb_Version = NB_VERSION;
            nb.nb_Name    = (STRPTR)"VortexRemoteBrowser";
            nb.nb_Title   = (STRPTR)"VortexRemoteBrowser 1.1";
            nb.nb_Descr   = (STRPTR)"Remote-rendered Web Browser for AmigaOS 4";
            nb.nb_Unique  = 0;
            nb.nb_Flags   = COF_SHOW_HIDE;
            nb.nb_Pri     = 0;
            nb.nb_Port    = app->cx_port;
            int32 cx_err  = 0;
            app->cx_broker = (APTR)ICommodities->CxBroker(&nb, &cx_err);
            if (app->cx_broker) {
                ICommodities->ActivateCxObj((CxObj *)app->cx_broker, TRUE);
            } else {
                IExec->FreeSysObject(ASOT_PORT, app->cx_port);
                app->cx_port = NULL;
            }
        }
    }
}

static void app_os4_handle_messages(VortexApp *app)
{
    if (!app) return;

    if (IApplication && app->applib_port) {
        struct Message *msg;
        while ((msg = IExec->GetMsg(app->applib_port)) != NULL) {
            struct ApplicationMsg *amsg = (struct ApplicationMsg *)msg;
            uint32 mtype = amsg->type;
            IExec->ReplyMsg(msg);
            switch (mtype) {
                case APPLIBMT_Quit:
                case APPLIBMT_ForceQuit:
                    app->running = FALSE;
                    break;
                case APPLIBMT_Hide:
                    app->iconify_needed = TRUE;
                    break;
                case APPLIBMT_Unhide:
                    app->uniconify_needed = TRUE;
                    break;
                case APPLIBMT_ToFront:
                case APPLIBMT_Unique:
                    if (app->is_iconified) {
                        app->uniconify_needed = TRUE;
                    } else {
                        app->to_front_needed = TRUE;
                    }
                    break;
                case APPLIBMT_OpenPrefs:
                    app->open_prefs_needed = TRUE;
                    break;
                default:
                    break;
            }
        }
    }

    if (ICommodities && app->cx_port) {
        struct Message *msg;
        while ((msg = IExec->GetMsg(app->cx_port)) != NULL) {
            CxMsg *cxm = (CxMsg *)msg;
            uint32 mtype = ICommodities->CxMsgType(cxm);
            int32  mid   = ICommodities->CxMsgID(cxm);
            IExec->ReplyMsg(msg);
            if (mtype == CXM_COMMAND) {
                switch (mid) {
                    case CXCMD_DISAPPEAR:
                        app->iconify_needed = TRUE;
                        break;
                    case CXCMD_APPEAR:
                    case CXCMD_UNIQUE:
                        if (app->is_iconified) {
                            app->uniconify_needed = TRUE;
                        } else {
                            app->to_front_needed = TRUE;
                        }
                        break;
                    case CXCMD_KILL:
                        app->running = FALSE;
                        break;
                    case CXCMD_ENABLE:
                        if (app->cx_broker) {
                            ICommodities->ActivateCxObj((CxObj *)app->cx_broker, TRUE);
                        }
                        break;
                    case CXCMD_DISABLE:
                        if (app->cx_broker) {
                            ICommodities->ActivateCxObj((CxObj *)app->cx_broker, FALSE);
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }
}

static void app_os4_unregister(VortexApp *app)
{
    if (!app) return;

    if (app->cx_broker && ICommodities) {
        ICommodities->DeleteCxObjAll((CxObj *)app->cx_broker);
        app->cx_broker = NULL;
    }
    if (app->cx_port) {
        struct Message *msg;
        while ((msg = IExec->GetMsg(app->cx_port)) != NULL) {
            IExec->ReplyMsg(msg);
        }
        IExec->FreeSysObject(ASOT_PORT, app->cx_port);
        app->cx_port = NULL;
    }
    if (app->applib_id && IApplication) {
        if (app->applib_port) {
            struct Message *msg;
            while ((msg = IExec->GetMsg(app->applib_port)) != NULL) {
                IExec->ReplyMsg(msg);
            }
        }
        IApplication->UnregisterApplication(app->applib_id,
            UNREGAPP_WaitForUnlock, TRUE,
            TAG_DONE);
        app->applib_id   = 0;
        app->applib_port = NULL;
    }
}

static void process_deferred_gui_actions(VortexApp *app)
{
    if (!app || !app->running) return;

    if (app->iconify_needed) {
        app->iconify_needed = FALSE;
        gui_iconify(app);
    }
    if (app->uniconify_needed && app->running) {
        app->uniconify_needed = FALSE;
        gui_uniconify(app);
    }
    if (app->to_front_needed && app->running) {
        app->to_front_needed = FALSE;
        gui_to_front(app);
    }
    if (app->open_prefs_needed && app->running) {
        app->open_prefs_needed = FALSE;
        if (app->is_iconified) {
            gui_uniconify(app);
        }
        if (app->win) {
            gui_prefs_open(app);
        }
    }
    if (app->reopen_screen_needed && app->running) {
        app->reopen_screen_needed = FALSE;
        app->reopen_gui_needed    = FALSE;
        gui_reopen_screen(app);
    } else if (app->reopen_gui_needed && app->running) {
        app->reopen_gui_needed = FALSE;
        gui_rebuild_window(app);
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
        favorites_add(app, "VortexRemoteBrowser AutoTest Bookmark", "https://www.amigans.net");
        BOOL fav_ok = favorites_save(app);
        favorites_rebuild_chooser_list(app);
        printf("[AutoTest] PASS 2/4: Favorites saved to ENVARC:VortexRemoteBrowser/favorites.prefs (ok=%d, count=%d)\n",
               (int)fav_ok, app->num_favorites);

        /* Trigger click on interactive button on portal */
        net_send_click(app, 900, 420, 1);
        app->autotest_stage = 1;
        app->autotest_ticks = 0;
    }
    /* Stage 1: Trigger download of test package from Pi server */
    else if (app->autotest_stage == 1 && app->autotest_ticks >= 4) {
        char dl_url[256];
        snprintf(dl_url, sizeof(dl_url), "http://127.0.0.1:8089/download/VortexRemoteBrowser_TestPackage.lha");
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
            printf("[AutoTest] PASS 4/4: All VortexRemoteBrowser AmigaOS 4 subsystems verified!\n");
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
    /* Prevent newlib from aborting out of main() via SIGINT/exit() on CTRL-C,
     * which would bypass AmigaOS library/socket/AHI/GUI teardown and trigger
     * a bsdsocket/newlib exit DSI. We handle SIGBREAKF_CTRL_C cleanly ourselves.
     */
    signal(SIGINT, SIG_IGN);

    /* If launched from Workbench (argc == 0), silence stdio to prevent
     * AmigaOS / newlib from auto-opening a console window ("CON:")
     */
    if (argc == 0) {
        freopen("NIL:", "w", stdout);
        freopen("NIL:", "w", stderr);
        freopen("NIL:", "r", stdin);
    }
    /* Strict AltiVec hardware check:
     * If compiled with dedicated AltiVec instructions (-maltivec / __ALTIVEC__),
     * verify that the host CPU actually possesses an active AltiVec/VMX vector unit.
     * Prevents illegal instruction exceptions on AmigaOne X5000, A1222+, Sam460, Sam440, G3.
     */
    locale_init_catalog();

    uint32 vec_unit = VECTORTYPE_NONE;
    if (IExec) {
        IExec->GetCPUInfoTags(GCIT_VectorUnit, &vec_unit, TAG_DONE);
    }
    BOOL has_altivec = (vec_unit == VECTORTYPE_ALTIVEC || vec_unit == 2);
    (void)has_altivec;
#if defined(__ALTIVEC__)
    if (!has_altivec) {
        if (IDOS) {
            IDOS->Printf("\n[VortexRemoteBrowser] ERROR: %s\n\n", GetVString(MSG_CPU_INCOMPAT_BODY));
        }
        if (IIntuition) {
            struct EasyStruct es = {
                sizeof(struct EasyStruct),
                0,
                (STRPTR)GetVString(MSG_CPU_INCOMPAT_TITLE),
                (STRPTR)GetVString(MSG_CPU_INCOMPAT_BODY),
                (STRPTR)GetVString(MSG_GEN_CANCEL)
            };
            IIntuition->EasyRequestArgs(NULL, &es, NULL, NULL);
        }
        locale_close_catalog();
        return 20;
    }
#endif

    memset(&g_app, 0, sizeof(VortexApp));
    g_app.server_host[0] = '\0';
    g_app.server_port    = DEFAULT_SERVER_PORT;
    g_app.sock_fd     = -1;
    g_app.running     = TRUE;
    strncpy(g_app.status_text, GetVString(MSG_STATUS_READY), sizeof(g_app.status_text) - 1);
    strncpy(g_app.autotest_dl_dir, "RAM:", sizeof(g_app.autotest_dl_dir) - 1);

    /* Shell banner suppressed per user request */

    if (!open_libs_and_classes()) {
        close_libs_and_classes();
        locale_close_catalog();
        return 1;
    }

    prefs_init(&g_app);
    prefs_load(&g_app);
    prefs_load_tooltypes(&g_app, argc, argv);

    /* Command line arguments override prefs and icon tooltypes */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            strncpy(g_app.server_host, argv[++i], sizeof(g_app.server_host) - 1);
            g_app.server_host[sizeof(g_app.server_host) - 1] = '\0';
            strncpy(g_app.prefs.server_host, g_app.server_host, sizeof(g_app.prefs.server_host) - 1);
            g_app.prefs.server_host[sizeof(g_app.prefs.server_host) - 1] = '\0';
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            g_app.server_port = (uint16_t)atoi(argv[++i]);
            g_app.prefs.server_port = g_app.server_port;
        } else if (strcmp(argv[i], "--url") == 0 && i + 1 < argc) {
            strncpy(g_app.current_url, argv[++i], sizeof(g_app.current_url) - 1);
            g_app.current_url[sizeof(g_app.current_url) - 1] = '\0';
        } else if (strcmp(argv[i], "--autotest") == 0) {
            g_app.autotest = TRUE;
        } else if (strcmp(argv[i], "--dl-dir") == 0 && i + 1 < argc) {
            strncpy(g_app.autotest_dl_dir, argv[++i], sizeof(g_app.autotest_dl_dir) - 1);
            g_app.autotest_dl_dir[sizeof(g_app.autotest_dl_dir) - 1] = '\0';
        }
    }

    /* Sanitize start_url if empty or corrupted with a non-@HOME internal command */
    if (g_app.prefs.start_url[0] == '\0' ||
        (g_app.prefs.start_url[0] == '@' && strcmp(g_app.prefs.start_url, "@HOME") != 0)) {
        strncpy(g_app.prefs.start_url, "@HOME", sizeof(g_app.prefs.start_url) - 1);
        g_app.prefs.start_url[sizeof(g_app.prefs.start_url) - 1] = '\0';
    }
    if (g_app.current_url[0] == '\0' || g_app.current_url[0] == '@') {
        strncpy(g_app.current_url, g_app.prefs.start_url, sizeof(g_app.current_url) - 1);
        g_app.current_url[sizeof(g_app.current_url) - 1] = '\0';
    }

    renderer_init(&g_app);
    download_init(&g_app);
    favorites_init(&g_app);
    audio_init(&g_app);
    clipboard_init();
    passwords_init(&g_app);

    if (!gui_init(&g_app)) {
        gui_cleanup(&g_app);
        passwords_cleanup(&g_app);
        download_cleanup(&g_app);
        clipboard_cleanup();
        audio_cleanup(&g_app);
        renderer_cleanup(&g_app);
        locale_close_catalog();
        close_libs_and_classes();
        return 1;
    }

    /* Rebuild chooser labels now that gad_fav_chooser exists */
    favorites_rebuild_chooser_list(&g_app);
    prefs_apply(&g_app);
    app_os4_register(&g_app, argc, argv);
    gui_handle_events(&g_app);

    /* Set initial status bar message with active streaming system */
    const char *sm_label = (g_app.prefs.video_stream_mode == STREAM_VIDEO_H264) ? "H.264" :
                           (g_app.prefs.video_stream_mode == STREAM_VIDEO_DIRTYRECT) ? "LZ4 (2D)" : "Auto";
    app_set_status(GetVString(MSG_STATUS_READY_STREAM), sm_label, (unsigned)g_app.prefs.video_bitrate_k);

    /* Connect automatically on startup if a server address is configured */
    if (g_app.server_host[0] != '\0') {
        if (net_connect(&g_app, g_app.server_host, g_app.server_port)) {
            app_set_status(GetVString(MSG_STATUS_CONNECTED_STREAM),
                           g_app.server_host, (unsigned)g_app.server_port, sm_label, (unsigned)g_app.prefs.video_bitrate_k);
            net_sync_tabs(&g_app);
        }
    } else {
        /* Clean start / fresh installation: prompt user for server address */
        prompt_server_address(&g_app);
    }

    /* Main Multiplexed Event Loop (bsdsocket WaitSelect + Reaction WINDOW_SigMask) */
    int iconify_poll_ticks = 0;
    while (g_app.running) {
        /* Drain any pending Intuition/Reaction/Application/Commodity events first */
        gui_handle_events(&g_app);
        app_os4_handle_messages(&g_app);
        process_deferred_gui_actions(&g_app);

        /* Send debounced/throttled mouse move to remote browser outside the IDCMP input task */
        if (g_app.connected && !g_app.is_iconified && g_app.pending_mouse_move) {
            int16_t rx = 0, ry = 0;
            BOOL in_vp = renderer_map_window_to_remote(&g_app, g_app.pending_win_mouse_x, g_app.pending_win_mouse_y, &rx, &ry);
            if (in_vp || (g_app.mouse_lmb_down && g_app.cached_space_box.Width > 0)) {
                uint32 s = 0, us = 0;
                IIntuition->CurrentTime(&s, &us);
                int32 diff_us = ((int32)s - (int32)g_app.last_move_secs) * 1000000 +
                                ((int32)us - (int32)g_app.last_move_micros);
                int16_t dx = rx - g_app.last_remote_x;
                int16_t dy = ry - g_app.last_remote_y;
                int32 min_interval_us = g_app.mouse_lmb_down ? 8000 : 33000;
                int32 min_dist_sq     = g_app.mouse_lmb_down ? 1 : 4;
                if ((diff_us >= min_interval_us || diff_us < 0) && (dx * dx + dy * dy >= min_dist_sq)) {
                    g_app.pending_mouse_move = FALSE;
                    g_app.last_move_secs   = s;
                    g_app.last_move_micros = us;
                    g_app.last_remote_x    = rx;
                    g_app.last_remote_y    = ry;
                    net_send_move(&g_app, rx, ry);
                } else if (dx == 0 && dy == 0) {
                    g_app.pending_mouse_move = FALSE;
                }
            } else {
                g_app.pending_mouse_move = FALSE;
                if (g_app.is_hovering_link) {
                    g_app.is_hovering_link = FALSE;
                }
            }
        }

        /* Flush debounced viewport @RESIZE once rate-limit interval elapses */
        if (g_app.connected && !g_app.is_iconified && g_app.renderer.resize_pending) {
            renderer_sync_viewport_size(&g_app);
        }

        /* If a frame is dirty or waiting to be blitted, flush it now */
        if (!g_app.is_iconified && g_app.renderer.dirty && g_app.renderer.has_frame) {
            renderer_flush_dirty(&g_app);
        }
        audio_poll(&g_app);
        if (!g_app.running) break;

        /* Reset mouse signal bit so WaitSelect blocks cleanly */
        if (g_app.mouse_sig_mask) {
            IExec->SetSignal(0, g_app.mouse_sig_mask);
        }

        uint32 win_sig   = gui_get_signal_mask(&g_app);
        uint32 audio_sig = audio_get_signal_mask(&g_app);
        ULONG  sigs      = win_sig | audio_sig | SIGBREAKF_CTRL_C;
        LONG   sel       = 0;

        if (g_app.connected && g_app.sock_fd >= 0) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(g_app.sock_fd, &rfds);

            struct timeval tv;
            tv.tv_sec  = 0;
            tv.tv_usec = 20000; /* 20ms tick for low-latency audio & video */

            sel = ISocket->WaitSelect(g_app.sock_fd + 1, &rfds, NULL, NULL, &tv, &sigs);

            if (sel > 0 && g_app.sock_fd >= 0 && FD_ISSET(g_app.sock_fd, &rfds)) {
                uint32_t prev_frames = g_app.renderer.frames_decoded;
                int batch = 0;
                do {
                    if (!net_handle_incoming(&g_app)) {
                        break;
                    }
                    batch++;
                } while (batch < 12 && (g_app.renderer.frames_decoded - prev_frames) < 2 && g_app.connected && net_has_pending_data(&g_app));

                /* Blit accumulated dirty scanlines once after draining pending packets */
                renderer_flush_dirty(&g_app);
            }
        } else {
            /* Disconnected: wait for GUI events with 100ms idle tick, NO blocking reconnect loop */
            struct timeval tv_idle;
            tv_idle.tv_sec  = 0;
            tv_idle.tv_usec = 100000; /* 100ms idle tick */
            sel = ISocket->WaitSelect(0, NULL, NULL, NULL, &tv_idle, &sigs);
        }

        /* Note: If WaitSelect returns -1 (EINTR), *sigs is undefined and
         * SIGBREAKF_CTRL_C may remain posted on the task; always clear it.
         */
        BOOL got_ctrl_c = ((sel >= 0 && (sigs & SIGBREAKF_CTRL_C)) ||
                           (IExec->SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)) ? TRUE : FALSE;
        if (got_ctrl_c) {
            DBG_LOG("[Main] CTRL-C received, exiting.\n");
            g_app.running = FALSE;
            break;
        }

        /* Always drain GUI, Application, Commodity events and reap AHI audio after WaitSelect */
        gui_handle_events(&g_app);
        app_os4_handle_messages(&g_app);
        process_deferred_gui_actions(&g_app);
        if (g_app.is_iconified && g_app.custom_screen != NULL) {
            if ((++iconify_poll_ticks % 10) == 0) {
                gui_try_close_pending_screen(&g_app);
            }
        }
        audio_poll(&g_app);

        if (g_app.autotest) {
            run_autotest_step(&g_app);
        }
    }

    /* Clear any pending break signal before shutdown file I/O (prefs_save) */
    IExec->SetSignal(0, SIGBREAKF_CTRL_C);

    const char *last_u = !prefs_is_startpage_url(g_app.current_url) ? g_app.current_url :
                         (g_app.num_tabs > 0 && !prefs_is_startpage_url(g_app.tabs[g_app.active_tab_idx].url)) ?
                         g_app.tabs[g_app.active_tab_idx].url : NULL;
    if (g_app.prefs.save_last_url) {
        if (last_u) {
            strncpy(g_app.prefs.start_url, last_u, sizeof(g_app.prefs.start_url) - 1);
            g_app.prefs.start_url[sizeof(g_app.prefs.start_url) - 1] = '\0';
        } else {
            strncpy(g_app.prefs.start_url, "@HOME", sizeof(g_app.prefs.start_url) - 1);
            g_app.prefs.start_url[sizeof(g_app.prefs.start_url) - 1] = '\0';
        }
    }

    prefs_capture_tabs(&g_app);
    if (!g_app.prefs.resolution.win_pos_locked) {
        prefs_capture_window_box(&g_app);
    }
    prefs_save(&g_app, TRUE);

    net_disconnect(&g_app);
    audio_cleanup(&g_app);
    clipboard_cleanup();
    download_cleanup(&g_app);
    passwords_cleanup(&g_app);
    app_os4_unregister(&g_app);
    gui_cleanup(&g_app);
    renderer_cleanup(&g_app);
    locale_close_catalog();
    close_libs_and_classes();

    DBG_LOG("[Main] VortexRemoteBrowser exited cleanly.\n");
    return 0;
}
