#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <proto/intuition.h>
#include <proto/exec.h>
#include <proto/chooser.h>
#include <gadgets/chooser.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/string.h>
#include <gadgets/checkbox.h>
#include <classes/window.h>

#include "app.h"
#include "passwords.h"
#include "gui_passwords.h"
#include "clipboard.h"
#include "net.h"

enum {
    GID_PW_CHOOSER = 100,
    GID_PW_DOMAIN,
    GID_PW_USER,
    GID_PW_PASS,
    GID_PW_SHOW_PASS,
    GID_PW_COPY_USER,
    GID_PW_COPY_PASS,
    GID_PW_AUTOLOGIN,
    GID_PW_ADD,
    GID_PW_DEL,
    GID_PW_CLOSE
};

static char s_pw_labels[VB_MAX_PASSWORDS][256];

static void free_pw_chooser_list(VortexApp *app)
{
    if (!IChooser || !app) return;
    struct Node *node = IExec->RemHead(&app->pw_chooser_list);
    while (node) {
        IChooser->FreeChooserNode(node);
        node = IExec->RemHead(&app->pw_chooser_list);
    }
}

static void rebuild_pw_chooser(VortexApp *app, int sel_idx)
{
    if (!IChooser || !app || !app->pw_gad_chooser) return;

    if (app->pw_win) {
        IIntuition->SetGadgetAttrs((struct Gadget *)app->pw_gad_chooser, app->pw_win, NULL,
                                   CHOOSER_Labels, ~0,
                                   TAG_DONE);
    }

    free_pw_chooser_list(app);
    IExec->NewList(&app->pw_chooser_list);

    if (!app->password_store || app->password_store->count == 0) {
        struct Node *n = IChooser->AllocChooserNode(
            CNA_Text, (Tag)"(Keine gespeicherten Passwoerter)",
            TAG_DONE);
        if (n) IExec->AddTail(&app->pw_chooser_list, n);
    } else {
        for (int i = 0; i < app->password_store->count; i++) {
            snprintf(s_pw_labels[i], sizeof(s_pw_labels[i]), "%s (%s)",
                     app->password_store->entries[i].domain,
                     app->password_store->entries[i].username);
            struct Node *n = IChooser->AllocChooserNode(
                CNA_Text, (Tag)s_pw_labels[i],
                TAG_DONE);
            if (n) IExec->AddTail(&app->pw_chooser_list, n);
        }
    }

    if (app->pw_win) {
        int count = app->password_store ? app->password_store->count : 0;
        if (sel_idx >= count) sel_idx = (count > 0) ? count - 1 : 0;
        if (sel_idx < 0) sel_idx = 0;
        app->pw_cur_sel = sel_idx;

        IIntuition->SetGadgetAttrs((struct Gadget *)app->pw_gad_chooser, app->pw_win, NULL,
                                   CHOOSER_Labels,   (Tag)&app->pw_chooser_list,
                                   CHOOSER_Selected, (uint32)sel_idx,
                                   TAG_DONE);
        IIntuition->RefreshGList((struct Gadget *)app->pw_gad_chooser, app->pw_win, NULL, 1);
    }
}

