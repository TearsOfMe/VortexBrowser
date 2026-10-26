#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/bsdsocket.h>
#include <proto/muimaster.h>
#include <inline4/muimaster.h>
#include <proto/popupmenu.h>
#include <classes/popupmenu.h>
#include <libraries/mui.h>
#include <libraries/gadtools.h>
#include <devices/inputevent.h>
#include <sys/select.h>
#include <sys/time.h>

#include "app.h"
#include "gui_mui.h"
#include "net.h"
#include "renderer.h"
#include "favorites.h"

extern struct SocketIFace *ISocket;
extern struct PopupMenuIFace *IPopupMenu;

#ifndef DoMethod
#define DoMethod(obj, ...) IIntuition->IDoMethod((Object *)(obj), __VA_ARGS__)
#endif
#ifndef DoSuperMethodA
#define DoSuperMethodA(cl, obj, msg) IIntuition->IDoSuperMethodA((cl), (Object *)(obj), (Msg)(msg))
#endif
#undef set
#define set(obj, attr, val) IIntuition->SetAttrs((Object *)(obj), (attr), (val), TAG_DONE)
#undef get
#define get(obj, attr, valptr) IIntuition->GetAttr((attr), (Object *)(obj), (uint32 *)(valptr))


#define VORTEXBROWSE_RPI_HOST    "192.168.178.2"
#define VORTEXBROWSE_RPI_PORT    7788
#define VORTEXBROWSE_HOSTPC_HOST "192.168.178.60"
#define VORTEXBROWSE_HOSTPC_PORT 9757

static const char *s_server_entries[] = {
    "RasPi 4 (192.168.178.2:7788)",
    "Host-PC (192.168.178.60:9757)",
    "Server manuell...",
    NULL
};

static struct NewMenu s_mui_menu[] = {
    { NM_TITLE, (char *)"Projekt", NULL, 0, 0, (APTR)MID_PROJECT },
    { NM_ITEM,  (char *)"Ueber VortexBrowse...", (char *)"?", 0, 0, (APTR)MID_ABOUT },
    { NM_ITEM,  (char *)"Cache / Speicher leeren", (char *)"K", 0, 0, (APTR)MID_CLEAR_CACHE },
    { NM_ITEM,  NM_BARLABEL, NULL, 0, 0, (APTR)0 },
    { NM_ITEM,  (char *)"Beenden", (char *)"Q", 0, 0, (APTR)MID_QUIT },

    { NM_TITLE, (char *)"Navigation", NULL, 0, 0, (APTR)MID_NAV },
    { NM_ITEM,  (char *)"Zurueck", (char *)"Z", 0, 0, (APTR)MID_BACK },
    { NM_ITEM,  (char *)"Vorwaerts", (char *)"V", 0, 0, (APTR)MID_FORWARD },
    { NM_ITEM,  (char *)"Seite neu laden", (char *)"R", 0, 0, (APTR)MID_RELOAD },
    { NM_ITEM,  (char *)"Ladevorgang stoppen", (char *)"S", 0, 0, (APTR)MID_STOP },
    { NM_ITEM,  (char *)"Startseite", (char *)"H", 0, 0, (APTR)MID_HOME },

    { NM_TITLE, (char *)"Lesezeichen", NULL, 0, 0, (APTR)MID_BOOKMARKS },
    { NM_ITEM,  (char *)"Aktuelle Seite hinzufuegen (+Fav)", (char *)"+", 0, 0, (APTR)MID_ADD_FAV },
    { NM_ITEM,  NM_BARLABEL, NULL, 0, 0, (APTR)0 },
    { NM_ITEM,  (char *)"Amiga-News.de", (char *)"1", 0, 0, (APTR)MID_BM_AMIGANEWS },
    { NM_ITEM,  (char *)"OS4Depot.net", (char *)"2", 0, 0, (APTR)MID_BM_OS4DEPOT },
    { NM_ITEM,  (char *)"Amigans.net", (char *)"3", 0, 0, (APTR)MID_BM_AMIGANS },
    { NM_ITEM,  (char *)"Aminet.net", (char *)"4", 0, 0, (APTR)MID_BM_AMINET },
    { NM_ITEM,  (char *)"YouTube", (char *)"5", 0, 0, (APTR)MID_BM_YOUTUBE },
    { NM_ITEM,  (char *)"Wikipedia (AmigaOS)", (char *)"6", 0, 0, (APTR)MID_BM_WIKI },

    { NM_TITLE, (char *)"Server", NULL, 0, 0, (APTR)MID_SERVER },
    { NM_ITEM,  (char *)"Raspberry Pi 4 (192.168.178.2:7788)", NULL, 0, 0, (APTR)MID_SERVER_RPI },
    { NM_ITEM,  (char *)"Host-PC (192.168.178.60:9757)", NULL, 0, 0, (APTR)MID_SERVER_PC },
    { NM_ITEM,  (char *)"Serveradresse manuell eingeben...", NULL, 0, 0, (APTR)MID_SERVER_CUSTOM },
    { NM_ITEM,  NM_BARLABEL, NULL, 0, 0, (APTR)0 },
    { NM_ITEM,  (char *)"Verbindung trennen", NULL, 0, 0, (APTR)MID_DISCONNECT },

    { NM_END,   NULL, NULL, 0, 0, (APTR)0 }
};

/* -------------------------------------------------------------------------
 * Custom MUI Viewport Class (MUIC_Area Subclass)
 * ------------------------------------------------------------------------- */
struct ViewportData {
    struct MUI_EventHandlerNode eh;
    VortexApp                  *app;
};

static uint32 Viewport_AskMinMax(struct IClass *cl, Object *obj, struct MUIP_AskMinMax *msg)
{
    DoSuperMethodA(cl, obj, (Msg)msg);

    msg->MinMaxInfo->MinWidth  += 640;
    msg->MinMaxInfo->DefWidth  += 1024;
    msg->MinMaxInfo->MaxWidth  += MUI_MAXMAX;

    msg->MinMaxInfo->MinHeight += 400;
    msg->MinMaxInfo->DefHeight += 640;
    msg->MinMaxInfo->MaxHeight += MUI_MAXMAX;

    return 0;
}

