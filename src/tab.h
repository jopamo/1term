#ifndef TAB_H
#define TAB_H

#include "1term.h"

G_BEGIN_DECLS

VteTerminal* add_tab(GtkNotebook* notebook);
void close_current_tab(GtkNotebook* notebook);
void on_tab_close_clicked(GtkButton* btn, gpointer user_data);
void on_child_exit_tab(VteTerminal* vt, int status, GtkNotebook* notebook);
void update_tab_title(VteTerminal* vt, GtkNotebook* notebook);
void on_notebook_page_added(GtkNotebook* notebook, GtkWidget* child, guint page_num, gpointer user_data);
void on_notebook_page_removed(GtkNotebook* notebook, GtkWidget* child, guint page_num, gpointer user_data);
void on_notebook_switch_page(GtkNotebook* notebook, GtkWidget* page, guint page_num, gpointer user_data);
void title_tab_sig_cb_new(VteTerminal* vt, guint prop_id, gpointer user_data);
#if !VTE_CHECK_VERSION(0, 78, 0)
void title_tab_sig_cb_old(VteTerminal* vt, gpointer user_data);
#endif

G_END_DECLS

#endif  // TAB_H