static void update_fields(VortexApp *app, int idx)
{
    if (!app || !app->pw_win) return;

    if (app->password_store && idx >= 0 && idx < app->password_store->count) {
        app->pw_cur_sel = idx;
        IIntuition->SetGadgetAttrs((struct Gadget *)app->pw_gad_domain, app->pw_win, NULL,
                                   STRINGA_TextVal, app->password_store->entries[idx].domain,
                                   TAG_DONE);
        IIntuition->SetGadgetAttrs((struct Gadget *)app->pw_gad_user, app->pw_win, NULL,
                                   STRINGA_TextVal, app->password_store->entries[idx].username,
                                   TAG_DONE);
        strncpy(app->pw_real_pass, app->password_store->entries[idx].password, sizeof(app->pw_real_pass) - 1);
        app->pw_real_pass[sizeof(app->pw_real_pass) - 1] = '\0';

        IIntuition->SetGadgetAttrs((struct Gadget *)app->pw_gad_pass, app->pw_win, NULL,
                                   STRINGA_HookType, app->pw_show_plaintext ? SHK_CUSTOM : SHK_PASSWORD,
                                   STRINGA_TextVal,  app->pw_real_pass,
                                   TAG_DONE);
    } else {
        app->pw_cur_sel = -1;
        app->pw_real_pass[0] = '\0';

        /* Auto-prefill Domain from current webpage URL */
        char clean_domain[128];
        clean_domain[0] = '\0';
        const char *u = app->current_url[0] ? app->current_url :
                        (app->num_tabs > 0 ? app->tabs[app->active_tab_idx].url : "");
        if (u && u[0] != '\0' && u[0] != '@') {
            const char *p = u;
            if (strncmp(p, "http://", 7) == 0) p += 7;
            else if (strncmp(p, "https://", 8) == 0) p += 8;
            if (strncmp(p, "www.", 4) == 0) p += 4;
            strncpy(clean_domain, p, sizeof(clean_domain) - 1);
            clean_domain[sizeof(clean_domain) - 1] = '\0';
            char *slash = strchr(clean_domain, '/');
            if (slash) *slash = '\0';
            char *colon = strchr(clean_domain, ':');
            if (colon) *colon = '\0';
        }

        IIntuition->SetGadgetAttrs((struct Gadget *)app->pw_gad_domain, app->pw_win, NULL,
                                   STRINGA_TextVal, clean_domain,
                                   TAG_DONE);
        IIntuition->SetGadgetAttrs((struct Gadget *)app->pw_gad_user, app->pw_win, NULL,
                                   STRINGA_TextVal, "",
                                   TAG_DONE);
        IIntuition->SetGadgetAttrs((struct Gadget *)app->pw_gad_pass, app->pw_win, NULL,
                                   STRINGA_HookType, app->pw_show_plaintext ? SHK_CUSTOM : SHK_PASSWORD,
                                   STRINGA_TextVal, "",
                                   TAG_DONE);
    }
}

BOOL gui_password_manager_is_open(VortexApp *app)
{
    return (app && app->pw_win_obj != NULL);
}

uint32 gui_get_password_manager_sigmask(VortexApp *app)
{
    uint32 sigmask = 0;
    if (app && app->pw_win_obj) {
        IIntuition->GetAttr(WINDOW_SigMask, app->pw_win_obj, &sigmask);
    }
    return sigmask;
}

void gui_close_password_manager(VortexApp *app)
{
    if (!app || !app->pw_win_obj) return;

    if (app->pw_gad_chooser) {
        IIntuition->SetAttrs(app->pw_gad_chooser, CHOOSER_Labels, (Tag)NULL, TAG_DONE);
    }

    IIntuition->DisposeObject(app->pw_win_obj);
    free_pw_chooser_list(app);
    app->pw_win_obj = NULL;
    app->pw_win = NULL;
    app->pw_gad_chooser = NULL;
    app->pw_gad_domain = NULL;
    app->pw_gad_user = NULL;
    app->pw_gad_pass = NULL;
    app->pw_gad_show_pass = NULL;
}

