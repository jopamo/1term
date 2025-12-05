#ifndef ONETERM_H
#define ONETERM_H

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

// Global settings
extern gboolean transparency_enabled;
extern gboolean scrollback_enabled;
extern GtkCssProvider* css_provider;

// Function declarations
void update_transparency_all(void);
void update_scrollback_all(void);
void update_css_transparency(void);

#endif  // ONETERM_H