static uint32 Viewport_Draw(struct IClass *cl, Object *obj, struct MUIP_Draw *msg)
{
    DoSuperMethodA(cl, obj, (Msg)msg);

    if (msg->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) {
        struct ViewportData *data = INST_DATA(cl, obj);
        VortexApp *app = data->app ? data->app : &g_app;
        struct RastPort *rp = _rp(obj);
        if (app && rp) {
            struct IBox box;
            box.Left   = _mleft(obj);
            box.Top    = _mtop(obj);
            box.Width  = _mwidth(obj);
            box.Height = _mheight(obj);

            app->mui_viewport_box = box;
            app->mui_rp           = rp;

            /* Check if viewport size changed and notify server */
            uint16_t w = (uint16_t)(box.Width & ~1);
            uint16_t h = (uint16_t)(box.Height & ~1);
            if (w > 1280) w = 1280;
            if (h > 900)  h = 900;
            if (w >= 320 && h >= 200 && app->connected &&
                (w != app->renderer.reported_w || h != app->renderer.reported_h)) {
                app->renderer.reported_w = w;
                app->renderer.reported_h = h;
                char cmd[64];
                snprintf(cmd, sizeof(cmd), "@RESIZE:%ux%u", (unsigned)w, (unsigned)h);
                net_send_url(app, cmd);
            }

            renderer_present(app, rp, &box);
            app->renderer.dirty = FALSE;
        }
    }
    return 0;
}

static uint32 Viewport_Setup(struct IClass *cl, Object *obj, Msg msg)
{
    struct ViewportData *data = INST_DATA(cl, obj);
    if (!DoSuperMethodA(cl, obj, msg)) {
        return FALSE;
    }

    data->app = &g_app;
    data->eh.ehn_Class    = cl;
    data->eh.ehn_Object   = obj;
    data->eh.ehn_Events   = IDCMP_MOUSEBUTTONS | IDCMP_RAWKEY | IDCMP_VANILLAKEY | IDCMP_EXTENDEDMOUSE;
    data->eh.ehn_Flags    = MUI_EHF_GUIMODE;
    data->eh.ehn_Priority = -1;

    if (_win(obj)) {
        DoMethod(_win(obj), MUIM_Window_AddEventHandler, &data->eh);
    }
    return TRUE;
}

static uint32 Viewport_Cleanup(struct IClass *cl, Object *obj, Msg msg)
{
    struct ViewportData *data = INST_DATA(cl, obj);
    if (_win(obj)) {
        DoMethod(_win(obj), MUIM_Window_RemEventHandler, &data->eh);
    }
    return DoSuperMethodA(cl, obj, msg);
}

static uint32 Viewport_HandleEvent(struct IClass *cl, Object *obj, struct MUIP_HandleEvent *msg)
{
    struct ViewportData *data = INST_DATA(cl, obj);
    VortexApp *app = data->app ? data->app : &g_app;
    if (!app || !app->connected || !msg->imsg) {
        return 0;
    }

    struct IntuiMessage *imsg = msg->imsg;

    #define _between(a,x,b) ((x)>=(a) && (x)<=(b))
    #define _isinobject(x,y) (_between(_mleft(obj),(x),_mright(obj)) && _between(_mtop(obj),(y),_mbottom(obj)))

    switch (imsg->Class) {
        case IDCMP_MOUSEBUTTONS: {
            if (_isinobject(imsg->MouseX, imsg->MouseY)) {
                int32_t rel_x = (int32_t)(imsg->MouseX - _mleft(obj));
                int32_t rel_y = (int32_t)(imsg->MouseY - _mtop(obj));
                int32_t obj_w = (int32_t)_mwidth(obj);
                int32_t obj_h = (int32_t)_mheight(obj);

                if (obj_w > 0 && obj_h > 0) {
                    uint16_t sw = app->renderer.src_width  ? app->renderer.src_width  : VORTEXBROWSE_DEFAULT_WIDTH;
                    uint16_t sh = app->renderer.src_height ? app->renderer.src_height : VORTEXBROWSE_DEFAULT_HEIGHT;

                    int32_t rx = (rel_x * (int32_t)sw) / obj_w;
                    int32_t ry = (rel_y * (int32_t)sh) / obj_h;
                    if (rx < 0) { rx = 0; }
                    if (rx >= sw) { rx = sw - 1; }
                    if (ry < 0) { ry = 0; }
                    if (ry >= sh) { ry = sh - 1; }

                    if (imsg->Code == SELECTDOWN) {
                        net_send_click(app, rx, ry, 1);
                        return MUI_EventHandlerRC_Eat;
                    } else if (imsg->Code == MIDDLEDOWN) {
                        net_send_click(app, rx, ry, 2);
                        return MUI_EventHandlerRC_Eat;
                    } else if (imsg->Code == MENUDOWN) {
                        memset(&app->ctx_menu, 0, sizeof(MsgContextMenu));
                        app->ctx_menu_ready   = FALSE;
                        app->ctx_menu_pending = TRUE;
                        app_set_status("Kontextmenue wird geoeffnet (%d, %d)...", (int)rx, (int)ry);
                        net_send_click(app, rx, ry, 3);
                        return MUI_EventHandlerRC_Eat;
                    }
                }
            }
            break;
        }

        case IDCMP_EXTENDEDMOUSE: {
            if (imsg->Code & IMSGCODE_INTUIWHEELDATA) {
                if (_isinobject(imsg->MouseX, imsg->MouseY)) {
                    struct IntuiWheelData *wd = (struct IntuiWheelData *)imsg->IAddress;
                    if (wd && wd->WheelY != 0) {
                        int16_t dy = (wd->WheelY > 0) ? 120 : -120;
                        int32_t rel_x = (int32_t)(imsg->MouseX - _mleft(obj));
                        int32_t rel_y = (int32_t)(imsg->MouseY - _mtop(obj));
                        int32_t obj_w = (int32_t)_mwidth(obj);
                        int32_t obj_h = (int32_t)_mheight(obj);
                        uint16_t sw = app->renderer.src_width ? app->renderer.src_width : VORTEXBROWSE_DEFAULT_WIDTH;
                        uint16_t sh = app->renderer.src_height ? app->renderer.src_height : VORTEXBROWSE_DEFAULT_HEIGHT;
                        int32_t rx = obj_w > 0 ? (rel_x * (int32_t)sw) / obj_w : sw / 2;
                        int32_t ry = obj_h > 0 ? (rel_y * (int32_t)sh) / obj_h : sh / 2;
                        net_send_wheel(app, rx, ry, dy);
                        return MUI_EventHandlerRC_Eat;
                    }
                }
            }
            break;
        }

        case IDCMP_VANILLAKEY: {
            if (_isinobject(imsg->MouseX, imsg->MouseY)) {
                uint16 ascii = imsg->Code;
                if (ascii == 0x08) {
                    net_send_key(app, INPUT_KEY_DOWN, ascii, "Backspace");
                    return MUI_EventHandlerRC_Eat;
                } else if (ascii == 0x09) {
                    net_send_key(app, INPUT_KEY_DOWN, ascii, "Tab");
                    return MUI_EventHandlerRC_Eat;
                } else if (ascii == 0x0D || ascii == 0x0A) {
                    net_send_key(app, INPUT_KEY_DOWN, ascii, "Enter");
                    return MUI_EventHandlerRC_Eat;
                } else if (ascii == 0x1B) {
                    net_send_key(app, INPUT_KEY_DOWN, ascii, "Escape");
                    return MUI_EventHandlerRC_Eat;
                } else if (ascii == 0x7F) {
                    net_send_key(app, INPUT_KEY_DOWN, ascii, "Delete");
                    return MUI_EventHandlerRC_Eat;
                } else if (ascii >= 0x20 && ascii <= 0x7E) {
                    char key_str[4];
                    key_str[0] = (char)ascii;
                    key_str[1] = '\0';
                    net_send_key(app, INPUT_KEY_DOWN, ascii, key_str);
                    return MUI_EventHandlerRC_Eat;
                }
            }
            break;
        }

        case IDCMP_RAWKEY: {
            if (_isinobject(imsg->MouseX, imsg->MouseY)) {
                uint16 rawkey = imsg->Code;
                if ((rawkey & IECODE_UP_PREFIX) == 0) {
                    const char *kname = NULL;
                    switch (rawkey) {
                        case 0x48: kname = "PageUp";     break;
                        case 0x49: kname = "PageDown";   break;
                        case 0x4C: kname = "ArrowUp";    break;
                        case 0x4D: kname = "ArrowDown";  break;
                        case 0x4E: kname = "ArrowRight"; break;
                        case 0x4F: kname = "ArrowLeft";  break;
                        case 0x50: kname = "F1";         break;
                        case 0x51: kname = "F2";         break;
                        case 0x52: kname = "F3";         break;
                        case 0x53: kname = "F4";         break;
                        case 0x54: kname = "F5";         break;
                        case 0x55: kname = "F6";         break;
                        case 0x56: kname = "F7";         break;
                        case 0x57: kname = "F8";         break;
                        case 0x58: kname = "F9";         break;
                        case 0x59: kname = "F10";        break;
                        case 0x70: kname = "Home";       break;
                        case 0x71: kname = "End";        break;
                        default:   break;
                    }
                    if (kname) {
                        net_send_key(app, INPUT_KEY_DOWN, rawkey, kname);
                        return MUI_EventHandlerRC_Eat;
                    }
                }
            }
            break;
        }

        default:
            break;
    }

    return 0;
}

