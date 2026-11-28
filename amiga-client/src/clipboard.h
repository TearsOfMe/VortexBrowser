#ifndef VORTEXREMOTEBROWSER_CLIPBOARD_H
#define VORTEXREMOTEBROWSER_CLIPBOARD_H

#include <exec/types.h>

#ifdef __cplusplus
extern "C" {
#endif

BOOL  clipboard_init(void);
void  clipboard_cleanup(void);

BOOL  clipboard_write_text(const char *text);
char *clipboard_read_text(void);

#ifdef __cplusplus
}
#endif

#endif /* VORTEXREMOTEBROWSER_CLIPBOARD_H */

