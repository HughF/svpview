/*
 * sv_plot.h — profile and time-series plotting
 *
 * Drawn into the Nuklear command buffer rather than straight to the
 * renderer, so plots composite in the right order with the widgets around
 * them and inherit the same clipping.
 */
#ifndef SV_PLOT_H
#define SV_PLOT_H

#include "nk.h"
#include "sv_theme.h"
#include "sv_types.h"

enum {
    SV_TRACE_SV      = 1 << 0,
    SV_TRACE_TEMP    = 1 << 1,
    SV_TRACE_SAL     = 1 << 2,
    SV_TRACE_DENSITY = 1 << 3
};

/*
 * Depth profile. Casts are drawn in order with `highlight` fully opaque and
 * the rest dimmed, so an overlay of ten casts still reads.
 *
 * Pass mouse in window coordinates to get a cursor readout; pass a rect with
 * zero size for no cursor.
 */
void sv_plot_profile(struct nk_context *ctx, struct nk_rect area,
                     const SvTheme *t, float scale,
                     const SvCast *const *casts, int n_casts, int highlight,
                     unsigned traces, struct nk_vec2 mouse);

/* Live time series of the last n samples. */
void sv_plot_live(struct nk_context *ctx, struct nk_rect area,
                  const SvTheme *t, float scale,
                  const SvLive *samples, int n);

#endif /* SV_PLOT_H */
