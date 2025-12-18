#include "tab.h"
#include "terminal.h"
#include "window.h"

VteTerminal* add_tab(GtkNotebook* notebook) {
    g_print("add_tab called\n");
    // Create terminal
    VteTerminal* vt = VTE_TERMINAL(vte_terminal_new());

    // Setup terminal
    setup_terminal(vt);
    setup_background_color(vt);

    // Create scrolled window
    GtkWidget* scr = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scr), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scr), GTK_WIDGET(vt));

    // Create tab label with close button
    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget* label = gtk_label_new("Terminal");
    GtkWidget* close_btn = gtk_button_new_from_icon_name("window-close");
    gtk_button_set_has_frame(GTK_BUTTON(close_btn), FALSE);
    gtk_widget_set_cursor_from_name(close_btn, "pointer");
    gtk_box_append(GTK_BOX(hbox), label);
    gtk_box_append(GTK_BOX(hbox), close_btn);
    gtk_widget_set_visible(hbox, TRUE);

    // Append to notebook
    gtk_notebook_append_page(notebook, scr, hbox);
    // Switch to the new tab
    int page_num = gtk_notebook_page_num(notebook, scr);
    if (page_num >= 0) {
        gtk_notebook_set_current_page(notebook, page_num);
    }

    // Connect signals
    g_signal_connect(vt, "selection-changed", G_CALLBACK(on_selection_changed), NULL);
    setup_key_events(vt);

    // Connect child-exit with notebook as user_data
    g_signal_connect(vt, "child-exited", G_CALLBACK(on_child_exit_tab), notebook);

    // Connect title updates
#if VTE_CHECK_VERSION(0, 78, 0)
    g_signal_connect(vt, "termprop-changed", G_CALLBACK(title_tab_sig_cb_new), notebook);
#else
    g_signal_connect(vt, "window-title-changed", G_CALLBACK(title_tab_sig_cb_old), notebook);
    g_signal_connect(vt, "current-directory-uri-changed", G_CALLBACK(title_tab_sig_cb_old), notebook);
#endif

    // Spawn shell
    setup_pty_and_shell(vt);

    // Connect close button
    g_signal_connect(close_btn, "clicked", G_CALLBACK(on_tab_close_clicked), notebook);

    // Set initial tab label
    update_tab_title(vt, notebook);

    return vt;
}

void on_tab_close_clicked(GtkButton* btn, gpointer user_data) {
    GtkNotebook* notebook = GTK_NOTEBOOK(user_data);
    if (!notebook)
        return;
    // Iterate pages to find which page has this button
    int n = gtk_notebook_get_n_pages(notebook);
    for (int i = 0; i < n; i++) {
        GtkWidget* page = gtk_notebook_get_nth_page(notebook, i);
        GtkWidget* tab_label = gtk_notebook_get_tab_label(notebook, page);
        if (tab_label && gtk_widget_is_ancestor(GTK_WIDGET(btn), tab_label)) {
            // Close the page
            gtk_notebook_remove_page(notebook, i);
            break;
        }
    }
}

void on_child_exit_tab(VteTerminal* vt, int status, GtkNotebook* notebook) {
    if (!notebook)
        return;
    g_print("Shell exited (status=%d). Closing tab\n", status);
    // Find page index for this terminal
    int n = gtk_notebook_get_n_pages(notebook);
    for (int i = 0; i < n; i++) {
        GtkWidget* page = gtk_notebook_get_nth_page(notebook, i);
        // page is scrolled window, its child is terminal
        GtkWidget* child = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(page));
        if (GTK_WIDGET(vt) == child) {
            gtk_notebook_remove_page(notebook, i);
            break;
        }
    }
    // If no pages left, close window
    if (gtk_notebook_get_n_pages(notebook) == 0) {
        // Handled in on_notebook_page_removed
    }
}

static char* get_terminal_title(VteTerminal* vt) {
    gsize len = 0;
#if VTE_CHECK_VERSION(0, 78, 0)
    const char* shell_title = vte_terminal_get_termprop_string(vt, VTE_TERMPROP_XTERM_TITLE, &len);
    g_autoptr(GUri) cwd_uri = vte_terminal_ref_termprop_uri(vt, VTE_TERMPROP_CURRENT_DIRECTORY_URI);
#else
    const char* shell_title = vte_terminal_get_window_title(vt);
    const char* cwd_uri_str = vte_terminal_get_current_directory_uri(vt);
    g_autoptr(GUri) cwd_uri = NULL;
    if (cwd_uri_str)
        cwd_uri = g_uri_parse(cwd_uri_str, G_URI_FLAGS_NONE, NULL);
#endif
    char* title = NULL;
    if (shell_title && *shell_title) {
        title = g_strdup(shell_title);
    }
    else if (cwd_uri) {
        const char* scheme = g_uri_get_scheme(cwd_uri);
        if (scheme && g_str_equal(scheme, "file")) {
            const char* path = g_uri_get_path(cwd_uri);
            if (path && *path) {
                title = g_strdup_printf("%s@%s", g_get_user_name(), path);
            }
        }
    }
    if (!title) {
        title = g_strdup_printf("%s@?", g_get_user_name());
    }
    return title;
}