static uint32 Viewport_Dispatcher(struct IClass *cl, Object *obj, Msg msg)
{
    switch (msg->MethodID) {
        case MUIM_AskMinMax:   return Viewport_AskMinMax(cl, obj, (struct MUIP_AskMinMax *)msg);
        case MUIM_Draw:        return Viewport_Draw(cl, obj, (struct MUIP_Draw *)msg);
        case MUIM_Setup:       return Viewport_Setup(cl, obj, msg);
        case MUIM_Cleanup:     return Viewport_Cleanup(cl, obj, msg);
        case MUIM_HandleEvent: return Viewport_HandleEvent(cl, obj, (struct MUIP_HandleEvent *)msg);
        default:               return DoSuperMethodA(cl, obj, msg);
    }
}

static struct MUI_CustomClass *s_viewport_mcc = NULL;

static void parse_host_port(const char *input, char *out_host, size_t host_cap, uint16_t *out_port)
{
    if (!input || input[0] == '\0') {
        return;
    }
    const char *colon = strchr(input, ':');
    if (colon) {
        size_t hlen = (size_t)(colon - input);
        if (hlen >= host_cap) hlen = host_cap - 1;
        memcpy(out_host, input, hlen);
        out_host[hlen] = '\0';
        int p = atoi(colon + 1);
        if (p > 0 && p <= 65535) {
            *out_port = (uint16_t)p;
        }
    } else {
        strncpy(out_host, input, host_cap - 1);
        out_host[host_cap - 1] = '\0';
    }
}

static void prompt_server_address_mui(VortexApp *app)
{
    APTR str_obj, btn_ok, btn_cancel, wnd;
    char current_str[160];
    snprintf(current_str, sizeof(current_str), "%s:%u", app->server_host, (unsigned)app->server_port);

    wnd = WindowObject,
        MUIA_Window_Title, "Serveradresse eingeben",
        MUIA_Window_ID, MAKE_ID('S','R','V','R'),
        WindowContents, VGroup,
            Child, Label("VortexBrowse Server (IP:Port):"),
            Child, str_obj = StringObject,
                MUIA_String_Contents, current_str,
                MUIA_String_MaxLen, 128,
            End,
            Child, HGroup,
                Child, btn_ok = SimpleButton("Verbinden"),
                Child, btn_cancel = SimpleButton("Abbrechen"),
            End,
        End,
    End;

    if (!wnd) return;

    DoMethod(app->mui_app, OM_ADDMEMBER, wnd);
    set(wnd, MUIA_Window_Open, TRUE);

    DoMethod(wnd, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, app->mui_app, 2, MUIM_Application_ReturnID, 1002);
    DoMethod(btn_cancel, MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, 1002);
    DoMethod(btn_ok, MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, 1001);
    DoMethod(str_obj, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, app->mui_app, 2, MUIM_Application_ReturnID, 1001);

    ULONG sigs = 0;
    BOOL loop = TRUE;
    BOOL apply = FALSE;

    while (loop) {
        ULONG ret = DoMethod(app->mui_app, MUIM_Application_NewInput, &sigs);
        if (ret == 1001) {
            apply = TRUE;
            loop = FALSE;
        } else if (ret == 1002 || ret == MUIV_Application_ReturnID_Quit) {
            loop = FALSE;
        } else if (sigs) {
            sigs = IExec->Wait(sigs | SIGBREAKF_CTRL_C);
            if (sigs & SIGBREAKF_CTRL_C) break;
        }
    }

    if (apply) {
        char *text = NULL;
        get(str_obj, MUIA_String_Contents, &text);
        if (text && text[0] != '\0') {
            parse_host_port(text, app->server_host, sizeof(app->server_host), &app->server_port);
            net_connect(app, app->server_host, app->server_port);
        }
    }

    set(wnd, MUIA_Window_Open, FALSE);
    DoMethod(app->mui_app, OM_REMMEMBER, wnd);
    MUI_DisposeObject(wnd);
}