void gui_open_password_manager(VortexApp *app)
{
    if (!app) return;

    /* If already open, bring to front and activate without blocking */
    if (app->pw_win_obj && app->pw_win) {
        IIntuition->WindowToFront(app->pw_win);
        IIntuition->ActivateWindow(app->pw_win);
        return;
    }

    if (!app->password_store) {
        passwords_init(app);
    }

    struct Screen *target_scr = (app->win && app->win->WScreen) ? app->win->WScreen : app->custom_screen;

    IExec->NewList(&app->pw_chooser_list);

    app->pw_gad_chooser = IIntuition->NewObject(NULL, "chooser.gadget",
        GA_ID,             GID_PW_CHOOSER,
        GA_RelVerify,      TRUE,
        CHOOSER_Labels,    (Tag)&app->pw_chooser_list,
        CHOOSER_Selected,  0,
        CHOOSER_MaxLabels, 64,
        TAG_DONE);

    app->pw_gad_domain = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID,        GID_PW_DOMAIN,
        GA_RelVerify, TRUE,
        TAG_DONE);

    app->pw_gad_user = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID,        GID_PW_USER,
        GA_RelVerify, TRUE,
        TAG_DONE);

    Object *btn_copy_user = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        GID_PW_COPY_USER,
        GA_RelVerify, TRUE,
        GA_Text,      "Kopieren",
        TAG_DONE);

    app->pw_gad_pass = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID,            GID_PW_PASS,
        GA_RelVerify,     TRUE,
        STRINGA_HookType, SHK_PASSWORD,
        TAG_DONE);

    Object *btn_copy_pass = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        GID_PW_COPY_PASS,
        GA_RelVerify, TRUE,
        GA_Text,      "Kopieren",
        TAG_DONE);

    app->pw_gad_show_pass = IIntuition->NewObject(NULL, "checkbox.gadget",
        GA_ID,            GID_PW_SHOW_PASS,
        GA_RelVerify,     TRUE,
        GA_Text,          "Passwort im Klartext anzeigen",
        CHECKBOX_Checked, FALSE,
        TAG_DONE);

    Object *btn_autologin = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        GID_PW_AUTOLOGIN,
        GA_RelVerify, TRUE,
        GA_Text,      "In Seite einloggen (Auto-Login)",
        GA_HintInfo,  "Zugangsdaten automatisch im aktuellen Web-Formular ausfuellen und absenden",
        TAG_DONE);

    Object *btn_add = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        GID_PW_ADD,
        GA_RelVerify, TRUE,
        GA_Text,      "Speichern / Neu",
        GA_HintInfo,  "Obige Zugangsdaten verschluesselt speichern",
        TAG_DONE);

    Object *btn_del = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        GID_PW_DEL,
        GA_RelVerify, TRUE,
        GA_Text,      "Loeschen",
        GA_HintInfo,  "Ausgewaehlte Zugangsdaten entfernen",
        TAG_DONE);

    Object *btn_close = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        GID_PW_CLOSE,
        GA_RelVerify, TRUE,
        GA_Text,      "Schliessen",
        TAG_DONE);

    Object *root_layout = IIntuition->NewObject(NULL, "layout.gadget",
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_DeferLayout,   TRUE,

        /* Group 1: Chooser */
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              "Gespeicherte Logins:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_LEFT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  30,
                                  LAYOUT_AddChild,      app->pw_gad_chooser,
                                  CHILD_WeightedWidth,  70,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,

        /* Group 2: Domain */
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              "Webseite / Domaene:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_LEFT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  30,
                                  LAYOUT_AddChild,      app->pw_gad_domain,
                                  CHILD_WeightedWidth,  70,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,

        /* Group 3: User */
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              "Benutzername / E-Mail:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_LEFT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  30,
                                  LAYOUT_AddChild,      app->pw_gad_user,
                                  CHILD_WeightedWidth,  50,
                                  LAYOUT_AddChild,      btn_copy_user,
                                  CHILD_WeightedWidth,  20,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,

        /* Group 4: Password */
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                                            GA_Text,              "Passwort:",
                                                            GA_ReadOnly,          TRUE,
                                                            BUTTON_Justification, BCJ_LEFT,
                                                            TAG_DONE),
                                  CHILD_WeightedWidth,  30,
                                  LAYOUT_AddChild,      app->pw_gad_pass,
                                  CHILD_WeightedWidth,  50,
                                  LAYOUT_AddChild,      btn_copy_pass,
                                  CHILD_WeightedWidth,  20,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,

        /* Group 5: Show Pass */
        LAYOUT_AddChild,      app->pw_gad_show_pass,
        CHILD_WeightedHeight, 0,

        /* Group 6: Auto-Login Button */
        LAYOUT_AddChild,      btn_autologin,
        CHILD_WeightedHeight, 0,

        /* Group 7: Action Buttons */
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      btn_add,
                                  CHILD_WeightedWidth,  34,
                                  LAYOUT_AddChild,      btn_del,
                                  CHILD_WeightedWidth,  33,
                                  LAYOUT_AddChild,      btn_close,
                                  CHILD_WeightedWidth,  33,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        TAG_DONE);

    if (!root_layout) return;

    app->pw_win_obj = IIntuition->NewObject(NULL, "window.class",
        WA_Title,             "VortexBrowser Passwort-Manager",
        WA_ScreenTitle,       "VortexBrowser",
        target_scr ? WA_CustomScreen : TAG_IGNORE, (Tag)target_scr,
        WA_CloseGadget,       TRUE,
        WA_DepthGadget,       TRUE,
        WA_DragBar,           TRUE,
        WA_Activate,          TRUE,
        WA_SmartRefresh,      TRUE,
        WA_InnerWidth,        500,
        WA_InnerHeight,       240,
        WA_IDCMP,             IDCMP_CLOSEWINDOW | IDCMP_GADGETUP,
        WINDOW_Position,      WPOS_CENTERSCREEN,
        WINDOW_ParentGroup,   root_layout,
        TAG_DONE);

    if (!app->pw_win_obj) {
        IIntuition->DisposeObject(root_layout);
        return;
    }

    app->pw_win = (struct Window *)IIntuition->IDoMethod(app->pw_win_obj, WM_OPEN);
    if (!app->pw_win) {
        IIntuition->DisposeObject(app->pw_win_obj);
        app->pw_win_obj = NULL;
        return;
    }

    app->pw_show_plaintext = FALSE;
    app->pw_cur_sel = -1;

    /* Extract clean domain from current URL */
    char clean_domain[128];
    clean_domain[0] = '\0';
    const char *u = app->current_url[0] ? app->current_url :
                    (app->num_tabs > 0 ? app->tabs[app->active_tab_idx].url : "");
    if (u && u[0] != '\0' && u[0] != '@') {
        const char *p = u;
        if (strncmp(p, "http://", 7) == 0) p += 7;
        else if (strncmp(p, "https://", 8) == 0) p += 8;
        if (strncmp(p, "www.", 4) == 0) p += 4;
        strncpy(clean_domain, p, sizeof(clean_domain) - 1);
        clean_domain[sizeof(clean_domain) - 1] = '\0';
        char *slash = strchr(clean_domain, '/');
        if (slash) *slash = '\0';
        char *colon = strchr(clean_domain, ':');
        if (colon) *colon = '\0';
    }

    /* If current domain matches a saved domain, auto-select it */
    int select_idx = -1;
    if (clean_domain[0] != '\0' && app->password_store) {
        const PasswordEntry *pe = passwords_find_by_domain(app, clean_domain);
        if (pe) {
            for (int i = 0; i < app->password_store->count; i++) {
                if (&app->password_store->entries[i] == pe) {
                    select_idx = i;
                    break;
                }
            }
        }
    }

    rebuild_pw_chooser(app, select_idx >= 0 ? select_idx : 0);
    update_fields(app, select_idx);

    /* Window is now open modelessly! No blocking loop! */
}

