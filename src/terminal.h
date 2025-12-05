#ifndef TERMINAL_H
#define TERMINAL_H

#include "1term.h"

G_BEGIN_DECLS

void setup_background_color(VteTerminal* vt);
void setup_terminal(VteTerminal* vt);
void vte_set_robust_word_chars(VteTerminal* vt);
void setup_key_events(VteTerminal* vt);
void setup_pty_and_shell(VteTerminal* vt);
void on_selection_changed(VteTerminal* vt, gpointer user_data);
gboolean on_key_pressed(GtkEventControllerKey* ctrl,
                        guint keyval,
                        guint keycode,
                        GdkModifierType state,
                        gpointer user_data);

G_END_DECLS

#endif  // TERMINAL_H