void gui_mui_rebuild_fav_cycle(VortexApp *app)
{
    if (!app) return;

    if (app->mui_fav_entries) {
        for (int i = 0; i < app->mui_fav_entries_count; i++) {
            if (app->mui_fav_entries[i]) free(app->mui_fav_entries[i]);
        }
        free(app->mui_fav_entries);
        app->mui_fav_entries = NULL;
        app->mui_fav_entries_count = 0;
    }

    int count = app->num_favorites;
    if (count <= 0) {
        app->mui_fav_entries = calloc(2, sizeof(char *));
        app->mui_fav_entries[0] = strdup("Keine Lesezeichen");
        app->mui_fav_entries[1] = NULL;
        app->mui_fav_entries_count = 1;
    } else {
        app->mui_fav_entries = calloc(count + 1, sizeof(char *));
        for (int i = 0; i < count; i++) {
            app->mui_fav_entries[i] = strdup(app->favorites[i].title);
        }
        app->mui_fav_entries[count] = NULL;
        app->mui_fav_entries_count = count;
    }

    if (app->mui_fav_cycle) {
        set(app->mui_fav_cycle, MUIA_Cycle_Entries, app->mui_fav_entries);
    }
}

BOOL gui_mui_init(VortexApp *app)
{
    if (!IMUIMaster) {
        printf("[GUI-MUI] ERROR: IMUIMaster interface is NULL!\n");
        return FALSE;
    }

    s_viewport_mcc = MUI_CreateCustomClass(
        NULL, MUIC_Area, NULL, sizeof(struct ViewportData), (APTR)Viewport_Dispatcher);

    if (!s_viewport_mcc) {
        printf("[GUI-MUI] ERROR: Failed to create custom MUI viewport class!\n");
        return FALSE;
    }

    gui_mui_rebuild_fav_cycle(app);

    APTR btn_back, btn_fwd, btn_reload, btn_stop, btn_home;
    APTR btn_clear_url, btn_go, btn_search_go;
    APTR btn_bm_an, btn_bm_os4, btn_bm_ans, btn_bm_net, btn_bm_yt, btn_bm_wiki;
    APTR btn_add_fav, btn_connect, btn_menu;
    APTR row1, row2, status_row;

    snprintf(app->win_title, sizeof(app->win_title), "Odyssey (MUI) / %s", VB_APP_TITLE);

    APTR menustrip = MUI_MakeObject(MUIO_MenustripNM, s_mui_menu, 0);

    /* ------------------------------------------------------------------------
     * Row 1: Odyssey Navigation, URL String & Web Search Bar
     * ------------------------------------------------------------------------ */
    row1 = HGroup,
        Child, btn_back      = SimpleButton("<"),
        Child, btn_fwd       = SimpleButton(">"),
        Child, btn_reload    = SimpleButton("Neu"),
        Child, btn_stop      = SimpleButton("Stopp"),
        Child, btn_home      = SimpleButton("Start"),
        Child, app->mui_url  = StringObject,
            MUIA_String_Contents, app->current_url[0] ? app->current_url : "",
            MUIA_String_MaxLen, VB_MAX_URL_LEN - 1,
            MUIA_CycleChain, 1,
        End,
        Child, btn_clear_url = SimpleButton("X"),
        Child, btn_go        = SimpleButton("Los"),
        Child, app->mui_search = StringObject,
            MUIA_String_Contents, "",
            MUIA_String_MaxLen, 128,
            MUIA_CycleChain, 1,
        End,
        Child, btn_search_go = SimpleButton("Suche"),
    End;

    /* ------------------------------------------------------------------------
     * Row 2: Odyssey Bookmarks, Lesezeichen Cycle & Server Chooser
     * ------------------------------------------------------------------------ */
    row2 = HGroup,
        Child, btn_bm_an           = SimpleButton("Amiga-News"),
        Child, btn_bm_os4          = SimpleButton("OS4Depot"),
        Child, btn_bm_ans          = SimpleButton("Amigans"),
        Child, btn_bm_net          = SimpleButton("Aminet"),
        Child, btn_bm_yt           = SimpleButton("YouTube"),
        Child, btn_bm_wiki         = SimpleButton("Wikipedia"),
        Child, btn_add_fav         = SimpleButton("+Lesez."),
        Child, app->mui_fav_cycle  = CycleObject,
            MUIA_Cycle_Entries, app->mui_fav_entries,
            MUIA_CycleChain, 1,
        End,
        Child, btn_menu            = SimpleButton("Menue"),
        Child, app->mui_server_cycle = CycleObject,
            MUIA_Cycle_Entries, s_server_entries,
            MUIA_Cycle_Active, 0,
            MUIA_CycleChain, 1,
        End,
        Child, btn_connect         = SimpleButton("Verbinden"),
    End;

    /* ------------------------------------------------------------------------
     * Row 3: Custom Viewport Object
     * ------------------------------------------------------------------------ */
    app->mui_viewport = IIntuition->NewObject(s_viewport_mcc->mcc_Class, NULL,
        MUIA_InputMode, MUIV_InputMode_RelVerify,
        TAG_DONE);

    if (!app->mui_viewport) {
        printf("[GUI-MUI] ERROR: Failed to instantiate custom viewport object!\n");
        return FALSE;
    }

    /* ------------------------------------------------------------------------
     * Row 4: Status Bar & Progress Gauge
     * ------------------------------------------------------------------------ */
    status_row = HGroup,
        Child, app->mui_status = TextObject,
            MUIA_Text_Contents, app->status_text,
            MUIA_Frame, MUIV_Frame_ReadList,
        End,
        Child, app->mui_gauge = GaugeObject,
            MUIA_Gauge_Horiz, TRUE,
            MUIA_Gauge_Max, 100,
            MUIA_Gauge_Current, 0,
            MUIA_FixWidth, 180,
        End,
    End;

    /* ------------------------------------------------------------------------
     * Root Window & Application
     * ------------------------------------------------------------------------ */
    app->mui_app = ApplicationObject,
        MUIA_Application_Title, "VortexBrowse",
        MUIA_Application_Version, "$VER: VortexBrowse-MUI 1.0 (AmigaOS 4.1 PowerPC)",
        MUIA_Application_Copyright, "(c) 2026 TearsOfMe",
        MUIA_Application_Author, "TearsOfMe / Antigravity",
        MUIA_Application_Description, "Modern Remote Web Browser for AmigaOS 4 (MUI Edition)",
        MUIA_Application_Base, "VORTEXBROWSE",
        MUIA_Application_Menustrip, menustrip,

        SubWindow, app->mui_win = WindowObject,
            MUIA_Window_Title, app->win_title,
            MUIA_Window_ID, MAKE_ID('V','B','M','U'),
            WindowContents, VGroup,
                Child, row1,
                Child, row2,
                Child, app->mui_viewport,
                Child, status_row,
            End,
        End,
    End;

    if (!app->mui_app) {
        printf("[GUI-MUI] ERROR: Failed to create ApplicationObject!\n");
        return FALSE;
    }

    /* Set up Notifications for Toolbar Buttons */
    DoMethod(btn_back,      MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_BACK);
    DoMethod(btn_fwd,       MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_FORWARD);
    DoMethod(btn_reload,    MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_RELOAD);
    DoMethod(btn_stop,      MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_STOP);
    DoMethod(btn_home,      MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_HOME);
    DoMethod(btn_clear_url, MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_CLEAR_URL);
    DoMethod(btn_go,        MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_GO);
    DoMethod(btn_search_go, MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_SEARCH_GO);

    /* Bookmarks Toolbar */
    DoMethod(btn_bm_an,   MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_BM_AMIGANEWS);
    DoMethod(btn_bm_os4,  MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_BM_OS4DEPOT);
    DoMethod(btn_bm_ans,  MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_BM_AMIGANS);
    DoMethod(btn_bm_net,  MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_BM_AMINET);
    DoMethod(btn_bm_yt,   MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_BM_YOUTUBE);
    DoMethod(btn_bm_wiki, MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_BM_WIKI);

    DoMethod(btn_add_fav, MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_ADD_FAV);
    DoMethod(btn_menu,    MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_MENU);
    DoMethod(btn_connect, MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_CONNECT);

    /* URL & Search Acknowledge (Enter Key) */
    DoMethod(app->mui_url,    MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, app->mui_app, 2, MUIM_Application_ReturnID, GID_GO);
    DoMethod(app->mui_search, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, app->mui_app, 2, MUIM_Application_ReturnID, GID_SEARCH_GO);

    /* Cycle Gadgets */
    DoMethod(app->mui_fav_cycle,    MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, app->mui_app, 2, MUIM_Application_ReturnID, GID_FAV_CHOOSER);
    DoMethod(app->mui_server_cycle, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, app->mui_app, 2, MUIM_Application_ReturnID, GID_SERVER_CHOOSER);

    /* Menu Actions */
    DoMethod(app->mui_app, MUIM_Notify, MUIA_Application_MenuAction, MUIV_EveryTime, app->mui_app, 2, MUIM_Application_ReturnID, MUIV_TriggerValue);

    /* Window Close Request */
    DoMethod(app->mui_win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, app->mui_app, 2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);

    /* Open the window */
    set(app->mui_win, MUIA_Window_Open, TRUE);

    /* Obtain Intuition window pointer */
    get(app->mui_win, MUIA_Window_Window, &app->win);

    printf("[GUI-MUI] VortexBrowse-MUI window opened successfully.\n");
    return TRUE;
}