void gui_handle_password_manager_events(VortexApp *app)
{
    if (!app || !app->pw_win_obj) return;

    uint32 result = 0;
    uint16 code = 0;
    while (app->pw_win_obj && (result = IIntuition->IDoMethod(app->pw_win_obj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
        switch (result & WMHI_CLASSMASK) {
            case WMHI_CLOSEWINDOW:
                gui_close_password_manager(app);
                return;

            case WMHI_GADGETUP: {
                uint32 gid = result & WMHI_GADGETMASK;
                if (gid == GID_PW_CLOSE) {
                    gui_close_password_manager(app);
                    return;
                } else if (gid == GID_PW_CHOOSER) {
                    uint32 sel = 0;
                    IIntuition->GetAttr(CHOOSER_Selected, app->pw_gad_chooser, &sel);
                    update_fields(app, (int)sel);
                } else if (gid == GID_PW_SHOW_PASS) {
                    uint32 chk = 0;
                    IIntuition->GetAttr(CHECKBOX_Checked, app->pw_gad_show_pass, &chk);
                    app->pw_show_plaintext = (chk != 0);
                    update_fields(app, app->pw_cur_sel);
                } else if (gid == GID_PW_AUTOLOGIN) {
                    if (app->password_store && app->pw_cur_sel >= 0 && app->pw_cur_sel < app->password_store->count) {
                        PasswordEntry *pe = &app->password_store->entries[app->pw_cur_sel];
                        char cmd[384];
                        snprintf(cmd, sizeof(cmd), "@AUTOLOGIN:%s\t%s", pe->username, pe->password);
                        net_send_url(app, cmd);
                        app_set_status("Auto-Login gesendet: %s (%s)", pe->domain, pe->username);
                    }
                } else if (gid == GID_PW_COPY_USER) {
                    if (app->password_store && app->pw_cur_sel >= 0 && app->pw_cur_sel < app->password_store->count) {
                        clipboard_write_text(app->password_store->entries[app->pw_cur_sel].username);
                        app_set_status("Benutzername ins Clipboard kopiert!");
                    }
                } else if (gid == GID_PW_COPY_PASS) {
                    if (app->password_store && app->pw_cur_sel >= 0 && app->pw_cur_sel < app->password_store->count) {
                        clipboard_write_text(app->password_store->entries[app->pw_cur_sel].password);
                        app_set_status("Passwort ins Clipboard kopiert!");
                    }
                } else if (gid == GID_PW_ADD) {
                    STRPTR d_txt = NULL, u_txt = NULL, p_txt = NULL;
                    IIntuition->GetAttr(STRINGA_TextVal, app->pw_gad_domain, (uint32 *)&d_txt);
                    IIntuition->GetAttr(STRINGA_TextVal, app->pw_gad_user,   (uint32 *)&u_txt);
                    IIntuition->GetAttr(STRINGA_TextVal, app->pw_gad_pass,   (uint32 *)&p_txt);
                    const char *final_p = (p_txt && p_txt[0] != '*' && p_txt[0] != '\0') ? p_txt : app->pw_real_pass;
                    if (d_txt && d_txt[0] && u_txt && u_txt[0] && final_p && final_p[0]) {
                        passwords_add(app, d_txt, u_txt, final_p, NULL);
                        int new_idx = app->password_store ? app->password_store->count - 1 : 0;
                        rebuild_pw_chooser(app, new_idx);
                        update_fields(app, new_idx);
                        app_set_status("Passwort gespeichert: %s (%s)", d_txt, u_txt);
                    }
                } else if (gid == GID_PW_DEL) {
                    if (app->password_store && app->pw_cur_sel >= 0 && app->pw_cur_sel < app->password_store->count) {
                        passwords_remove(app, app->pw_cur_sel);
                        int new_idx = (app->pw_cur_sel > 0) ? app->pw_cur_sel - 1 : 0;
                        rebuild_pw_chooser(app, new_idx);
                        update_fields(app, new_idx);
                        app_set_status("Zugangsdaten geloescht.");
                    }
                }
                break;
            }
        }
    }
}

void gui_do_autologin_current_domain(VortexApp *app)
{
    if (!app) return;

    if (!app->password_store) {
        passwords_init(app);
    }

    const char *url = app->current_url[0] ? app->current_url :
                      (app->num_tabs > 0 ? app->tabs[app->active_tab_idx].url : "");
    if (!url || url[0] == '\0' || url[0] == '@') {
        app_set_status("Keine aktive Webseite fuer Auto-Login.");
        gui_open_password_manager(app);
        return;
    }

    const PasswordEntry *pe = passwords_find_by_domain(app, url);
    if (pe) {
        char cmd[384];
        snprintf(cmd, sizeof(cmd), "@AUTOLOGIN:%s\t%s", pe->username, pe->password);
        net_send_url(app, cmd);
        app_set_status("Auto-Login fuer '%s' ausgefuehrt (%s)...", pe->domain, pe->username);
    } else {
        app_set_status("Kein Login fuer Domaene gefunden. Passwort-Manager geoeffnet.");
        gui_open_password_manager(app);
    }
}
