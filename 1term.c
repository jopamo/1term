#include <gtk/gtk.h>
#include <vte/vte.h>

/*
 * 1term: lightweight gtk4 terminal using VTE
 *
 */

static gboolean on_terminal_key_pressed(GtkEventControllerKey* controller,
                                        guint keyval,
                                        guint keycode,
                                        GdkModifierType state,
                                        gpointer user_data) {
    VteTerminal* terminal = VTE_TERMINAL(user_data);

    /* Check if Ctrl+Shift is pressed */
    if ((state & GDK_CONTROL_MASK) && (state & GDK_SHIFT_MASK)) {
        switch (keyval) {
            case GDK_KEY_C:
                vte_terminal_copy_clipboard_format(terminal, VTE_FORMAT_TEXT);
                return TRUE; /* Event handled */
            case GDK_KEY_V:
                vte_terminal_paste_clipboard(terminal);
                return TRUE; /* Event handled */
            case GDK_KEY_A:  /* Ctrl+Shift+A to copy the whole scrollback */
                /* Select all the text in the terminal */
                vte_terminal_select_all(terminal);
                vte_terminal_copy_clipboard_format(terminal, VTE_FORMAT_TEXT);
                return TRUE; /* Event handled */
            default:
                break;
        }
    }
    return FALSE; /* Propagate event further if not handled */
}

static void on_window_resize(GtkWidget* widget, GdkRectangle* allocation, gpointer user_data) {
    /* Optional: Handle terminal resizing here */
    g_print("Window resized: %d x %d\n", allocation->width, allocation->height);
}

static void on_app_activate(GApplication* app, gpointer user_data) {
    /* Create a top-level window for our app */
    GtkWidget* window = gtk_application_window_new(GTK_APPLICATION(app));
    gtk_window_set_title(GTK_WINDOW(window), "1term");
    gtk_window_set_default_size(GTK_WINDOW(window), 1200, 500);

    /* Add 10% transparency to the window */
    gtk_widget_set_opacity(window, 0.9);  // 10% transparency

    /* Create a scrolled window so we can scroll back in terminal history */
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_window_set_child(GTK_WINDOW(window), scroll);

    /* Create a new VTE terminal widget */
    GtkWidget* terminal = vte_terminal_new();

    /* Set the window icon */
    gtk_window_set_icon_name(GTK_WINDOW(window), "1term");

    /* Set the font for the terminal */
    PangoFontDescription* font_desc = pango_font_description_from_string("Monospace 11");
    vte_terminal_set_font(VTE_TERMINAL(terminal), font_desc);
    pango_font_description_free(font_desc);

    /* Increase scrollback buffer size to 100,000 lines */
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(terminal), 100000);
    vte_terminal_set_scroll_on_output(VTE_TERMINAL(terminal), TRUE);
    vte_terminal_set_scroll_on_keystroke(VTE_TERMINAL(terminal), TRUE);

    /* Enable mouse scrolling */
    vte_terminal_set_mouse_autohide(VTE_TERMINAL(terminal), TRUE); /* Auto-hide mouse when not interacting */

    /* Add the terminal widget to the scrolled window */
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), terminal);

    /* Spawn the user's default shell inside the terminal */
    const char* default_shell = vte_get_user_shell();
    char* argv[] = {(char*)default_shell, NULL};

    vte_terminal_spawn_async(VTE_TERMINAL(terminal), VTE_PTY_DEFAULT, /* VtePtyFlags */
                             NULL,                                    /* working directory, or NULL = current */
                             argv,                                    /* argv for child */
                             NULL,                                    /* child environment, or NULL = inherit */
                             (GSpawnFlags)0,                          /* flags from GSpawnFlags */
                             NULL,                                    /* child_setup */
                             NULL,                                    /* child_setup_data */
                             NULL,                                    /* GCancellable */
                             -1,                                      /* timeout for spawning, or -1 = no timeout */
                             NULL,                                    /* GCancellable */
                             NULL,                                    /* async-ready-callback, or NULL */
                             NULL                                     /* user_data for callback */
    );

    /*
     * Create and attach a key event controller to the terminal
     * for our custom copy/paste shortcuts.
     */
    GtkEventController* key_controller = gtk_event_controller_key_new();
    g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_terminal_key_pressed), terminal);
    gtk_widget_add_controller(terminal, key_controller);

    /* Connect to window resize signal */
    g_signal_connect(window, "notify::allocation", G_CALLBACK(on_window_resize), NULL);

    /* Finally, present the window */
    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char** argv) {
    /* Create our application */
    GtkApplication* app;
    int status;

    app = gtk_application_new("org.example.vtegtk4", G_APPLICATION_DEFAULT_FLAGS);

    /* Connect to the "activate" signal, emitted on g_application_run() */
    g_signal_connect(app, "activate", G_CALLBACK(on_app_activate), NULL);

    /* Run the application (enters the main event loop) */
    status = g_application_run(G_APPLICATION(app), argc, argv);

    g_object_unref(app);
    return status;
}