void gui_mui_cleanup(VortexApp *app)
{
    if (app->mui_app) {
        set(app->mui_win, MUIA_Window_Open, FALSE);
        MUI_DisposeObject(app->mui_app);
        app->mui_app = NULL;
        app->mui_win = NULL;
        app->win     = NULL;
    }

    if (s_viewport_mcc) {
        MUI_DeleteCustomClass(s_viewport_mcc);
        s_viewport_mcc = NULL;
    }

    if (app->mui_fav_entries) {
        for (int i = 0; i < app->mui_fav_entries_count; i++) {
            if (app->mui_fav_entries[i]) free(app->mui_fav_entries[i]);
        }
        free(app->mui_fav_entries);
        app->mui_fav_entries = NULL;
        app->mui_fav_entries_count = 0;
    }
}

void gui_mui_flush_dirty(VortexApp *app)
{
    if (app && app->mui_viewport) {
        MUI_Redraw((Object *)app->mui_viewport, MADF_DRAWUPDATE);
    }
}

void gui_mui_update_url_bar(VortexApp *app, const char *url)
{
    if (!app || !app->mui_url) {
        return;
    }
    const char *disp = url ? url : "";
    if (strncmp(disp, "http://127.0.0.1:", 17) == 0) {
        disp = "";
    }
    set(app->mui_url, MUIA_String_Contents, (STRPTR)disp);

    if (disp[0] != '\0') {
        snprintf(app->win_title, sizeof(app->win_title), "Odyssey (MUI) / VortexBrowse - %s", disp);
    } else {
        snprintf(app->win_title, sizeof(app->win_title), "Odyssey (MUI) / %s", VB_APP_TITLE);
    }
    if (app->mui_win) {
        set(app->mui_win, MUIA_Window_Title, (STRPTR)app->win_title);
    }
}

void gui_update_url_bar(VortexApp *app, const char *url)
{
    gui_mui_update_url_bar(app, url);
}

void app_set_status(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_app.status_text, sizeof(g_app.status_text), fmt, args);
    va_end(args);

    if (g_app.mui_status) {
        set(g_app.mui_status, MUIA_Text_Contents, (STRPTR)g_app.status_text);
    }
}

void app_set_gauge(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;

    if (g_app.mui_gauge) {
        set(g_app.mui_gauge, MUIA_Gauge_Current, percent);
    }
}

