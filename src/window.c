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
    int border_correction = 8;

    int width = 800 - border_correction - window_count * 20;
    int height = 400 - window_count * 10;

    if (monitors && g_list_model_get_n_items(monitors) > 0) {
        GdkMonitor* monitor = g_list_model_get_item(monitors, 0);
        if (monitor) {
            GdkRectangle geometry;
            gdk_monitor_get_geometry(monitor, &geometry);
            g_object_unref(monitor);

            width = geometry.width / 2 - border_correction - window_count * 20;
            height = geometry.height / 5 - window_count * 10;
        }
    }

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
    gdouble alpha = transparency_enabled ? 0.95 : 1.0;
    g_autofree char* css = g_strdup_printf(
        "window{background-color:rgba(0,0,0,0); border: 1px solid rgba(255,255,255,0.1);} "
        ".hidden-titlebar{min-height:0;margin:0;padding:0;border:none;background:none;box-shadow:none;} "
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

static void on_minimize_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    GtkWindow* win = GTK_WINDOW(user_data);
    gtk_window_minimize(win);
}

static void on_maximize_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    GtkWindow* win = GTK_WINDOW(user_data);
    if (gtk_window_is_maximized(win))
        gtk_window_unmaximize(win);
    else
        gtk_window_maximize(win);
}

static void on_close_window_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    GtkWindow* win = GTK_WINDOW(user_data);
    gtk_window_close(win);
}

static void on_notebook_drag_begin(GtkGestureDrag* gesture, double start_x, double start_y, gpointer user_data) {
    GtkNotebook* notebook = GTK_NOTEBOOK(user_data);

    // Get the widget that was actually clicked
    GtkWidget* target = gtk_widget_pick(GTK_WIDGET(notebook), start_x, start_y, GTK_PICK_DEFAULT);
    if (!target)
        target = GTK_WIDGET(notebook);

    // Do not initiate move if we clicked on a button (like close/min/max)
    if (gtk_widget_get_ancestor(target, GTK_TYPE_BUTTON)) {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
        return;
    }

    // Do not initiate move if we clicked inside the terminal/page area
    int n_pages = gtk_notebook_get_n_pages(notebook);
    for (int i = 0; i < n_pages; i++) {
        GtkWidget* page = gtk_notebook_get_nth_page(notebook, i);
        if (target == page || gtk_widget_is_ancestor(target, page)) {
            gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
            return;
        }
    }

    // Do not initiate move if the drag started on a tab. Otherwise, small pointer
    // movements while clicking can be interpreted as a window drag, making tab
    // switching feel flaky (e.g., requiring double-clicks).
    for (int i = 0; i < n_pages; i++) {
        GtkWidget* page = gtk_notebook_get_nth_page(notebook, i);
        GtkWidget* tab_label = page ? gtk_notebook_get_tab_label(notebook, page) : NULL;
        if (!tab_label)
            continue;

        graphene_rect_t bounds;
        if (gtk_widget_compute_bounds(tab_label, GTK_WIDGET(notebook), &bounds)) {
            const float x0 = bounds.origin.x;
            const float y0 = bounds.origin.y;
            const float x1 = x0 + bounds.size.width;
            const float y1 = y0 + bounds.size.height;
            if (start_x >= x0 && start_x < x1 && start_y >= y0 && start_y < y1) {
                gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
                return;
            }
        }

        if (target == tab_label || gtk_widget_is_ancestor(target, tab_label)) {
            gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
            return;
        }
    }

    GtkWidget* win = gtk_widget_get_ancestor(GTK_WIDGET(notebook), GTK_TYPE_WINDOW);
    if (!win) {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
        return;
    }

    GtkNative* native = GTK_NATIVE(win);
    GdkSurface* surface = gtk_native_get_surface(native);

    if (GDK_IS_TOPLEVEL(surface)) {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
        gdk_toplevel_begin_move(GDK_TOPLEVEL(surface),
                                gtk_event_controller_get_current_event_device(GTK_EVENT_CONTROLLER(gesture)),
                                gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)), start_x, start_y,
                                gtk_event_controller_get_current_event_time(GTK_EVENT_CONTROLLER(gesture)));
    }
    else {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
    }
}

void create_window(GtkApplication* app) {
    static int window_count = 0;

    GtkWidget* win = my_window_new(app);
    MyWindow* mywin = MY_WINDOW(win);

    // Explicitly allow resizing
    gtk_window_set_resizable(GTK_WINDOW(win), TRUE);
    gtk_window_set_decorated(GTK_WINDOW(win), TRUE);

    // Use a custom (empty) titlebar to enable CSD but hide the default header bar
    GtkWidget* titlebar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(titlebar, "hidden-titlebar");
    gtk_window_set_titlebar(GTK_WINDOW(win), titlebar);

    // Create notebook
    GtkNotebook* notebook = GTK_NOTEBOOK(gtk_notebook_new());
    mywin->notebook = notebook;

    // Connect notebook signals
    g_signal_connect(notebook, "page-added", G_CALLBACK(on_notebook_page_added), NULL);
    g_signal_connect(notebook, "page-removed", G_CALLBACK(on_notebook_page_removed), NULL);
    g_signal_connect(notebook, "switch-page", G_CALLBACK(on_notebook_switch_page), NULL);

    // Add gesture for dragging from empty space
    GtkGesture* gesture = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 1);  // Listen to primary button (left click)
    g_signal_connect(gesture, "drag-begin", G_CALLBACK(on_notebook_drag_begin), notebook);
    gtk_widget_add_controller(GTK_WIDGET(notebook), GTK_EVENT_CONTROLLER(gesture));

    // Create action widget (box) for window controls
    GtkWidget* action_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_valign(action_box, GTK_ALIGN_CENTER);

    // Minimize
    GtkWidget* btn_min = gtk_button_new_from_icon_name("window-minimize-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(btn_min), FALSE);
    g_signal_connect(btn_min, "clicked", G_CALLBACK(on_minimize_clicked), win);
    gtk_box_append(GTK_BOX(action_box), btn_min);

    // Maximize
    GtkWidget* btn_max = gtk_button_new_from_icon_name("window-maximize-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(btn_max), FALSE);
    g_signal_connect(btn_max, "clicked", G_CALLBACK(on_maximize_clicked), win);
    gtk_box_append(GTK_BOX(action_box), btn_max);

    // Close
    GtkWidget* btn_close = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(btn_close), FALSE);
    g_signal_connect(btn_close, "clicked", G_CALLBACK(on_close_window_clicked), win);
    gtk_box_append(GTK_BOX(action_box), btn_close);

    gtk_widget_set_visible(action_box, TRUE);
    gtk_notebook_set_action_widget(notebook, action_box, GTK_PACK_END);

    // Set notebook as window child (no wrapper)
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
