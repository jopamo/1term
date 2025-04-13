#define _POSIX_C_SOURCE 200809L

#include <gtk/gtk.h>
#include <vte/vte.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <zstd.h>

static gboolean debug_enabled = FALSE;

static void compress_entire_scrollback(VteTerminal* term);

#define DPRINT(fmt, ...)                            \
    do {                                            \
        if (debug_enabled)                          \
            g_print("[DEBUG] " fmt, ##__VA_ARGS__); \
    } while (0)

static GPid g_child_pid = -1;

static void compress_text_zstd(const char* text, const char* outFilename, int compressionLevel) {
    if (!text || !*text) {
        g_printerr("Nothing to compress.\n");
        return;
    }

    size_t srcSize = strlen(text);
    size_t bound = ZSTD_compressBound(srcSize);

    void* compressedData = malloc(bound);
    if (!compressedData) {
        g_printerr("Error: cannot allocate memory for compression.\n");
        return;
    }

    size_t cSize = ZSTD_compress(compressedData, bound, text, srcSize, compressionLevel);
    if (ZSTD_isError(cSize)) {
        g_printerr("ZSTD_compress error: %s\n", ZSTD_getErrorName(cSize));
        free(compressedData);
        return;
    }

    FILE* fp = fopen(outFilename, "wb");
    if (!fp) {
        g_printerr("Error opening %s for writing: %s\n", outFilename, strerror(errno));
        free(compressedData);
        return;
    }

    size_t written = fwrite(compressedData, 1, cSize, fp);
    fclose(fp);
    free(compressedData);

    if (written != cSize) {
        g_printerr("Error writing all compressed bytes to %s.\n", outFilename);
        return;
    }

    g_print("Successfully compressed scrollback to '%s' (%zu bytes)\n", outFilename, cSize);
}

static void on_vte_title_changed(VteTerminal* term, GParamSpec* pspec, gpointer user_data) {
    GtkWindow* win = GTK_WINDOW(user_data);
    gchar* title = NULL;

    g_object_get(term, "window-title", &title, NULL);
    if (title && *title) {
        const gchar* current_title = gtk_window_get_title(win);
        if (g_strcmp0(title, current_title) != 0) {
            DPRINT("Title changed: %s\n", title);
            gtk_window_set_title(win, title);
        }
    }
    g_free(title);
}

static void on_working_directory_changed(GObject* obj, GParamSpec* pspec, gpointer user_data) {
    GtkWindow* win = GTK_WINDOW(user_data);
    gchar* uri = NULL;
    g_object_get(obj, "current-directory-uri", &uri, NULL);
    if (!uri) {
        DPRINT("No current-directory-uri found.\n");
        return;
    }

    static gchar* last_cwd = NULL;
    gchar* cwd = g_filename_from_uri(uri, NULL, NULL);
    g_free(uri);

    if (cwd && g_strcmp0(cwd, last_cwd) != 0) {
        gchar* title = g_strdup_printf("Terminal: %s", cwd);
        DPRINT("Working directory changed => %s\n", title);
        gtk_window_set_title(win, title);
        g_free(title);

        g_free(last_cwd);
        last_cwd = cwd;
    }
    else {
        g_free(cwd);
    }
}

static void spawn_cb(VteTerminal* term, GPid pid, GError* err, gpointer user_data) {
    if (err) {
        g_printerr("Error spawning child: %s\n", err->message);
        g_clear_error(&err);
        return;
    }
    g_child_pid = pid;
    DPRINT("Shell spawned, PID=%d\n", (int)pid);
}

static int ensure_log_directory(void) {
    const char* home = g_get_home_dir();
    if (!home || !*home) {
        g_printerr("Error: cannot determine home directory.\n");
        return -1;
    }

    gchar* logsdir = g_build_filename(home, ".1term", "logs", NULL);
    if (g_mkdir_with_parents(logsdir, 0700) != 0) {
        g_printerr("Could not create directory %s: %s\n", logsdir, strerror(errno));
        g_free(logsdir);
        return -1;
    }
    g_free(logsdir);
    return 0;
}

static gboolean on_terminal_key_pressed(GtkEventControllerKey* ctl,
                                        guint keyval,
                                        guint keycode,
                                        GdkModifierType state,
                                        gpointer user_data) {
    VteTerminal* term = VTE_TERMINAL(user_data);

    if ((state & GDK_CONTROL_MASK) && (state & GDK_SHIFT_MASK)) {
        switch (keyval) {
            case GDK_KEY_C:
                DPRINT("Ctrl+Shift+C => copy\n");
                vte_terminal_copy_clipboard_format(term, VTE_FORMAT_TEXT);
                return TRUE;

            case GDK_KEY_V:
                DPRINT("Ctrl+Shift+V => paste\n");
                vte_terminal_paste_clipboard(term);
                return TRUE;

            case GDK_KEY_A:
                DPRINT("Ctrl+Shift+A => select all + copy\n");
                vte_terminal_select_all(term);
                vte_terminal_copy_clipboard_format(term, VTE_FORMAT_TEXT);
                return TRUE;

            case GDK_KEY_B:
                DPRINT("Ctrl+Shift+B => compress entire scrollback\n");
                compress_entire_scrollback(term);
                return TRUE;

            default:
                break;
        }
    }
    return FALSE;
}

static void compress_entire_scrollback(VteTerminal* term) {
    DPRINT("Entering compress_entire_scrollback()\n");

    if (ensure_log_directory() != 0) {
        DPRINT("ensure_log_directory() failed, aborting.\n");
        return;
    }

    DPRINT("Calling vte_terminal_get_text_range_format()...\n");
    gsize text_len = 0;
    gchar* all_text = vte_terminal_get_text_range_format(term, VTE_FORMAT_TEXT, 0, 0, -1, -1, &text_len);

    if (!all_text || !*all_text) {
        DPRINT("No text from get_text_range_format(), trying fallback...\n");
        g_free(all_text);

        all_text = vte_terminal_get_text(term, NULL, NULL, NULL);
        if (!all_text || !*all_text) {
            DPRINT("No text from fallback API either.\n");
            g_free(all_text);
            return;
        }
    }

    DPRINT("Retrieved text of length=%zu\n", text_len);
    if (!*all_text) {
        DPRINT("All_text is empty (length=%zu). Nothing to compress.\n", text_len);
        g_free(all_text);
        return;
    }

    const char* home = g_get_home_dir();
    gchar* logsdir = g_build_filename(home, ".1term", "logs", NULL);

    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);

    char timestr[64];
    strftime(timestr, sizeof(timestr), "terminal_%Y%m%d_%H%M%S.logz", &tm_info);

    gchar* outFilename = g_build_filename(logsdir, timestr, NULL);

    DPRINT("Compressing text to '%s'...\n", outFilename);
    compress_text_zstd(all_text, outFilename, 19);

    g_free(all_text);
    g_free(outFilename);
    g_free(logsdir);

    DPRINT("Exiting compress_entire_scrollback()\n");
}

