#ifndef VORTEXBROWSE_PASSWORDS_H
#define VORTEXBROWSE_PASSWORDS_H

#include <exec/types.h>

#define VB_MAX_PASSWORDS      128
#define VB_PW_MAX_DOMAIN      128
#define VB_PW_MAX_USER        128
#define VB_PW_MAX_PASS        128
#define VB_PW_MAX_NOTES       256

typedef struct PasswordEntry {
    char domain[VB_PW_MAX_DOMAIN];
    char username[VB_PW_MAX_USER];
    char password[VB_PW_MAX_PASS];
    char notes[VB_PW_MAX_NOTES];
} PasswordEntry;

typedef struct PasswordStore {
    PasswordEntry entries[VB_MAX_PASSWORDS];
    int           count;
    char          master_password[128];
    BOOL          unlocked;
    BOOL          has_custom_master;
    BOOL          modified;
} PasswordStore;

struct VortexApp;

BOOL passwords_init(struct VortexApp *app);
void passwords_cleanup(struct VortexApp *app);

BOOL passwords_file_exists(void);
BOOL passwords_unlock(struct VortexApp *app, const char *master_pw);
BOOL passwords_save(struct VortexApp *app);
BOOL passwords_set_master(struct VortexApp *app, const char *new_master_pw);

BOOL passwords_add(struct VortexApp *app, const char *domain, const char *user, const char *pass, const char *notes);
BOOL passwords_update(struct VortexApp *app, int index, const char *domain, const char *user, const char *pass, const char *notes);
BOOL passwords_remove(struct VortexApp *app, int index);
const PasswordEntry *passwords_find_by_domain(struct VortexApp *app, const char *domain);

#endif /* VORTEXBROWSE_PASSWORDS_H */
