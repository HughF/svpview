/*
 * sv_theme.h — colour roles and Nuklear styling
 *
 * One palette table per theme, addressed by role rather than by colour name,
 * so a widget never hard-codes an RGB value and the two themes cannot drift
 * apart.
 */
#ifndef SV_THEME_H
#define SV_THEME_H

#include "nk.h"

typedef struct {
    bool is_light;

    struct nk_color bg;            /* window background        */
    struct nk_color panel;         /* raised surface           */
    struct nk_color panel_alt;     /* alternating row          */
    struct nk_color rail;          /* navigation rail          */
    struct nk_color border;
    struct nk_color divider;

    struct nk_color text;          /* primary                  */
    struct nk_color text_dim;      /* secondary/units          */
    struct nk_color text_faint;    /* disabled/hint            */
    struct nk_color text_on_accent;

    struct nk_color accent;        /* selection, focus, primary button */
    struct nk_color accent_hover;
    struct nk_color accent_dim;

    struct nk_color ok;
    struct nk_color warn;
    struct nk_color alarm;

    struct nk_color plot_bg;
    struct nk_color plot_grid;
    struct nk_color plot_axis;
    struct nk_color trace_sv;
    struct nk_color trace_temp;
    struct nk_color trace_sal;
    struct nk_color trace_density;
    struct nk_color scrim;         /* dims the page behind a dialog */
} SvTheme;

extern const SvTheme SV_THEME_DARK;
extern const SvTheme SV_THEME_LIGHT;

/* Push the theme into a Nuklear context: colour table plus the metrics
 * (rounding, padding, spacing) that make the layout read as one system. */
void sv_theme_apply(struct nk_context *ctx, const SvTheme *t, float scale);

#endif /* SV_THEME_H */
