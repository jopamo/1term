#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include "1term.h"

G_BEGIN_DECLS

void compress_scrollback_via_clipboard_async(VteTerminal* vt);
void free_compress_pool(void);

G_END_DECLS

#endif  // CLIPBOARD_H