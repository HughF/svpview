/*
 * sv_chart.h — plan view of cast positions
 *
 * A chart, not a map: a graticule, the casts, the track the instrument has
 * been broadcasting, a scale bar. There is deliberately no basemap imagery —
 * see docs/CHART.md — so this needs no tile server, no internet on a boat,
 * and no dependency beyond the SDL2 the rest of the program already uses.
 *
 * Drawn into the Nuklear command buffer like sv_plot, so it clips and
 * composites with the widgets around it.
 */
#ifndef SV_CHART_H
#define SV_CHART_H

#include "nk.h"
#include "sv_geo.h"
#include "sv_theme.h"
#include "sv_types.h"
#include "sv_app.h"

/* View state, owned by the UI so panning and zooming survive a page change. */
typedef struct {
    double lat, lon;          /* what is at the centre of the plot        */
    double m_per_px;          /* zoom; larger is further out              */
    bool   have_view;         /* false until the first fit                */
    bool   fit_pending;       /* fit on the next draw, which knows the rect */
    bool   follow;            /* keep the live position centred           */
    bool   labels;            /* draw the cast labels                     */
} SvChartView;

/* What the last frame drew, for the panel beside the chart to report on. */
typedef struct {
    int    plotted;           /* casts drawn                              */
    int    no_fix;            /* casts held in memory with no position    */
    int    track_points;
    bool   cursor_valid;      /* mouse was inside the plot                */
    double cursor_lat, cursor_lon;
    int    hit;               /* cast index under the cursor, or -1       */
    struct nk_rect plot;      /* the inner plot rect, for hit testing     */
} SvChartInfo;

/*
 * Ask for the view to be set to contain everything it can find. The fit
 * happens inside the next draw, which is the only place that knows how many
 * pixels there are to fit into.
 */
void sv_chart_request_fit(SvChartView *v);

/* Zoom about a point in the plot, keeping the position under it fixed. */
void sv_chart_zoom(SvChartView *v, const SvChartInfo *info,
                   double factor, struct nk_vec2 at);

/* Pan by a pixel delta. */
void sv_chart_pan(SvChartView *v, double dx_px, double dy_px);

/*
 * live_stale marks a fix that is no longer being refreshed — the instrument
 * broadcasts nothing while it sits at the command prompt, so the last known
 * position can be minutes old and the vessel long gone from it. A stale
 * position is drawn hollow rather than filled; a solid marker means the fix is
 * current.
 */
void sv_chart_draw(struct nk_context *ctx, struct nk_rect area,
                   const SvTheme *t, float scale,
                   SvChartView *v,
                   const SvCast *const *casts, int n_casts, int selected,
                   const SvFix *track, int n_track,
                   bool live_valid, bool live_stale,
                   double live_lat, double live_lon,
                   struct nk_vec2 mouse, SvChartInfo *out);

#endif /* SV_CHART_H */