void gui_mui_handle_action(VortexApp *app, uint32 action_id)
{
    switch (action_id) {
        case GID_BACK:
        case MID_BACK:
            net_send_url(app, "@BACK");
            break;

        case GID_FORWARD:
        case MID_FORWARD:
            net_send_url(app, "@FORWARD");
            break;

        case GID_RELOAD:
        case MID_RELOAD:
            net_send_url(app, "@RELOAD");
            break;

        case GID_STOP:
        case MID_STOP:
            net_send_url(app, "@STOP");
            app_set_status("Ladevorgang angehalten.");
            break;

        case GID_HOME:
        case MID_HOME:
            gui_mui_update_url_bar(app, "");
            net_send_url(app, "@HOME");
            break;

        case GID_CLEAR_URL:
            if (app->mui_url) {
                set(app->mui_url, MUIA_String_Contents, "");
            }
            break;

        case GID_GO: {
            char *url_buf = NULL;
            get(app->mui_url, MUIA_String_Contents, &url_buf);
            if (url_buf && url_buf[0] != '\0') {
                net_send_url(app, url_buf);
            }
            break;
        }

        case GID_SEARCH:
        case GID_SEARCH_GO: {
            char *s_buf = NULL;
            get(app->mui_search, MUIA_String_Contents, &s_buf);
            if (s_buf && s_buf[0] != '\0') {
                char search_cmd[320];
                snprintf(search_cmd, sizeof(search_cmd), "/search?q=%s", s_buf);
                net_send_url(app, search_cmd);
            }
            break;
        }

        case GID_BM_AMIGANEWS:
        case MID_BM_AMIGANEWS:
            net_send_url(app, "https://www.amiga-news.de");
            break;

        case GID_BM_OS4DEPOT:
        case MID_BM_OS4DEPOT:
            net_send_url(app, "https://os4depot.net");
            break;

        case GID_BM_AMIGANS:
        case MID_BM_AMIGANS:
            net_send_url(app, "https://www.amigans.net");
            break;

        case GID_BM_AMINET:
        case MID_BM_AMINET:
            net_send_url(app, "https://aminet.net");
            break;

        case GID_BM_YOUTUBE:
        case MID_BM_YOUTUBE:
            net_send_url(app, "https://www.youtube.com");
            break;

        case GID_BM_WIKI:
        case MID_BM_WIKI:
            net_send_url(app, "https://de.wikipedia.org/wiki/AmigaOS");
            break;

        case GID_ADD_FAV:
        case MID_ADD_FAV: {
            char *url_buf = NULL;
            get(app->mui_url, MUIA_String_Contents, &url_buf);
            const char *u = (url_buf && url_buf[0] != '\0') ? url_buf : app->current_url;
            if (u && u[0] != '\0') {
                if (favorites_add(app, u, u)) {
                    favorites_save(app);
                    gui_mui_rebuild_fav_cycle(app);
                    app_set_status("Zu Lesezeichen hinzugefuegt (ENVARC:): %s", u);
                }
            }
            break;
        }

        case GID_FAV_CHOOSER: {
            ULONG idx = 0;
            get(app->mui_fav_cycle, MUIA_Cycle_Active, &idx);
            if ((int)idx >= 0 && (int)idx < app->num_favorites) {
                const char *fav_url = app->favorites[idx].url;
                if (fav_url[0] != '@') {
                    gui_mui_update_url_bar(app, fav_url);
                } else {
                    gui_mui_update_url_bar(app, "");
                }
                net_send_url(app, fav_url);
            }
            break;
        }

        case GID_SERVER_CHOOSER: {
            ULONG sel = 0;
            get(app->mui_server_cycle, MUIA_Cycle_Active, &sel);
            if (sel == 0) {
                strncpy(app->server_host, VORTEXBROWSE_RPI_HOST, sizeof(app->server_host) - 1);
                app->server_port = VORTEXBROWSE_RPI_PORT;
                app_set_status("Server gewaehlt: Raspberry Pi 4 (%s:%u)", app->server_host, app->server_port);
                net_connect(app, app->server_host, app->server_port);
            } else if (sel == 1) {
                strncpy(app->server_host, VORTEXBROWSE_HOSTPC_HOST, sizeof(app->server_host) - 1);
                app->server_port = VORTEXBROWSE_HOSTPC_PORT;
                app_set_status("Server gewaehlt: Host-PC (%s:%u)", app->server_host, app->server_port);
                net_connect(app, app->server_host, app->server_port);
            } else if (sel == 2) {
                prompt_server_address_mui(app);
            }
            break;
        }

        case GID_CONNECT:
            net_connect(app, app->server_host, app->server_port);
            break;

        case GID_MENU: {
            memset(&app->ctx_menu, 0, sizeof(MsgContextMenu));
            app->ctx_menu_ready   = FALSE;
            app->ctx_menu_pending = TRUE;
            app_set_status("Kontextmenue wird geoeffnet...");
            net_send_click(app, 640, 360, 3);
            break;
        }

        case MID_SERVER_RPI:
            set(app->mui_server_cycle, MUIA_Cycle_Active, 0);
            strncpy(app->server_host, VORTEXBROWSE_RPI_HOST, sizeof(app->server_host) - 1);
            app->server_port = VORTEXBROWSE_RPI_PORT;
            app_set_status("Server: Raspberry Pi 4 (%s:%u)", app->server_host, app->server_port);
            net_connect(app, app->server_host, app->server_port);
            break;

        case MID_SERVER_PC:
            set(app->mui_server_cycle, MUIA_Cycle_Active, 1);
            strncpy(app->server_host, VORTEXBROWSE_HOSTPC_HOST, sizeof(app->server_host) - 1);
            app->server_port = VORTEXBROWSE_HOSTPC_PORT;
            app_set_status("Server: Host-PC (%s:%u)", app->server_host, app->server_port);
            net_connect(app, app->server_host, app->server_port);
            break;

        case MID_SERVER_CUSTOM:
            prompt_server_address_mui(app);
            break;

        case MID_DISCONNECT:
            net_disconnect(app);
            app_set_status("Verbindung getrennt.");
            break;

        case MID_CLEAR_CACHE:
            net_send_url(app, "@CLEARCACHE");
            app_set_status("Cache-Bereinigung angefordert.");
            break;

        case MID_ABOUT:
            MUI_Request(app->mui_app, app->mui_win, 0,
                        "Ueber VortexBrowse (MUI)",
                        "OK",
                        "VortexBrowse 1.0 (MUI Edition)\n"
                        "AmigaOS 4.1 PowerPC\n\n"
                        "Moderner Remote-Browser mit Odyssey-Bedienkonzept,\n"
                        "Compositing Video-Engine & AHI Low-Latency Audio.\n\n"
                        "(c) 2026 TearsOfMe");
            break;

        case MID_QUIT:
        case MUIV_Application_ReturnID_Quit:
            app->running = FALSE;
            break;

        default:
            break;
    }
}