void update_tab_title(VteTerminal* vt, GtkNotebook* notebook) {
    if (!notebook)
        return;
    // Find page index for this terminal
    int n = gtk_notebook_get_n_pages(notebook);
    for (int i = 0; i < n; i++) {
        GtkWidget* page = gtk_notebook_get_nth_page(notebook, i);
        if (!page || !GTK_IS_SCROLLED_WINDOW(page))
            continue;
        GtkWidget* child = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(page));
        if (GTK_WIDGET(vt) == child) {
            GtkWidget* tab_label = gtk_notebook_get_tab_label(notebook, page);
            if (!tab_label)
                break;
            // tab_label is the hbox, its first child is GtkLabel
            GtkWidget* label = gtk_widget_get_first_child(tab_label);
            if (label && GTK_IS_LABEL(label)) {
                g_autofree char* title = get_terminal_title(vt);
                gtk_label_set_text(GTK_LABEL(label), title);

                // Also update window title if this is the active tab
                if (i == gtk_notebook_get_current_page(notebook)) {
                    GtkWidget* window = gtk_widget_get_ancestor(GTK_WIDGET(notebook), GTK_TYPE_WINDOW);
                    if (window)
                        gtk_window_set_title(GTK_WINDOW(window), title);
                }
            }
            break;
        }
    }
}

void on_notebook_page_added(GtkNotebook* notebook, GtkWidget* child, guint page_num, gpointer user_data) {
    (void)child;
    (void)page_num;
    (void)user_data;
    g_print("on_notebook_page_added: pages=%d\n", gtk_notebook_get_n_pages(notebook));
    // Always show tabs
    gtk_notebook_set_show_tabs(notebook, TRUE);
}

void on_notebook_page_removed(GtkNotebook* notebook, GtkWidget* child, guint page_num, gpointer user_data) {
    (void)child;
    (void)page_num;
    (void)user_data;
    // Always show tabs
    gtk_notebook_set_show_tabs(notebook, TRUE);

    // If no pages left, close window is already handled in on_child_exit_tab or elsewhere?
    // Actually it's safer here too.
    if (gtk_notebook_get_n_pages(notebook) == 0) {
        GtkWidget* window = gtk_widget_get_ancestor(GTK_WIDGET(notebook), GTK_TYPE_WINDOW);
        if (window)
            gtk_window_close(GTK_WINDOW(window));
    }
    else {
        // Update window title for the now-current page
        int current = gtk_notebook_get_current_page(notebook);
        if (current >= 0) {
            GtkWidget* page = gtk_notebook_get_nth_page(notebook, current);
            GtkWidget* vt_child = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(page));
            if (vt_child && VTE_IS_TERMINAL(vt_child)) {
                g_autofree char* title = get_terminal_title(VTE_TERMINAL(vt_child));
                GtkWidget* window = gtk_widget_get_ancestor(GTK_WIDGET(notebook), GTK_TYPE_WINDOW);
                if (window)
                    gtk_window_set_title(GTK_WINDOW(window), title);
                gtk_widget_grab_focus(vt_child);
            }
        }
    }
}

void on_notebook_switch_page(GtkNotebook* notebook, GtkWidget* page, guint page_num, gpointer user_data) {
    (void)page_num;
    (void)user_data;

    if (!page || !GTK_IS_SCROLLED_WINDOW(page))
        return;
    GtkWidget* child = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(page));
    if (!child || !VTE_IS_TERMINAL(child))
        return;

    VteTerminal* vt = VTE_TERMINAL(child);

    // Update window title explicitly using the new page's terminal
    g_autofree char* title = get_terminal_title(vt);
    GtkWidget* window = gtk_widget_get_ancestor(GTK_WIDGET(notebook), GTK_TYPE_WINDOW);
    if (window)
        gtk_window_set_title(GTK_WINDOW(window), title);

    // Also update the tab label just in case
    update_tab_title(vt, notebook);

    // Ensure the terminal has focus
    gtk_widget_grab_focus(GTK_WIDGET(vt));
}
void close_current_tab(GtkNotebook* notebook) {
    int current = gtk_notebook_get_current_page(notebook);
    if (current >= 0) {
        gtk_notebook_remove_page(notebook, current);
    }
}

void title_tab_sig_cb_new(VteTerminal* vt, guint prop_id, gpointer user_data) {
    (void)prop_id;
    update_tab_title(vt, GTK_NOTEBOOK(user_data));
}

#if !VTE_CHECK_VERSION(0, 78, 0)
void title_tab_sig_cb_old(VteTerminal* vt, gpointer user_data) {
    update_tab_title(vt, GTK_NOTEBOOK(user_data));
}
#endif
