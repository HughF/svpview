/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Hugh Frater
 *
 * This file is part of svpview. svpview is free software: you can
 * redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version. It is distributed in
 * the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License in LICENSE for details.
 */
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