static uint32 ctx_menu_hook_func(struct Hook *h, Object *item, APTR reserved)
{
    (void)reserved;
    uint32 *out_id = (uint32 *)h->h_Data;
    uint32 id_val  = 0;
    if (out_id && item && IIntuition->GetAttr(PMIA_ID, item, &id_val)) {
        *out_id = id_val;
    }
    return 0;
}

void gui_mui_handle_events(VortexApp *app)
{
    if (app->ctx_menu_pending) {
        app->ctx_menu_pending = FALSE;

        /* Wait up to 1.2s for server MSG_CONTEXT_MENU target inspection response */
        for (int w = 0; w < 60 && !app->ctx_menu_ready && app->connected && app->sock_fd >= 0; w++) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(app->sock_fd, &rfds);
            struct timeval tv;
            tv.tv_sec  = 0;
            tv.tv_usec = 20000;
            LONG r = ISocket ? ISocket->WaitSelect(app->sock_fd + 1, &rfds, NULL, NULL, &tv, NULL) : 0;
            if (r > 0 && app->sock_fd >= 0 && FD_ISSET(app->sock_fd, &rfds)) {
                do {
                    if (!net_handle_incoming(app)) {
                        break;
                    }
                } while (app->connected && !app->ctx_menu_ready && net_has_pending_data(app));
            }
        }
        gui_mui_flush_dirty(app);

        struct Window *target_win = app->win;
        if (!target_win && app->mui_win) {
            get(app->mui_win, MUIA_Window_Window, &target_win);
        }

        if (target_win) {
            enum {
                CTX_NONE = 0,
                CTX_OPEN_LINK = 1,
                CTX_OPEN_NEW_WIN,
                CTX_DOWNLOAD_LINK,
                CTX_COPY_LINK_URL,
                CTX_FAV_LINK,
                CTX_OPEN_MEDIA,
                CTX_DOWNLOAD_MEDIA,
                CTX_BACK,
                CTX_FORWARD,
                CTX_RELOAD,
                CTX_HOME,
                CTX_FAV_PAGE,
                CTX_DOWNLOAD_PAGE
            };

            BOOL has_link  = (app->ctx_menu.link_url[0] != '\0');
            BOOL has_media = (app->ctx_menu.media_url[0] != '\0' &&
                              strcmp(app->ctx_menu.media_url, app->ctx_menu.link_url) != 0);

            char hdr_buf[96];
            if (has_link) {
                const char *lbl = app->ctx_menu.link_text[0] ? app->ctx_menu.link_text : app->ctx_menu.link_url;
                snprintf(hdr_buf, sizeof(hdr_buf), "Link: %.50s", lbl);
            } else if (has_media) {
                snprintf(hdr_buf, sizeof(hdr_buf), "Bild / Medium");
            } else {
                snprintf(hdr_buf, sizeof(hdr_buf), "Odyssey / VortexBrowse");
            }

            uint32 selected_id = CTX_NONE;

            if (IPopupMenu) {
                struct Hook pm_hook;
                memset(&pm_hook, 0, sizeof(struct Hook));
                pm_hook.h_Entry = (HOOKFUNC)ctx_menu_hook_func;
                pm_hook.h_Data  = &selected_id;

                Object *menu_obj = NULL;
                if (has_link) {
                    menu_obj = PMMENU(hdr_buf),
                        PMA_MenuHandler, &pm_hook,
                        PMITEM("Link oeffnen"), PMIA_ID, CTX_OPEN_LINK, PMEND,
                        PMITEM("Link in neuem Tab oeffnen"), PMIA_ID, CTX_OPEN_NEW_WIN, PMEND,
                        PMITEM("Verknuepftes Ziel speichern unter (ASL)..."), PMIA_ID, CTX_DOWNLOAD_LINK, PMEND,
                        PMITEM("Link-Adresse in URL-Leiste kopieren"), PMIA_ID, CTX_COPY_LINK_URL, PMEND,
                        PMITEM("Link zu Lesezeichen hinzufuegen"), PMIA_ID, CTX_FAV_LINK, PMEND,
                        PMBAR(), PMEND,
                        PMITEM("Zurueck"), PMIA_ID, CTX_BACK, PMEND,
                        PMITEM("Vorwaerts"), PMIA_ID, CTX_FORWARD, PMEND,
                        PMITEM("Seite neu laden"), PMIA_ID, CTX_RELOAD, PMEND,
                        PMITEM("Startseite (Speed-Dial)"), PMIA_ID, CTX_HOME, PMEND,
                    PMEND;
                } else if (has_media) {
                    menu_obj = PMMENU(hdr_buf),
                        PMA_MenuHandler, &pm_hook,
                        PMITEM("Bild / Medium anzeigen"), PMIA_ID, CTX_OPEN_MEDIA, PMEND,
                        PMITEM("Bild / Medium speichern unter (ASL)..."), PMIA_ID, CTX_DOWNLOAD_MEDIA, PMEND,
                        PMBAR(), PMEND,
                        PMITEM("Zurueck"), PMIA_ID, CTX_BACK, PMEND,
                        PMITEM("Vorwaerts"), PMIA_ID, CTX_FORWARD, PMEND,
                        PMITEM("Seite neu laden"), PMIA_ID, CTX_RELOAD, PMEND,
                        PMITEM("Startseite (Speed-Dial)"), PMIA_ID, CTX_HOME, PMEND,
                    PMEND;
                } else {
                    menu_obj = PMMENU(hdr_buf),
                        PMA_MenuHandler, &pm_hook,
                        PMITEM("Zurueck"), PMIA_ID, CTX_BACK, PMEND,
                        PMITEM("Vorwaerts"), PMIA_ID, CTX_FORWARD, PMEND,
                        PMITEM("Seite neu laden"), PMIA_ID, CTX_RELOAD, PMEND,
                        PMITEM("Startseite (Speed-Dial)"), PMIA_ID, CTX_HOME, PMEND,
                        PMBAR(), PMEND,
                        PMITEM("Aktuelle Seite zu Lesezeichen (+Fav)"), PMIA_ID, CTX_FAV_PAGE, PMEND,
                        PMITEM("Aktuelle Adresse herunterladen (ASL)..."), PMIA_ID, CTX_DOWNLOAD_PAGE, PMEND,
                    PMEND;
                }

                if (menu_obj) {
                    Object *sel_item = (Object *)IIntuition->IDoMethod(menu_obj, PM_OPEN, target_win);
                    if (selected_id == CTX_NONE && sel_item != NULL) {
                        uint32 id_val = 0;
                        if (IIntuition->GetAttr(PMIA_ID, sel_item, &id_val) && id_val != 0) {
                            selected_id = id_val;
                        }
                    }
                    IIntuition->DisposeObject(menu_obj);
                }
            } else {
                struct EasyStruct es;
                memset(&es, 0, sizeof(es));
                es.es_StructSize   = sizeof(struct EasyStruct);
                es.es_Title        = (STRPTR)"Odyssey / VortexBrowse Kontextmenue";
                if (has_link) {
                    es.es_TextFormat   = (STRPTR)"Link: %s";
                    es.es_GadgetFormat = (STRPTR)"Link oeffnen|Neuer Tab|Herunterladen (ASL)|In URL-Leiste|Zurueck|Abbrechen";
                    LONG choice = IIntuition->EasyRequest(target_win, &es, NULL, app->ctx_menu.link_url);
                    if (choice == 1) selected_id = CTX_OPEN_LINK;
                    else if (choice == 2) selected_id = CTX_OPEN_NEW_WIN;
                    else if (choice == 3) selected_id = CTX_DOWNLOAD_LINK;
                    else if (choice == 4) selected_id = CTX_COPY_LINK_URL;
                    else if (choice == 5) selected_id = CTX_BACK;
                } else {
                    es.es_TextFormat   = (STRPTR)"Seiten-Aktion waehlen:";
                    es.es_GadgetFormat = (STRPTR)"Zurueck|Vorwaerts|Neu laden|Startseite|Abbrechen";
                    LONG choice = IIntuition->EasyRequest(target_win, &es, NULL);
                    if (choice == 1) selected_id = CTX_BACK;
                    else if (choice == 2) selected_id = CTX_FORWARD;
                    else if (choice == 3) selected_id = CTX_RELOAD;
                    else if (choice == 4) selected_id = CTX_HOME;
                }
            }

            switch (selected_id) {
                case CTX_OPEN_LINK:
                    gui_mui_update_url_bar(app, app->ctx_menu.link_url);
                    net_send_url(app, app->ctx_menu.link_url);
                    break;

                case CTX_OPEN_NEW_WIN:
                    gui_mui_update_url_bar(app, app->ctx_menu.link_url);
                    net_send_url(app, app->ctx_menu.link_url);
                    app_set_status("Oeffne Link: %s", app->ctx_menu.link_url);
                    break;

                case CTX_DOWNLOAD_LINK: {
                    char dl_cmd[VB_MAX_URL_LEN + 16];
                    snprintf(dl_cmd, sizeof(dl_cmd), "@DOWNLOAD:%s", app->ctx_menu.link_url);
                    app_set_status("Starte ASL-Download: %s", app->ctx_menu.link_url);
                    net_send_url(app, dl_cmd);
                    break;
                }

                case CTX_COPY_LINK_URL:
                    gui_mui_update_url_bar(app, app->ctx_menu.link_url);
                    app_set_status("Link in URL-Leiste kopiert: %s", app->ctx_menu.link_url);
                    break;

                case CTX_FAV_LINK: {
                    const char *t = app->ctx_menu.link_text[0] ? app->ctx_menu.link_text : app->ctx_menu.link_url;
                    if (favorites_add(app, t, app->ctx_menu.link_url)) {
                        favorites_save(app);
                        gui_mui_rebuild_fav_cycle(app);
                        app_set_status("Link zu Lesezeichen hinzugefuegt: %s", t);
                    }
                    break;
                }

                case CTX_OPEN_MEDIA:
                    gui_mui_update_url_bar(app, app->ctx_menu.media_url);
                    net_send_url(app, app->ctx_menu.media_url);
                    break;

                case CTX_DOWNLOAD_MEDIA: {
                    char dl_cmd[VB_MAX_URL_LEN + 16];
                    snprintf(dl_cmd, sizeof(dl_cmd), "@DOWNLOAD:%s", app->ctx_menu.media_url);
                    app_set_status("Starte Medien-Download: %s", app->ctx_menu.media_url);
                    net_send_url(app, dl_cmd);
                    break;
                }

                case CTX_BACK:
                    net_send_url(app, "@BACK");
                    break;

                case CTX_FORWARD:
                    net_send_url(app, "@FORWARD");
                    break;

                case CTX_RELOAD:
                    net_send_url(app, "@RELOAD");
                    break;

                case CTX_HOME:
                    gui_mui_update_url_bar(app, "");
                    net_send_url(app, "@HOME");
                    break;

                case CTX_FAV_PAGE: {
                    const char *u = app->current_url[0] ? app->current_url : "https://www.amiga-news.de";
                    if (favorites_add(app, u, u)) {
                        favorites_save(app);
                        gui_mui_rebuild_fav_cycle(app);
                        app_set_status("Seite zu Lesezeichen hinzugefuegt: %s", u);
                    }
                    break;
                }

                case CTX_DOWNLOAD_PAGE: {
                    if (app->current_url[0] != '\0') {
                        char dl_cmd[VB_MAX_URL_LEN + 16];
                        snprintf(dl_cmd, sizeof(dl_cmd), "@DOWNLOAD:%s", app->current_url);
                        app_set_status("Starte Download: %s", app->current_url);
                        net_send_url(app, dl_cmd);
                    }
                    break;
                }

                default:
                    app_set_status("Bereit.");
                    break;
            }
        }
    }
}
