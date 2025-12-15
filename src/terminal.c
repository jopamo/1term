#include "terminal.h"
#include "window.h"
#include "clipboard.h"
#include "tab.h"

static void spawn_finished_cb(GObject* source_object, GAsyncResult* res, gpointer user_data) {
    VtePty* pty = VTE_PTY(source_object);
    VteTerminal* vt = VTE_TERMINAL(user_data);
    GPid child_pid = 0;
    GError* error = NULL;

    gboolean success = vte_pty_spawn_finish(pty, res, &child_pid, &error);
    if (!success || child_pid <= 0) {
        g_printerr("Error spawning shell: %s\n", (error ? error->message : "(unknown)"));
        g_clear_error(&error);
        return;
    }

    g_print("Spawned shell (PID=%d)\n", (int)child_pid);

    vte_terminal_watch_child(vt, child_pid);
}

void setup_background_color(VteTerminal* vt) {
    GdkRGBA bg = (GdkRGBA){0, 0, 0, transparency_enabled ? 0.95 : 1.0};
    vte_terminal_set_color_background(vt, &bg);
}

void vte_set_robust_word_chars(VteTerminal* vt) {
    // includes - . / _ ~ : @ % + # = , for paths, URLs, queries, kv pairs
    const char* exceptions = "-./_~:@%+#=,";
    vte_terminal_set_word_char_exceptions(vt, exceptions);
}

void setup_terminal(VteTerminal* vt) {
    PangoFontDescription* fd = pango_font_description_from_string("Monospace 9");
    vte_terminal_set_font(vt, fd);
    pango_font_description_free(fd);

    vte_terminal_set_scrollback_lines(vt, scrollback_enabled ? 100000 : 0);
    vte_terminal_set_scroll_on_output(vt, FALSE);
    vte_terminal_set_scroll_on_keystroke(vt, TRUE);
#if VTE_CHECK_VERSION(0, 78, 0)
    vte_terminal_set_enable_a11y(vt, FALSE);
#endif
    vte_terminal_set_enable_bidi(vt, FALSE);
    vte_terminal_set_enable_shaping(vt, FALSE);
    vte_terminal_set_enable_sixel(vt, FALSE);
    vte_terminal_set_enable_fallback_scrolling(vt, FALSE);
    vte_terminal_set_audible_bell(vt, FALSE);

    vte_set_robust_word_chars(vt);
}

void setup_key_events(VteTerminal* vt) {
    GtkEventController* keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), vt);
    gtk_widget_add_controller(GTK_WIDGET(vt), keys);
}

void on_selection_changed(VteTerminal* vt, gpointer user_data) {
    (void)user_data;

    if (!vte_terminal_get_has_selection(vt))
        return;

    gchar* sel = vte_terminal_get_text_selected(vt, VTE_FORMAT_TEXT);
    if (!sel)
        return;

    gsize len = strlen(sel);
    while (len && sel[len - 1] == ':') {
        sel[--len] = '\0';
    }

    GtkWidget* widget = GTK_WIDGET(vt);
    GdkClipboard* cb = gtk_widget_get_clipboard(widget);
    gdk_clipboard_set_text(cb, sel);

    g_free(sel);
}

gboolean on_key_pressed(GtkEventControllerKey* ctrl,
                        guint keyval,
                        guint keycode,
                        GdkModifierType state,
                        gpointer user_data) {
    VteTerminal* vt = VTE_TERMINAL(user_data);

    if ((state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) == (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) {
        switch (keyval) {
            case GDK_KEY_C:
                vte_terminal_copy_clipboard_format(vt, VTE_FORMAT_TEXT);
                return TRUE;
            case GDK_KEY_V:
                vte_terminal_paste_clipboard(vt);
                return TRUE;
            case GDK_KEY_A:
                vte_terminal_select_all(vt);
                vte_terminal_copy_clipboard_format(vt, VTE_FORMAT_TEXT);
                return TRUE;
            case GDK_KEY_B:
                // compress via select-all -> copy -> read clipboard
                compress_scrollback_via_clipboard_async(vt);
                return TRUE;

            case GDK_KEY_T: {
                transparency_enabled = !transparency_enabled;
                update_transparency_all();
                return TRUE;
            }

            case GDK_KEY_S: {
                scrollback_enabled = !scrollback_enabled;
                update_scrollback_all();
                return TRUE;
            }
            case GDK_KEY_N: {
                GtkNotebook* notebook = get_notebook_from_terminal(vt);
                if (notebook) {
                    add_tab(notebook);
                }
                return TRUE;
            }
            case GDK_KEY_W: {
                GtkNotebook* notebook = get_notebook_from_terminal(vt);
                if (notebook) {
                    close_current_tab(notebook);
                }
                return TRUE;
            }
        }
    }
    return FALSE;
}

void setup_pty_and_shell(VteTerminal* vt) {
    const char* shell = "/bin/bash";

    char* argv[] = {(char*)shell, "-i", NULL};
    char** envp = g_environ_setenv(g_get_environ(), "TERM", "xterm-256color", TRUE);
    envp = g_environ_setenv(envp, "PS1", "\\u@\\h:\\w\\$ ", TRUE);

    GError* err = NULL;
    VtePty* pty = vte_pty_new_sync(VTE_PTY_DEFAULT, NULL, &err);
    if (!pty) {
        g_printerr("Failed to create PTY: %s\n", err ? err->message : "(unknown error)");
        g_clear_error(&err);
        g_strfreev(envp);
        return;
    }

    vte_terminal_set_pty(vt, pty);
    vte_terminal_set_input_enabled(vt, TRUE);

    vte_pty_spawn_async(pty, NULL, argv, envp, (GSpawnFlags)0, NULL, NULL, NULL, -1, NULL, spawn_finished_cb, vt);

    g_strfreev(envp);
    g_object_unref(pty);
}