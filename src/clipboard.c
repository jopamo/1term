#include "clipboard.h"

static GThreadPool* compress_pool = NULL;

typedef struct {
    gchar* text;
    gchar* path;
    int lvl;
} CompressJob;

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

    g_print("Scroll-back compressed → %s\n", j->path);

    g_free(tmpl);
    ZSTD_freeCStream(zs);

done:
    g_free(j->text);
    g_free(j->path);
    g_free(j);
}

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

void compress_scrollback_via_clipboard_async(VteTerminal* vt) {
    GtkWidget* widget = GTK_WIDGET(vt);
    GdkClipboard* cb = gtk_widget_get_clipboard(widget);

    vte_terminal_select_all(vt);
    vte_terminal_copy_clipboard_format(vt, VTE_FORMAT_TEXT);

    g_object_ref(vt);

    gdk_clipboard_read_text_async(cb, NULL, compress_clipboard_text_ready, vt);
}

void free_compress_pool(void) {
    if (compress_pool) {
        g_thread_pool_free(compress_pool, TRUE, TRUE);
        compress_pool = NULL;
    }
}