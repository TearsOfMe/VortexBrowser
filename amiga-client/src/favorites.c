#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/chooser.h>
#include <gadgets/chooser.h>

#include "favorites.h"
#include "gui.h"

static void ensure_dir_exists(const char *dir_path)
{
    BPTR lock = IDOS->Lock(dir_path, SHARED_LOCK);
    if (lock) {
        IDOS->UnLock(lock);
        return;
    }
    lock = IDOS->CreateDir(dir_path);
    if (lock) {
        IDOS->UnLock(lock);
    }
}

static void populate_defaults(VortexApp *app)
{
    app->num_favorites = 0;
    favorites_add(app, "VortexBrowse Portal (Start)", "@HOME");
    favorites_add(app, "amiga-news.de", "https://www.amiga-news.de");
    favorites_add(app, "OS4Depot", "https://os4depot.net");
    favorites_add(app, "Amigans.net", "https://www.amigans.net");
    favorites_add(app, "YouTube", "https://www.youtube.com");
    favorites_add(app, "Wikipedia AmigaOS", "https://de.wikipedia.org/wiki/AmigaOS");
    favorites_add(app, "Google Suche", "https://www.google.de");
}

void favorites_init(VortexApp *app)
{
    IExec->NewList(&app->fav_chooser_list);
    app->num_favorites = 0;

    if (!favorites_load(app) || app->num_favorites == 0) {
        populate_defaults(app);
        favorites_save(app);
    }
    favorites_rebuild_chooser_list(app);
}

static BOOL load_from_path(VortexApp *app, const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return FALSE;
    }

    app->num_favorites = 0;
    char line[768];
    while (fgets(line, sizeof(line), fp) && app->num_favorites < VB_MAX_FAVORITES) {
        /* Strip trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0 || line[0] == '#') {
            continue;
        }
        char *sep = strchr(line, '|');
        if (sep) {
            *sep = '\0';
            const char *title = line;
            const char *url   = sep + 1;
            if (url[0] != '\0') {
                FavoriteItem *item = &app->favorites[app->num_favorites++];
                snprintf(item->title, VB_MAX_TITLE_LEN, "%s", title);
                snprintf(item->url, VB_MAX_URL_LEN, "%s", url);
            }
        }
    }
    fclose(fp);
    return (app->num_favorites > 0) ? TRUE : FALSE;
}

BOOL favorites_load(VortexApp *app)
{
    if (load_from_path(app, VB_ENV_FILE)) {
        return TRUE;
    }
    if (load_from_path(app, VB_ENVARC_FILE)) {
        return TRUE;
    }
    return FALSE;
}

static BOOL save_to_path(const VortexApp *app, const char *path)
{
    FILE *fp = fopen(path, "w");
    if (!fp) {
        return FALSE;
    }
    fprintf(fp, "# VortexBrowse Favorites Preferences\n");
    for (int i = 0; i < app->num_favorites; i++) {
        fprintf(fp, "%s|%s\n", app->favorites[i].title, app->favorites[i].url);
    }
    fclose(fp);
    return TRUE;
}

BOOL favorites_save(const VortexApp *app)
{
    ensure_dir_exists(VB_ENV_DIR);
    ensure_dir_exists(VB_ENVARC_DIR);

    BOOL ok_env    = save_to_path(app, VB_ENV_FILE);
    BOOL ok_envarc = save_to_path(app, VB_ENVARC_FILE);
    return (ok_env || ok_envarc) ? TRUE : FALSE;
}

BOOL favorites_add(VortexApp *app, const char *title, const char *url)
{
    if (!url || url[0] == '\0') {
        return FALSE;
    }
    /* Avoid duplicates */
    for (int i = 0; i < app->num_favorites; i++) {
        if (strcmp(app->favorites[i].url, url) == 0) {
            if (title && title[0] != '\0') {
                strncpy(app->favorites[i].title, title, VB_MAX_TITLE_LEN - 1);
                app->favorites[i].title[VB_MAX_TITLE_LEN - 1] = '\0';
            }
            return TRUE;
        }
    }
    if (app->num_favorites >= VB_MAX_FAVORITES) {
        return FALSE;
    }
    FavoriteItem *item = &app->favorites[app->num_favorites++];
    const char *use_title = (title && title[0] != '\0') ? title : url;
    strncpy(item->title, use_title, VB_MAX_TITLE_LEN - 1);
    item->title[VB_MAX_TITLE_LEN - 1] = '\0';
    strncpy(item->url, url, VB_MAX_URL_LEN - 1);
    item->url[VB_MAX_URL_LEN - 1] = '\0';
    return TRUE;
}

void favorites_free_chooser_list(VortexApp *app)
{
    if (!IChooser) {
        return;
    }
    struct Node *node = IExec->RemHead(&app->fav_chooser_list);
    while (node) {
        IChooser->FreeChooserNode(node);
        node = IExec->RemHead(&app->fav_chooser_list);
    }
}

void favorites_rebuild_chooser_list(VortexApp *app)
{
    if (app->gad_fav_chooser && app->win) {
        IIntuition->SetGadgetAttrs((struct Gadget *)app->gad_fav_chooser, app->win, NULL,
                                   CHOOSER_Labels, ~0,
                                   TAG_DONE);
    }

    if (!IChooser) {
        return;
    }

    favorites_free_chooser_list(app);

    int32 bg_pen = gui_get_dark_btn_bg();
    int32 txt_pen = gui_get_dark_txt_pen();

    for (int i = 0; i < app->num_favorites; i++) {
        struct Node *node = IChooser->AllocChooserNode(
            CNA_CopyText, TRUE,
            CNA_Text,     app->favorites[i].title,
            CNA_UserData, (APTR)(uintptr_t)i,
            (bg_pen >= 0) ? CNA_BGPen : TAG_IGNORE, (WORD)bg_pen,
            (txt_pen >= 0) ? CNA_FGPen : TAG_IGNORE, (WORD)txt_pen,
            TAG_DONE);
        if (node) {
            IExec->AddTail(&app->fav_chooser_list, node);
        }
    }

    if (app->gad_fav_chooser) {
        IIntuition->SetGadgetAttrs((struct Gadget *)app->gad_fav_chooser, app->win, NULL,
                                   CHOOSER_Labels, &app->fav_chooser_list,
                                   TAG_DONE);
    }
}
