#define _POSIX_C_SOURCE 200809L

#include <gtk/gtk.h>
#include <vte/vte.h>
#include <pwd.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static gboolean debug_enabled = FALSE;
static GPid g_child_pid = -1;

#define DPRINT(...)               \
    do {                          \
        if (debug_enabled)        \
            g_print(__VA_ARGS__); \
    } while (0)

static uid_t get_process_euid(GPid pid) {
    if (pid < 1)
        return (uid_t)-1;
    gchar* path = g_strdup_printf("/proc/%d/status", (int)pid);
    FILE* f = fopen(path, "r");
    uid_t euid = (uid_t)-1;

    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (g_str_has_prefix(line, "Uid:")) {
                unsigned ruid_i, euid_i_, suid_i, fsuid_i;
                if (sscanf(line, "Uid:\t%u\t%u\t%u\t%u", &ruid_i, &euid_i_, &suid_i, &fsuid_i) == 4) {
                    euid = (uid_t)euid_i_;
                    break;
                }
            }
        }
        fclose(f);
    }
    else {
        DPRINT("Could not open %s\n", path);
    }
    g_free(path);
    return euid;
}

static gchar* get_hostname_stripped(void) {
    char hostname[256] = {0};
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        char* dot = strchr(hostname, '.');
        if (dot)
            *dot = '\0';
        return g_strdup(hostname);
    }
    return g_strdup("unknown");
}

static void on_vte_title_changed(VteTerminal* term, GParamSpec* p, gpointer data) {
    GtkWindow* win = GTK_WINDOW(data);
    gchar* title = NULL;
    g_object_get(term, "window-title", &title, NULL);
    if (title && *title) {
        DPRINT("on_vte_title_changed: %s\n", title);
        gtk_window_set_title(win, title);
    }
    g_free(title);
}

static void on_working_directory_changed(GObject* obj, GParamSpec* p, gpointer data) {
    GtkWindow* win = GTK_WINDOW(data);
    gchar* uri = NULL;
    g_object_get(obj, "current-directory-uri", &uri, NULL);
    if (!uri) {
        DPRINT("No URI.\n");
        return;
    }
    gchar* cwd = g_filename_from_uri(uri, NULL, NULL);
    g_free(uri);
    if (!cwd) {
        DPRINT("g_filename_from_uri() failed.\n");
        return;
    }
    uid_t euid = get_process_euid(g_child_pid);
    struct passwd* pw = (euid != (uid_t)-1) ? getpwuid(euid) : NULL;
    const gchar* user = (pw && pw->pw_name) ? pw->pw_name : "unknown";
    gchar* host = get_hostname_stripped();
    gchar* title = g_strdup_printf("%s@%s: %s", user, host, cwd);

    DPRINT("on_working_directory_changed: '%s'\n", title);
    gtk_window_set_title(win, title);
    g_free(title);
    g_free(host);
    g_free(cwd);
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

static gboolean on_terminal_key_pressed(GtkEventControllerKey* ctl,
                                        guint keyval,
                                        guint code,
                                        GdkModifierType st,
                                        gpointer data) {
    VteTerminal* term = VTE_TERMINAL(data);
    if ((st & GDK_CONTROL_MASK) && (st & GDK_SHIFT_MASK)) {
        switch (keyval) {
            case GDK_KEY_C:
                vte_terminal_copy_clipboard_format(term, VTE_FORMAT_TEXT);
                return TRUE;
            case GDK_KEY_V:
                vte_terminal_paste_clipboard(term);
                return TRUE;
            case GDK_KEY_A:
                vte_terminal_select_all(term);
                vte_terminal_copy_clipboard_format(term, VTE_FORMAT_TEXT);
                return TRUE;
        }
    }
    return FALSE;
}

static void on_child_exited(VteTerminal* term, gint status, gpointer data) {
    if (debug_enabled)
        g_print("Child exited, status=%d\n", status);
    g_application_quit(G_APPLICATION(data));
}

static void on_app_activate(GApplication* app, gpointer data) {
    DPRINT("on_app_activate\n");
    GtkWidget* win = gtk_application_window_new(GTK_APPLICATION(app));
    gtk_window_set_title(GTK_WINDOW(win), "1term");
    gtk_window_set_default_size(GTK_WINDOW(win), 1200, 500);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_window_set_child(GTK_WINDOW(win), scroll);

    GtkWidget* term = vte_terminal_new();
    gtk_window_set_icon_name(GTK_WINDOW(win), "1term");

    PangoFontDescription* font = pango_font_description_from_string("Monospace 11");
    vte_terminal_set_font(VTE_TERMINAL(term), font);
    pango_font_description_free(font);

    vte_terminal_set_scrollback_lines(VTE_TERMINAL(term), 100000);
    vte_terminal_set_scroll_on_output(VTE_TERMINAL(term), TRUE);
    vte_terminal_set_scroll_on_keystroke(VTE_TERMINAL(term), TRUE);
    vte_terminal_set_mouse_autohide(VTE_TERMINAL(term), TRUE);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), term);

    const char* shell = vte_get_user_shell();
    char* argv[] = {(char*)shell, NULL};

    vte_terminal_spawn_async(VTE_TERMINAL(term), VTE_PTY_DEFAULT, NULL, argv, NULL, (GSpawnFlags)0, NULL, NULL, NULL,
                             -1, NULL, (VteTerminalSpawnAsyncCallback)spawn_cb, NULL);

    GtkEventController* keyctl = gtk_event_controller_key_new();
    g_signal_connect(keyctl, "key-pressed", G_CALLBACK(on_terminal_key_pressed), term);
    gtk_widget_add_controller(term, keyctl);

    g_signal_connect(term, "notify::current-directory-uri", G_CALLBACK(on_working_directory_changed), win);
    g_signal_connect(term, "notify::window-title", G_CALLBACK(on_vte_title_changed), win);

    g_signal_connect(term, "child-exited", G_CALLBACK(on_child_exited), app);

    gtk_window_present(GTK_WINDOW(win));
}

static void print_help(const char* prog) {
    g_print("Usage: %s [OPTIONS]\n", prog);
    g_print("  -D, --debug             Enable debug messages\n");
    g_print("  -h, -H, --help          Show this help\n");
}

static int handle_local_options(GApplication* app, GVariantDict* opts, gpointer data) {
    if (g_variant_dict_contains(opts, "help")) {
        print_help(g_get_prgname());
        return 0;
    }
    return -1;
}

int main(int argc, char** argv) {
    GtkApplication* app = gtk_application_new("org.example.vtegtk4", G_APPLICATION_DEFAULT_FLAGS);
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
