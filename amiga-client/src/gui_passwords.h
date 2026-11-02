#ifndef VORTEXBROWSE_GUI_PASSWORDS_H
#define VORTEXBROWSE_GUI_PASSWORDS_H

#include <exec/types.h>

struct VortexApp;

void   gui_open_password_manager(struct VortexApp *app);
void   gui_close_password_manager(struct VortexApp *app);
void   gui_handle_password_manager_events(struct VortexApp *app);
uint32 gui_get_password_manager_sigmask(struct VortexApp *app);
BOOL   gui_password_manager_is_open(struct VortexApp *app);
void   gui_do_autologin_current_domain(struct VortexApp *app);

#endif /* VORTEXBROWSE_GUI_PASSWORDS_H */
