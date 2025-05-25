#define _POSIX_C_SOURCE 200809L

#include <gtk/gtk.h>
#include <vte/vte.h>
#include <zstd.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef VTE_CHECK_VERSION
#define VTE_CHECK_VERSION(maj, min, mic)                                                         \
    ((VTE_MAJOR_VERSION > (maj)) || (VTE_MAJOR_VERSION == (maj) && VTE_MINOR_VERSION > (min)) || \
     (VTE_MAJOR_VERSION == (maj) && VTE_MINOR_VERSION == (min) && VTE_MICRO_VERSION >= (mic)))
#endif

#if !VTE_CHECK_VERSION(0, 78, 0)
#define TERM_SIGNAL_TITLE "window-title-changed"
#define TERM_SIGNAL_CWD "current-directory-uri-changed"
#else
#define TERM_SIGNAL_TITLE "termprop-changed"
#define TERM_SIGNAL_CWD "termprop-changed"
#endif

static void create_window(GtkApplication* app);

static GThreadPool* compress_pool = NULL;

typedef struct {
    gchar* text;
    gchar* path;
    int lvl;
} CompressJob;

static gboolean transparency_enabled = TRUE;
static gboolean scrollback_enabled = TRUE;

static void compress_worker(gpointer data, gpointer unused) {
    CompressJob* j = data;
    const size_t src_len = strlen(j->text);
    const size_t cap = ZSTD_compressBound(src_len);

    void* buf = malloc(cap);
    if (!buf)
        goto done;

    size_t compressed_size = ZSTD_compress(buf, cap, j->text, src_len, j->lvl);
    if (ZSTD_isError(compressed_size)) {
        g_printerr("zstd: %s\n", ZSTD_getErrorName(compressed_size));
        free(buf);
        goto done;
    }

    FILE* f = fopen(j->path, "wb");
    if (!f) {
        g_printerr("%s: %s\n", j->path, g_strerror(errno));
        free(buf);
        goto done;
    }

    fwrite(buf, 1, compressed_size, f);
    fclose(f);
    free(buf);

    g_print("Scroll-back compressed → %s (%zu B)\n", j->path, compressed_size);

done:
    g_free(j->text);
    g_free(j->path);
    g_free(j);
}

static void compress_scrollback_async(VteTerminal* vt) {
    gsize text_len = 0;

    gchar* txt = vte_terminal_get_text_range_format(vt, VTE_FORMAT_TEXT, 0, 0, -1, -1, &text_len);
    if (!txt || !*txt) {
        g_free(txt);
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
        txt = vte_terminal_get_text(vt, NULL, NULL, NULL);
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
        if (!txt || !*txt) {
            g_free(txt);
            return;
        }
    }

    gchar* dir = g_build_filename(g_get_home_dir(), ".1term", "logs", NULL);
    if (g_mkdir_with_parents(dir, 0700) != 0) {
        g_printerr("mkdir %s: %s\n", dir, g_strerror(errno));
        g_free(txt);
        g_free(dir);
        return;
    }

    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);

    char timestr[64];
    strftime(timestr, sizeof(timestr), "terminal_%Y%m%d_%H%M%S.logz", &tm_info);

    gchar* path = g_build_filename(dir, timestr, NULL);
    g_free(dir);

    CompressJob* job = g_new0(CompressJob, 1);
    job->text = txt;
    job->path = path;
    job->lvl = 19;

    if (!compress_pool) {
        compress_pool = g_thread_pool_new(compress_worker, NULL, g_get_num_processors(), FALSE, NULL);
    }
    g_thread_pool_push(compress_pool, job, NULL);
}

static gboolean on_key_pressed(GtkEventControllerKey* ctrl,
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
                compress_scrollback_async(vt);
                return TRUE;

            case GDK_KEY_T: {
                transparency_enabled = !transparency_enabled;
                GdkRGBA newbg;
                newbg.red = 0.0;
                newbg.green = 0.0;
                newbg.blue = 0.0;

                newbg.alpha = transparency_enabled ? 0.8 : 1.0;
                vte_terminal_set_color_background(vt, &newbg);
                return TRUE;
            }

            case GDK_KEY_S: {
                scrollback_enabled = !scrollback_enabled;

                vte_terminal_set_scrollback_lines(vt, scrollback_enabled ? 100000 : 0);
                return TRUE;
            }
        }
    }
    return FALSE;
}

static void update_title(VteTerminal* vt, GtkWindow* win) {
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

    if (shell_title && *shell_title) {
        gtk_window_set_title(win, shell_title);
        return;
    }

    if (cwd_uri) {
        const char* scheme = g_uri_get_scheme(cwd_uri);
        if (scheme && g_str_equal(scheme, "file")) {
            const char* path = g_uri_get_path(cwd_uri);
            if (path && *path) {
                g_autofree char* s = g_strdup_printf("%s@%s", g_get_user_name(), path);
                gtk_window_set_title(win, s);
                return;
            }
        }
    }

    g_autofree char* s = g_strdup_printf("%s@?", g_get_user_name());
    gtk_window_set_title(win, s);
}

static void title_sig_cb(VteTerminal* vt, guint prop_id, gpointer user_data) {
    (void)prop_id;
    update_title(vt, GTK_WINDOW(user_data));
}

