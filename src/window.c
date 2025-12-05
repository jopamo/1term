#include "window.h"
#include "terminal.h"
#include "tab.h"

G_DEFINE_TYPE(MyWindow, my_window, GTK_TYPE_APPLICATION_WINDOW)

// Global settings
gboolean transparency_enabled = TRUE;
gboolean scrollback_enabled = TRUE;
GtkCssProvider* css_provider = NULL;

static void my_window_css_changed(GtkWidget* w, GtkCssStyleChange* c) {
    GTK_WIDGET_CLASS(my_window_parent_class)->css_changed(w, c);
}

static void my_window_class_init(MyWindowClass* klass) {
    GTK_WIDGET_CLASS(klass)->css_changed = my_window_css_changed;
}

static void my_window_init(MyWindow* self) {
    self->notebook = NULL;
}

GtkWidget* my_window_new(GtkApplication* app) {
    return g_object_new(my_window_get_type(), "application", app, NULL);
}

GtkNotebook* my_window_get_notebook(MyWindow* self) {
    return self->notebook;
}

static void setup_window_size(GtkWidget* win, int window_count) {
    GdkDisplay* display = gtk_widget_get_display(win);
    GListModel* monitors = gdk_display_get_monitors(display);
    GdkMonitor* monitor = g_list_model_get_item(monitors, 0);
    GdkRectangle geometry;
    gdk_monitor_get_geometry(monitor, &geometry);
    g_object_unref(monitor);

    int border_correction = 8;

    int width = geometry.width / 2 - border_correction - window_count * 20;
    int height = geometry.height / 5 - window_count * 10;
    if (width < 150)
        width = 150;
    if (height < 80)
        height = 80;

    gtk_window_set_default_size(GTK_WINDOW(win), width, height);
}

static void apply_css(GtkWidget* win) {
    GdkDisplay* display = gtk_widget_get_display(win);
    if (!css_provider) {
        css_provider = gtk_css_provider_new();
        gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(css_provider),
                                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    update_css_transparency();
}

void update_css_transparency(void) {
    if (!css_provider)
        return;
    gdouble alpha = transparency_enabled ? 0.8 : 1.0;
    g_autofree char* css = g_strdup_printf(
        "window{background-color:rgba(0,0,0,0);} "
        "notebook{background-color:rgba(0,0,0,0);} "
        "notebook > header{background-color:rgba(0,0,0,%g);} "
        "notebook > stack{background-color:rgba(0,0,0,0);} "
        "scrolledwindow{background-color:rgba(0,0,0,0);} "
        "scrollbar{background-color:rgba(0,0,0,%g);} "
        "vte-terminal{background-color:rgba(0,0,0,0);}",
        alpha, alpha);
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    gtk_css_provider_load_from_data(css_provider, css, -1);
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
}

void update_transparency_for_notebook(GtkNotebook* notebook) {
    if (!notebook)
        return;
    int n = gtk_notebook_get_n_pages(notebook);
    for (int i = 0; i < n; i++) {
        GtkWidget* page = gtk_notebook_get_nth_page(notebook, i);
        if (!page || !GTK_IS_SCROLLED_WINDOW(page))
            continue;
        GtkWidget* child = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(page));
        if (child && VTE_IS_TERMINAL(child)) {
            setup_background_color(VTE_TERMINAL(child));
        }
    }
}

void update_transparency_all(void) {
    GList* toplevels = gtk_window_list_toplevels();
    for (GList* l = toplevels; l; l = l->next) {
        GtkWindow* win = GTK_WINDOW(l->data);
        if (MY_IS_WINDOW(win)) {
            MyWindow* mywin = MY_WINDOW(win);
            if (mywin->notebook) {
                update_transparency_for_notebook(mywin->notebook);
            }
        }
    }
    g_list_free(toplevels);
}

void update_scrollback_for_notebook(GtkNotebook* notebook) {
    if (!notebook)
        return;
    int n = gtk_notebook_get_n_pages(notebook);
    for (int i = 0; i < n; i++) {
        GtkWidget* page = gtk_notebook_get_nth_page(notebook, i);
        if (!page || !GTK_IS_SCROLLED_WINDOW(page))
            continue;
        GtkWidget* child = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(page));
        if (child && VTE_IS_TERMINAL(child)) {
            vte_terminal_set_scrollback_lines(VTE_TERMINAL(child), scrollback_enabled ? 100000 : 0);
        }
    }
}

void update_scrollback_all(void) {
    GList* toplevels = gtk_window_list_toplevels();
    for (GList* l = toplevels; l; l = l->next) {
        GtkWindow* win = GTK_WINDOW(l->data);
        if (MY_IS_WINDOW(win)) {
            MyWindow* mywin = MY_WINDOW(win);
            if (mywin->notebook) {
                update_scrollback_for_notebook(mywin->notebook);
            }
        }
    }
    g_list_free(toplevels);
}

GtkNotebook* get_notebook_from_terminal(VteTerminal* vt) {
    GtkWidget* widget = GTK_WIDGET(vt);
    GtkWidget* scr = gtk_widget_get_parent(widget);  // scrolled window
    if (!scr) {
        g_print("get_notebook_from_terminal: no scrolled window parent, trying window fallback\n");
        // Fallback: get window and access its notebook member
        GtkWidget* window = gtk_widget_get_ancestor(widget, GTK_TYPE_WINDOW);
        if (window && MY_IS_WINDOW(window)) {
            MyWindow* mywin = MY_WINDOW(window);
            if (mywin->notebook) {
                g_print("get_notebook_from_terminal: found notebook via window fallback\n");
                return mywin->notebook;
            }
        }
        return NULL;
    }
    GtkWidget* notebook = gtk_widget_get_parent(scr);  // notebook
    if (!notebook || !GTK_IS_NOTEBOOK(notebook)) {
        g_print("get_notebook_from_terminal: no notebook parent or not notebook, trying window fallback\n");
        // Fallback
        GtkWidget* window = gtk_widget_get_ancestor(widget, GTK_TYPE_WINDOW);
        if (window && MY_IS_WINDOW(window)) {
            MyWindow* mywin = MY_WINDOW(window);
            if (mywin->notebook) {
                g_print("get_notebook_from_terminal: found notebook via window fallback\n");
                return mywin->notebook;
            }
        }
        return NULL;
    }
    g_print("get_notebook_from_terminal: found notebook via parent chain\n");
    return GTK_NOTEBOOK(notebook);
}

void create_window(GtkApplication* app) {
    static int window_count = 0;

    GtkWidget* win = my_window_new(app);
    MyWindow* mywin = MY_WINDOW(win);

    // Create notebook
    GtkNotebook* notebook = GTK_NOTEBOOK(gtk_notebook_new());
    mywin->notebook = notebook;

    // Connect notebook signals
    g_signal_connect(notebook, "page-added", G_CALLBACK(on_notebook_page_added), NULL);
    g_signal_connect(notebook, "page-removed", G_CALLBACK(on_notebook_page_removed), NULL);
    g_signal_connect(notebook, "switch-page", G_CALLBACK(on_notebook_switch_page), NULL);

    // Set notebook as window child
    gtk_window_set_child(GTK_WINDOW(win), GTK_WIDGET(notebook));

    setup_window_size(win, window_count);
    window_count++;

    gtk_window_set_icon_name(GTK_WINDOW(win), "1term");

    apply_css(win);

    // Add first tab
    add_tab(notebook);

    gtk_window_present(GTK_WINDOW(win));
    // Window title will be set via update_tab_title
}