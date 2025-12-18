#ifndef COLORS_H
#define COLORS_H

#include <gtk/gtk.h>
#include <vte/vte.h>

typedef struct {
    const char* name;
    GdkRGBA foreground;
    GdkRGBA background;
    GdkRGBA palette[16];
} ColorScheme;

int get_color_scheme_count(void);
const ColorScheme* get_color_scheme(int index);
void apply_color_scheme(VteTerminal* vt, int scheme_index);

#endif  // COLORS_H
