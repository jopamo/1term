#ifndef WINDOW_H
#define WINDOW_H

#include "1term.h"

G_BEGIN_DECLS

#define MY_TYPE_WINDOW (my_window_get_type())
G_DECLARE_FINAL_TYPE(MyWindow, my_window, MY, WINDOW, GtkApplicationWindow)

struct _MyWindow {
    GtkApplicationWindow parent_instance;
    GtkNotebook* notebook;
};

GtkWidget* my_window_new(GtkApplication* app);
GtkNotebook* my_window_get_notebook(MyWindow* self);

void create_window(GtkApplication* app);
void update_transparency_for_notebook(GtkNotebook* notebook);
void update_transparency_all(void);
void update_scrollback_for_notebook(GtkNotebook* notebook);
void update_scrollback_all(void);
void update_css_transparency(void);
GtkNotebook* get_notebook_from_terminal(VteTerminal* vt);

G_END_DECLS

#endif  // WINDOW_H