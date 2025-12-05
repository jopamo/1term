#define _POSIX_C_SOURCE 200809L

#include <glib-2.0/glib/gstdio.h>
#include <gtk/gtk.h>
#include <vte/vte.h>
#include <zstd.h>

#include <errno.h>
#include <fcntl.h>
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
     (VTE_MAJOR_VERSION == (maj) && VTE_MICRO_VERSION >= (mic)))
#endif

#if !VTE_CHECK_VERSION(0, 78, 0)
#define TERM_SIGNAL_TITLE "window-title-changed"
#define TERM_SIGNAL_CWD "current-directory-uri-changed"
#else
#define TERM_SIGNAL_TITLE "termprop-changed"
#define TERM_SIGNAL_CWD "termprop-changed"
#endif

static void create_window(GtkApplication* app);
static VteTerminal* add_tab(GtkNotebook* notebook);
static void close_current_tab(GtkNotebook* notebook);
static void on_tab_close_clicked(GtkButton* btn, gpointer user_data);
static void on_child_exit_tab(VteTerminal* vt, int status, GtkNotebook* notebook);
static void update_tab_title(VteTerminal* vt, GtkNotebook* notebook);
static void on_notebook_page_added(GtkNotebook* notebook, GtkWidget* child, guint page_num, gpointer user_data);
static void on_notebook_page_removed(GtkNotebook* notebook, GtkWidget* child, guint page_num, gpointer user_data);
static void on_notebook_switch_page(GtkNotebook* notebook, GtkWidget* page, guint page_num, gpointer user_data);
static GtkNotebook* get_notebook_from_terminal(VteTerminal* vt);
static void title_tab_sig_cb_new(VteTerminal* vt, guint prop_id, gpointer user_data);
static void update_transparency_for_notebook(GtkNotebook* notebook);
static void update_transparency_all(void);
static void update_css_transparency(void);
static void update_scrollback_for_notebook(GtkNotebook* notebook);
static void update_scrollback_all(void);
#if !VTE_CHECK_VERSION(0, 78, 0)
static void title_tab_sig_cb_old(VteTerminal* vt, gpointer user_data);
#endif

static GThreadPool* compress_pool = NULL;

typedef struct {
    gchar* text;
    gchar* path;
    int lvl;
} CompressJob;

static gboolean transparency_enabled = TRUE;
static gboolean scrollback_enabled = TRUE;
static GtkCssProvider* css_provider = NULL;

static void compress_worker(gpointer data, gpointer unused) {
    CompressJob* j = data;

    ZSTD_CStream* zs = ZSTD_createCStream();
    if (!zs)
        goto done;

    size_t zr = ZSTD_initCStream(zs, j->lvl);
    if (ZSTD_isError(zr)) {
        g_printerr("zstd init: %s\n", ZSTD_getErrorName(zr));
        ZSTD_freeCStream(zs);
        goto done;
    }

    gchar* dir = g_path_get_dirname(j->path);
    if (g_mkdir_with_parents(dir, 0700) != 0) {
        g_printerr("mkdir %s: %s\n", dir, g_strerror(errno));
        g_free(dir);
        ZSTD_freeCStream(zs);
        goto done;
    }
    g_free(dir);

    gchar* tmpl = g_strdup_printf("%s.XXXXXX", j->path);
    int tfd = g_mkstemp_full(tmpl, O_WRONLY | O_CLOEXEC, 0600);
    if (tfd < 0) {
        g_printerr("mkstemp %s: %s\n", tmpl, g_strerror(errno));
        g_free(tmpl);
        ZSTD_freeCStream(zs);
        goto done;
    }
    FILE* tf = fdopen(tfd, "wb");
    if (!tf) {
        g_printerr("fdopen: %s\n", g_strerror(errno));
        close(tfd);
        g_unlink(tmpl);
        g_free(tmpl);
        ZSTD_freeCStream(zs);
        goto done;
    }

    // zstd streaming loop
    const size_t in_total = strlen(j->text);
    ZSTD_inBuffer in = (ZSTD_inBuffer){j->text, in_total, 0};
    unsigned char outbuf[1 << 15];  // 32 KiB chunk
    ZSTD_outBuffer out = (ZSTD_outBuffer){outbuf, sizeof(outbuf), 0};

    while (in.pos < in.size) {
        out.pos = 0;
        size_t ret = ZSTD_compressStream(zs, &out, &in);
        if (ZSTD_isError(ret)) {
            g_printerr("zstd write: %s\n", ZSTD_getErrorName(ret));
            fclose(tf);
            g_unlink(tmpl);
            g_free(tmpl);
            ZSTD_freeCStream(zs);
            goto done;
        }
        if (out.pos && fwrite(out.dst, 1, out.pos, tf) != out.pos) {
            g_printerr("fwrite: %s\n", g_strerror(errno));
            fclose(tf);
            g_unlink(tmpl);
            g_free(tmpl);
            ZSTD_freeCStream(zs);
            goto done;
        }
    }

    // flush remaining
    for (;;) {
        out.pos = 0;
        size_t ret = ZSTD_endStream(zs, &out);
        if (ZSTD_isError(ret)) {
            g_printerr("zstd end: %s\n", ZSTD_getErrorName(ret));
            fclose(tf);
            g_unlink(tmpl);
            g_free(tmpl);
            ZSTD_freeCStream(zs);
            goto done;
        }
        if (out.pos && fwrite(out.dst, 1, out.pos, tf) != out.pos) {
            g_printerr("fwrite: %s\n", g_strerror(errno));
            fclose(tf);
            g_unlink(tmpl);
            g_free(tmpl);
            ZSTD_freeCStream(zs);
            goto done;
        }
        if (ret == 0)
            break;
    }

    fflush(tf);
    fsync(fileno(tf));
    fclose(tf);

    if (g_rename(tmpl, j->path) != 0) {
        g_printerr("rename %s -> %s: %s\n", tmpl, j->path, g_strerror(errno));
        g_unlink(tmpl);
        g_free(tmpl);
        ZSTD_freeCStream(zs);
        goto done;
    }

    g_print("Scroll-back compressed \u2192 %s\n", j->path);

    g_free(tmpl);
    ZSTD_freeCStream(zs);

done:
    g_free(j->text);
    g_free(j->path);
    g_free(j);
}