static void on_child_exited(VteTerminal* term, gint status, gpointer user_data) {
    if (debug_enabled) {
        g_print("Child exited, status=%d\n", status);
    }
    g_application_quit(G_APPLICATION(user_data));
}

static void print_help(const char* prog) {
    g_print("Usage: %s [OPTIONS]\n", prog);
    g_print("  -D, --debug          Enable debug messages\n");
    g_print("  -h, -H, --help       Show this help\n");
}

static int handle_local_options(GApplication* app, GVariantDict* opts, gpointer user_data) {
    if (g_variant_dict_contains(opts, "help")) {
        print_help(g_get_prgname());
        return 0;
    }
    return -1;
}

static void on_app_activate(GApplication* app, gpointer user_data) {
    GtkWidget* win = gtk_application_window_new(GTK_APPLICATION(app));
    gtk_window_set_title(GTK_WINDOW(win), "1term");
    gtk_window_set_default_size(GTK_WINDOW(win), 1200, 500);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_window_set_child(GTK_WINDOW(win), scroll);

    VteTerminal* term = VTE_TERMINAL(vte_terminal_new());
    gtk_window_set_icon_name(GTK_WINDOW(win), "1term");

    PangoFontDescription* font = pango_font_description_from_string("Monospace 9");
    vte_terminal_set_font(term, font);
    pango_font_description_free(font);

    vte_terminal_set_scrollback_lines(term, 100000);
    vte_terminal_set_scroll_on_output(term, FALSE);
    vte_terminal_set_scroll_on_keystroke(term, TRUE);
    vte_terminal_set_mouse_autohide(term, TRUE);
    vte_terminal_set_enable_fallback_scrolling(term, FALSE);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), GTK_WIDGET(term));

    const char* shell = vte_get_user_shell();
    if (!shell || shell[0] == '\0') {
        shell = "/bin/sh";
    }
    char* argv[] = {(char*)shell, NULL};

    vte_terminal_spawn_async(term, VTE_PTY_DEFAULT, NULL, argv, NULL, (GSpawnFlags)0, NULL, NULL, NULL, -1, NULL,
                             spawn_cb, NULL);

    GtkEventController* keyctl = gtk_event_controller_key_new();
    g_signal_connect(keyctl, "key-pressed", G_CALLBACK(on_terminal_key_pressed), term);
    gtk_widget_add_controller(GTK_WIDGET(term), keyctl);

    g_signal_connect(term, "notify::current-directory-uri", G_CALLBACK(on_working_directory_changed), win);
    g_signal_connect(term, "notify::window-title", G_CALLBACK(on_vte_title_changed), win);
    g_signal_connect(term, "child-exited", G_CALLBACK(on_child_exited), app);

    gtk_window_present(GTK_WINDOW(win));
}

int main(int argc, char** argv) {
    GtkApplication* app =
        gtk_application_new("org.example.vtegtk4", G_APPLICATION_DEFAULT_FLAGS | G_APPLICATION_NON_UNIQUE);

    static GOptionEntry entries[] = {
        {"debug", 'D', 0, G_OPTION_ARG_NONE, &debug_enabled, "Enable debug messages", NULL},
        {"help", 'h', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, NULL, "Show help", NULL},
        {NULL}};

    g_application_add_main_option_entries(G_APPLICATION(app), entries);
    g_signal_connect(app, "handle-local-options", G_CALLBACK(handle_local_options), NULL);
    g_signal_connect(app, "activate", G_CALLBACK(on_app_activate), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
