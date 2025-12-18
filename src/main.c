#include "config.h"
#include "1term.h"
#include "window.h"
#include "clipboard.h"

static void print_usage(const char* argv0) {
    g_print("Usage: %s [--help] [--version]\n", argv0);
}

static gboolean try_handle_cli(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (g_str_equal(argv[i], "--help") || g_str_equal(argv[i], "-h")) {
            print_usage(argv[0]);
            return TRUE;
        }
        if (g_str_equal(argv[i], "--version") || g_str_equal(argv[i], "-V")) {
            g_print("1term %s\n", ONETERM_VERSION);
            return TRUE;
        }
    }
    return FALSE;
}

static void new_window_action(GSimpleAction* a, GVariant* p, gpointer user_data) {
    create_window(GTK_APPLICATION(user_data));
}

static void app_activate(GApplication* gapp, gpointer unused) {
    create_window(GTK_APPLICATION(gapp));
}

static void hard_disable_a11y(void) {
    g_setenv("GTK_A11Y", "none", TRUE);
    g_setenv("NO_AT_BRIDGE", "1", TRUE);
}

int main(int argc, char** argv) {
    if (try_handle_cli(argc, argv))
        return 0;

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