// async clipboard -> compress callback
static void compress_clipboard_text_ready(GObject* source_object, GAsyncResult* res, gpointer user_data) {
    GdkClipboard* cb = GDK_CLIPBOARD(source_object);
    VteTerminal* vt = VTE_TERMINAL(user_data);
    GError* error = NULL;

    char* text = gdk_clipboard_read_text_finish(cb, res, &error);
    if (!text) {
        if (error) {
            g_printerr("clipboard read: %s\n", error->message);
            g_clear_error(&error);
        }
        vte_terminal_unselect_all(vt);
        g_object_unref(vt);
        return;
    }

    // build destination path under ~/.1term/logs
    gchar* dir = g_build_filename(g_get_home_dir(), ".1term", "logs", NULL);
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    char timestr[64];
    strftime(timestr, sizeof(timestr), "terminal_%Y%m%d_%H%M%S_%d.logz", &tm_info);
    gchar* path = g_build_filename(dir, timestr, NULL);
    g_free(dir);

    // dispatch compression to thread pool
    CompressJob* job = g_new0(CompressJob, 1);
    job->text = text;
    job->path = path;
    job->lvl = 15;

    if (!compress_pool) {
        compress_pool = g_thread_pool_new(compress_worker, NULL, g_get_num_processors(), FALSE, NULL);
    }
    g_thread_pool_push(compress_pool, job, NULL);

    // optional UX cleanup
    vte_terminal_unselect_all(vt);

    g_object_unref(vt);
}

