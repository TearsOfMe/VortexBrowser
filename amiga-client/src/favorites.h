#ifndef VORTEXBROWSE_FAVORITES_H
#define VORTEXBROWSE_FAVORITES_H

#include "app.h"

#define VB_ENV_DIR     "ENV:VortexBrowser"
#define VB_ENVARC_DIR  "ENVARC:VortexBrowser"
#define VB_ENV_FILE    "ENV:VortexBrowser/favorites.prefs"
#define VB_ENVARC_FILE "ENVARC:VortexBrowser/favorites.prefs"

void favorites_init(VortexApp *app);
BOOL favorites_load(VortexApp *app);
BOOL favorites_save(const VortexApp *app);
BOOL favorites_add(VortexApp *app, const char *title, const char *url);
void favorites_free_chooser_list(VortexApp *app);
void favorites_rebuild_chooser_list(VortexApp *app);

#endif /* VORTEXBROWSE_FAVORITES_H */