static void on_child_exit(VteTerminal* vt, int status, GtkWindow* win) {
    g_print("Shell exited (status=%d). Closing window.\n", status);
    gtk_window_destroy(win);
}

G_DECLARE_FINAL_TYPE(MyWindow, my_window, MY, WINDOW, GtkApplicationWindow)
struct _MyWindow {
    GtkApplicationWindow parent_instance;
};
G_DEFINE_TYPE(MyWindow, my_window, GTK_TYPE_APPLICATION_WINDOW)

static void my_window_css_changed(GtkWidget* w, GtkCssStyleChange* c) {
    GTK_WIDGET_CLASS(my_window_parent_class)->css_changed(w, c);
}

static void my_window_class_init(MyWindowClass* klass) {
    GTK_WIDGET_CLASS(klass)->css_changed = my_window_css_changed;
}

static void my_window_init(MyWindow* self) {}

static GtkWidget* my_window_new(GtkApplication* app) {
    return g_object_new(my_window_get_type(), "application", app, NULL);
}

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
    static const char css[] =
        "window{background-color:rgba(0,0,0,0);} "
        "vte-terminal{background-color:rgba(0,0,0,0);}";

    GtkCssProvider* prov = gtk_css_provider_new();
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    gtk_css_provider_load_from_data(prov, css, -1);
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
    gtk_style_context_add_provider_for_display(gtk_widget_get_display(win), GTK_STYLE_PROVIDER(prov),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(prov);
}

static void setup_terminal(VteTerminal* vt) {
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
}

static void setup_background_color(VteTerminal* vt) {
    GdkRGBA bg = {0, 0, 0, transparency_enabled ? 0.95 : 1.0};
    vte_terminal_set_color_background(vt, &bg);
}

static void setup_pty_and_shell(VteTerminal* vt) {
    const char* shell = vte_get_user_shell();
    if (!shell || !*shell)
        shell = "/bin/sh";

    char* argv[] = {(char*)shell, NULL};
    char** envp = g_environ_setenv(g_get_environ(), "TERM", "xterm-256color", TRUE);

    GError* err = NULL;
    VtePty* pty = vte_pty_new_sync(VTE_PTY_DEFAULT, NULL, &err);
    if (!pty) {
        g_printerr("Failed to create PTY: %s\n", err ? err->message : "(unknown error)");
        g_clear_error(&err);
        g_strfreev(envp);
        return;
    }

    vte_terminal_set_pty(vt, pty);
    vte_pty_spawn_async(pty, NULL, argv, envp, (GSpawnFlags)0, NULL, NULL, NULL, -1, NULL, spawn_finished_cb, vt);

    g_strfreev(envp);
    g_object_unref(pty);
}

static void setup_key_events(VteTerminal* vt) {
    GtkEventController* keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), vt);
    gtk_widget_add_controller(GTK_WIDGET(vt), keys);
}

static void create_window(GtkApplication* app) {
    static int window_count = 0;

    GtkWidget* win = my_window_new(app);
    VteTerminal* vt = VTE_TERMINAL(vte_terminal_new());

    setup_window_size(win, window_count);
    window_count++;

    gtk_window_set_icon_name(GTK_WINDOW(win), "1term");

    apply_css(win);

    GtkWidget* scr = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scr), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scr), GTK_WIDGET(vt));
    gtk_window_set_child(GTK_WINDOW(win), scr);

    setup_terminal(vt);
    setup_background_color(vt);

    setup_pty_and_shell(vt);

    g_signal_connect(vt, "child-exited", G_CALLBACK(on_child_exit), win);
#if VTE_CHECK_VERSION(0, 78, 0)
    g_signal_connect(vt, "termprop-changed", G_CALLBACK(title_sig_cb), win);
#else
    g_signal_connect(vt, "window-title-changed", G_CALLBACK(title_sig_cb), win);
    g_signal_connect(vt, "current-directory-uri-changed", G_CALLBACK(title_sig_cb), win);
#endif

    setup_key_events(vt);

    gtk_window_present(GTK_WINDOW(win));
    update_title(vt, GTK_WINDOW(win));
}

static void new_window_action(GSimpleAction* a, GVariant* p, gpointer user_data) {
    create_window(GTK_APPLICATION(user_data));
}

static void app_activate(GApplication* gapp, gpointer unused) {
    create_window(GTK_APPLICATION(gapp));
}

static void free_compress_pool(void) {
    if (compress_pool) {
        g_thread_pool_free(compress_pool, TRUE, TRUE);
        compress_pool = NULL;
    }
}

int main(int argc, char** argv) {
    atexit(free_compress_pool);

    GtkApplication* app = gtk_application_new("com.example.oneterm", G_APPLICATION_NON_UNIQUE);

    g_signal_connect(app, "activate", G_CALLBACK(app_activate), NULL);

    GSimpleAction* act = g_simple_action_new("new-window", NULL);
    g_signal_connect(act, "activate", G_CALLBACK(new_window_action), app);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(act));

    int status = g_application_run(G_APPLICATION(app), argc, argv);

    g_object_unref(app);

    return status;
}