// selects all, copies to clipboard, then reads text back and compresses it
static void compress_scrollback_via_clipboard_async(VteTerminal* vt) {
    GtkWidget* widget = GTK_WIDGET(vt);
    GdkClipboard* cb = gtk_widget_get_clipboard(widget);

    vte_terminal_select_all(vt);
    vte_terminal_copy_clipboard_format(vt, VTE_FORMAT_TEXT);

    g_object_ref(vt);

    gdk_clipboard_read_text_async(cb, NULL, compress_clipboard_text_ready, vt);
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

G_DECLARE_FINAL_TYPE(MyWindow, my_window, MY, WINDOW, GtkApplicationWindow)
struct _MyWindow {
    GtkApplicationWindow parent_instance;
    GtkNotebook* notebook;
};
G_DEFINE_TYPE(MyWindow, my_window, GTK_TYPE_APPLICATION_WINDOW)

static void my_window_css_changed(GtkWidget* w, GtkCssStyleChange* c) {
    GTK_WIDGET_CLASS(my_window_parent_class)->css_changed(w, c);
}

static void my_window_class_init(MyWindowClass* klass) {
    GTK_WIDGET_CLASS(klass)->css_changed = my_window_css_changed;
}

static void my_window_init(MyWindow* self) {
    self->notebook = NULL;
}

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
    GdkDisplay* display = gtk_widget_get_display(win);
    if (!css_provider) {
        css_provider = gtk_css_provider_new();
        gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(css_provider),
                                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    update_css_transparency();
}

static void update_css_transparency(void) {
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

static void vte_set_robust_word_chars(VteTerminal* vt) {
    // includes - . / _ ~ : @ % + # = , for paths, URLs, queries, kv pairs
    const char* exceptions = "-./_~:@%+#=,";

    vte_terminal_set_word_char_exceptions(vt, exceptions);
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

    vte_set_robust_word_chars(vt);
}

static void setup_background_color(VteTerminal* vt) {
    GdkRGBA bg = (GdkRGBA){0, 0, 0, transparency_enabled ? 0.8 : 1.0};
    vte_terminal_set_color_background(vt, &bg);
}

static void update_transparency_for_notebook(GtkNotebook* notebook) {
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

static void update_transparency_all(void) {
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

static void update_scrollback_for_notebook(GtkNotebook* notebook) {
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

static void update_scrollback_all(void) {
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

static void setup_pty_and_shell(VteTerminal* vt) {
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

static void setup_key_events(VteTerminal* vt) {
    GtkEventController* keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), vt);
    gtk_widget_add_controller(GTK_WIDGET(vt), keys);
}

static void on_selection_changed(VteTerminal* vt, gpointer user_data) {
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

static GtkNotebook* get_notebook_from_terminal(VteTerminal* vt) {
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

static void title_tab_sig_cb_new(VteTerminal* vt, guint prop_id, gpointer user_data) {
    (void)prop_id;
    update_tab_title(vt, GTK_NOTEBOOK(user_data));
}

#if !VTE_CHECK_VERSION(0, 78, 0)
static void title_tab_sig_cb_old(VteTerminal* vt, gpointer user_data) {
    update_tab_title(vt, GTK_NOTEBOOK(user_data));
}
#endif

static VteTerminal* add_tab(GtkNotebook* notebook) {
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

static void on_tab_close_clicked(GtkButton* btn, gpointer user_data) {
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

static void on_child_exit_tab(VteTerminal* vt, int status, GtkNotebook* notebook) {
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
        GtkWidget* window = gtk_widget_get_ancestor(GTK_WIDGET(notebook), GTK_TYPE_WINDOW);
        gtk_window_destroy(GTK_WINDOW(window));
    }
}

static void update_tab_title(VteTerminal* vt, GtkNotebook* notebook) {
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
                // Use same title logic as update_title
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
                g_autofree char* title = NULL;
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
                gtk_label_set_text(GTK_LABEL(label), title);

                // Also update window title if this is the active tab
                if (i == gtk_notebook_get_current_page(notebook)) {
                    GtkWidget* window = gtk_widget_get_ancestor(GTK_WIDGET(notebook), GTK_TYPE_WINDOW);
                    gtk_window_set_title(GTK_WINDOW(window), title);
                }
            }
            break;
        }
    }
}

static void on_notebook_page_added(GtkNotebook* notebook, GtkWidget* child, guint page_num, gpointer user_data) {
    (void)child;
    (void)page_num;
    (void)user_data;
    g_print("on_notebook_page_added: pages=%d\n", gtk_notebook_get_n_pages(notebook));
    // Show tabs if more than one page
    gboolean show_tabs = gtk_notebook_get_n_pages(notebook) > 1;
    g_print("Setting show_tabs=%d\n", show_tabs);
    gtk_notebook_set_show_tabs(notebook, show_tabs);
}

static void on_notebook_page_removed(GtkNotebook* notebook, GtkWidget* child, guint page_num, gpointer user_data) {
    (void)child;
    (void)page_num;
    (void)user_data;
    // Show tabs if more than one page
    gtk_notebook_set_show_tabs(notebook, gtk_notebook_get_n_pages(notebook) > 1);
    // If no pages left, close window
    if (gtk_notebook_get_n_pages(notebook) == 0) {
        GtkWidget* window = gtk_widget_get_ancestor(GTK_WIDGET(notebook), GTK_TYPE_WINDOW);
        gtk_window_destroy(GTK_WINDOW(window));
    }
}

static void on_notebook_switch_page(GtkNotebook* notebook, GtkWidget* page, guint page_num, gpointer user_data) {
    (void)page;
    (void)user_data;
    // Update window title to active tab's title
    GtkWidget* scr = gtk_notebook_get_nth_page(notebook, page_num);
    GtkWidget* child = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(scr));
    VteTerminal* vt = VTE_TERMINAL(child);
    update_tab_title(vt, notebook);
}

static void close_current_tab(GtkNotebook* notebook) {
    int current = gtk_notebook_get_current_page(notebook);
    if (current >= 0) {
        gtk_notebook_remove_page(notebook, current);
    }
}

static void create_window(GtkApplication* app) {
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

// force-disable GTK accessibility bridge early to avoid a11y bus warnings
static void hard_disable_a11y(void) {
    g_setenv("GTK_A11Y", "none", TRUE);
    g_setenv("NO_AT_BRIDGE", "1", TRUE);
}

int main(int argc, char** argv) {
    atexit(free_compress_pool);

    hard_disable_a11y();  // must run before GTK initialization

    GtkApplication* app = gtk_application_new("com.example.oneterm", G_APPLICATION_NON_UNIQUE);

    g_signal_connect(app, "activate", G_CALLBACK(app_activate), NULL);

    GSimpleAction* act = g_simple_action_new("new-window", NULL);
    g_signal_connect(act, "activate", G_CALLBACK(new_window_action), app);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(act));

    int status = g_application_run(G_APPLICATION(app), argc, argv);

    g_object_unref(app);

    return status;
}
