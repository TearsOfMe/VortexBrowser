#include <proto/exec.h>
#include <proto/textclip.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "app.h"
#include "clipboard.h"

BOOL clipboard_init(void)
{
    /* With -lauto, ITextClip and TextClipBase are auto-opened if available. */
    if (ITextClip) {
        DBG_LOG("[Clipboard] textclip.library initialized successfully.\n");
        return TRUE;
    }
    DBG_LOG("[Clipboard] textclip.library interface not available.\n");
    return FALSE;
}

void clipboard_cleanup(void)
{
    /* Handled by -lauto on program exit */
}

BOOL clipboard_write_text(const char *text)
{
    if (!text || text[0] == '\0') {
        return FALSE;
    }

    if (ITextClip) {
        ULONG len = (ULONG)strlen(text);
        LONG res = ITextClip->WriteClipVector((STRPTR)text, len);
        if (res) {
            DBG_LOG("[Clipboard] Copied %lu bytes to Amiga clipboard.\n", (unsigned long)len);
            return TRUE;
        }
    }

    return FALSE;
}

char *clipboard_read_text(void)
{
    if (ITextClip) {
        STRPTR clip_str = NULL;
        ULONG  clip_len = 0;
        LONG res = ITextClip->ReadClipVector(&clip_str, &clip_len);
        if (res && clip_str) {
            char *copy = (char *)malloc(clip_len + 1);
            if (copy) {
                memcpy(copy, clip_str, clip_len);
                copy[clip_len] = '\0';
            }
            ITextClip->DisposeClipVector(clip_str);
            return copy;
        }
    }

    return NULL;
}

