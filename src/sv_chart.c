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
#include "sv_chart.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Gutters for the graticule labels, in unscaled points. The left one is
 * measured from the font instead, because a clipped latitude is worse than a
 * slightly narrower plot — see gutter_left(). */
#define GUT_B 26.0f
#define GUT_T 22.0f
#define GUT_R 14.0f

#define MARKER_R      6.0f       /* cast marker radius            */
#define HIT_R        14.0f       /* how close counts as hovering  */
#define MIN_M_PER_PX (0.02)      /* about 8 m across a wide plot  */
#define MAX_M_PER_PX (4000.0)    /* the whole ocean               */

/* Enough to keep labels from overlapping without a general layout solver. */
#define MAX_LABELS 32

static void draw_text(struct nk_context *ctx, struct nk_command_buffer *cb,
                      float x, float y, float w, float h,
                      const char *s, struct nk_color col)
{
    nk_draw_text(cb, nk_rect(x, y, w, h), s, (int)strlen(s), ctx->style.font,
                 nk_rgba(0, 0, 0, 0), col);
}

static float text_w(struct nk_context *ctx, const char *s)
{
    const struct nk_user_font *f = ctx->style.font;
    return f->width(f->userdata, f->height, s, (int)strlen(s));
}

/*
 * Width to leave for the latitude labels: the widest one this font can
 * produce, not a constant. A fixed gutter that suits 14 pt at scale 1 clips
 * "50° 25.700' N" at scale 2, and a half-eaten latitude on a chart is the sort
 * of detail someone plots a boat from.
 */
static float gutter_left(struct nk_context *ctx, float scale)
{
    float w = text_w(ctx, "180\xc2\xb0 88.8888' W") + 12.0f * scale;
    float min = 40.0f * scale;
    return w < min ? min : w;
}

/*
 * Cohen–Sutherland region code, used only to reject a line with both ends off
 * the same side of the plot. Panned or zoomed in, most of the track is off the
 * chart and its coordinates run to millions of pixels; a segment like that is
 * still turned into geometry, and at that magnitude the vertices lose the
 * precision that would have put the visible end in the right place.
 */
enum { OC_L = 1, OC_R = 2, OC_T = 4, OC_B = 8 };

static int outcode(struct nk_rect r, float x, float y)
{
    int c = 0;
    if (x < r.x)          c |= OC_L;
    if (x > r.x + r.w)    c |= OC_R;
    if (y < r.y)          c |= OC_T;
    if (y > r.y + r.h)    c |= OC_B;
    return c;
}

/* Intersection of two rects, empty (w or h <= 0) if they do not overlap.
 * nk_unify() would do this but it is internal to the implementation unit. */
static struct nk_rect rect_clip(struct nk_rect a, struct nk_rect b)
{
    float x0 = a.x > b.x ? a.x : b.x;
    float y0 = a.y > b.y ? a.y : b.y;
    float x1 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    float y1 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    return nk_rect(x0, y0, x1 - x0, y1 - y0);
}

static struct nk_color fade(struct nk_color c, struct nk_color bg, float k)
{
    struct nk_color o;
    o.r = (nk_byte)(bg.r + (c.r - bg.r) * k);
    o.g = (nk_byte)(bg.g + (c.g - bg.g) * k);
    o.b = (nk_byte)(bg.b + (c.b - bg.b) * k);
    o.a = 255;
    return o;
}

/* ------------------------------------------------------------------ */
/* View                                                                */
/* ------------------------------------------------------------------ */

void sv_chart_request_fit(SvChartView *v)
{
    v->fit_pending = true;
}

void sv_chart_zoom(SvChartView *v, const SvChartInfo *info,
                   double factor, struct nk_vec2 at)
{
    if (!v->have_view || factor <= 0.0)
        return;

    double next = v->m_per_px / factor;
    if (next < MIN_M_PER_PX) next = MIN_M_PER_PX;
    if (next > MAX_M_PER_PX) next = MAX_M_PER_PX;
    if (next == v->m_per_px)
        return;

    /* Keep whatever is under the cursor where it is: shift the centre by the
     * change in how many metres that pixel offset represents. Without this a
     * zoom walks the feature you were aiming at off the edge. */
    struct nk_rect p = info ? info->plot : nk_rect(0, 0, 0, 0);
    if (p.w > 1.0f && p.h > 1.0f &&
        at.x >= p.x && at.x <= p.x + p.w && at.y >= p.y && at.y <= p.y + p.h) {

        double dx_px = at.x - (p.x + p.w / 2.0);
        double dy_px = at.y - (p.y + p.h / 2.0);
        double d_e = dx_px * (v->m_per_px - next);
        double d_n = -dy_px * (v->m_per_px - next);

        SvGeoProj pr;
        sv_geo_proj_init(&pr, v->lat, v->lon);
        sv_geo_inverse(&pr, d_e, d_n, &v->lat, &v->lon);
    }

    v->m_per_px = next;
    v->follow = false;
}

void sv_chart_pan(SvChartView *v, double dx_px, double dy_px)
{
    if (!v->have_view)
        return;

    SvGeoProj pr;
    sv_geo_proj_init(&pr, v->lat, v->lon);
    sv_geo_inverse(&pr, -dx_px * v->m_per_px, dy_px * v->m_per_px,
                   &v->lat, &v->lon);
    v->follow = false;
}

static void do_fit(SvChartView *v, struct nk_rect in,
                   const SvCast *const *casts, int n_casts,
                   const SvFix *track, int n_track,
                   bool live_valid, double live_lat, double live_lon)
{
    SvGeoBounds b;
    sv_geo_bounds_reset(&b);

    for (int i = 0; i < n_casts; i++)
        if (casts[i] && casts[i]->has_fix)
            sv_geo_bounds_add(&b, casts[i]->lat, casts[i]->lon);
    for (int i = 0; i < n_track; i++)
        sv_geo_bounds_add(&b, track[i].lat, track[i].lon);
    if (live_valid)
        sv_geo_bounds_add(&b, live_lat, live_lon);

    if (b.n == 0)
        return;

    sv_geo_bounds_centre(&b, &v->lat, &v->lon);

    double w_m = 0, h_m = 0;
    sv_geo_bounds_extent_m(&b, &w_m, &h_m);

    /* A single position, or several within a metre of each other, has no
     * extent to scale from; open at 500 m across so there is context. */
    double need_x = (in.w > 1.0f) ? w_m / (in.w * 0.82) : 0.0;
    double need_y = (in.h > 1.0f) ? h_m / (in.h * 0.82) : 0.0;
    double m_per_px = (need_x > need_y) ? need_x : need_y;

    if (!(m_per_px > 0.0) || !isfinite(m_per_px))
        m_per_px = (in.w > 1.0f) ? 500.0 / in.w : 0.5;

    if (m_per_px < MIN_M_PER_PX) m_per_px = MIN_M_PER_PX;
    if (m_per_px > MAX_M_PER_PX) m_per_px = MAX_M_PER_PX;

    v->m_per_px = m_per_px;
    v->have_view = true;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

/* Short form for the scale bar: "500 m", "2 km". */
static void scale_label(char *dst, size_t cap, double metres)
{
    if (metres < 1000.0)
        snprintf(dst, cap, "%.0f m", metres);
    else
        snprintf(dst, cap, "%.4g km", metres / 1000.0);
}

static void draw_graticule(struct nk_context *ctx, struct nk_command_buffer *cb,
                           const SvTheme *t, float scale, struct nk_rect in,
                           const SvGeoProj *pr, double m_per_px)
{
    /* Span of the plot in degrees, from its own corners. */
    double lat_lo, lon_lo, lat_hi, lon_hi;
    sv_geo_inverse(pr, -in.w / 2.0 * m_per_px, -in.h / 2.0 * m_per_px,
                   &lat_lo, &lon_lo);
    sv_geo_inverse(pr,  in.w / 2.0 * m_per_px,  in.h / 2.0 * m_per_px,
                   &lat_hi, &lon_hi);

    int div_y = (int)(in.h / (110.0f * scale));
    int div_x = (int)(in.w / (150.0f * scale));
    if (div_y < 2) div_y = 2;
    if (div_x < 2) div_x = 2;

    double step_lat = sv_geo_grid_step_deg(lat_hi - lat_lo, div_y);
    double step_lon = sv_geo_grid_step_deg(lon_hi - lon_lo, div_x);

    char lab[48];
    float fh = ctx->style.font->height;

    /* Parallels. */
    double first = floor(lat_lo / step_lat) * step_lat;
    for (double lat = first; lat <= lat_hi + step_lat / 2; lat += step_lat) {
        double e, n;
        sv_geo_forward(pr, lat, pr->lon0, &e, &n);
        float y = in.y + in.h / 2.0f - (float)(n / m_per_px);
        if (y < in.y - 1 || y > in.y + in.h + 1)
            continue;

        nk_stroke_line(cb, in.x, y, in.x + in.w, y, 1.0f, t->plot_grid);
        sv_geo_format_lat(lab, sizeof lab, lat, step_lat);
        float w = text_w(ctx, lab);
        draw_text(ctx, cb, in.x - 8 * scale - w, y - fh / 2, w + 2, fh,
                  lab, t->text_dim);
    }

    /* Meridians. Labels are skipped where they would collide, as on the
     * profile plot — a graticule reading "3 41.0' W3 40.5' W" is worse than
     * one label fewer. */
    float last_right = -1e9f;
    first = floor(lon_lo / step_lon) * step_lon;
    for (double lon = first; lon <= lon_hi + step_lon / 2; lon += step_lon) {
        double e, n;
        sv_geo_forward(pr, pr->lat0, lon, &e, &n);
        float x = in.x + in.w / 2.0f + (float)(e / m_per_px);
        if (x < in.x - 1 || x > in.x + in.w + 1)
            continue;

        nk_stroke_line(cb, x, in.y, x, in.y + in.h, 1.0f, t->plot_grid);

        sv_geo_format_lon(lab, sizeof lab, lon, step_lon);
        float w = text_w(ctx, lab);
        float lx = x - w / 2;
        if (lx < in.x) lx = in.x;
        if (lx + w > in.x + in.w) lx = in.x + in.w - w;
        if (lx < last_right + 10 * scale)
            continue;
        last_right = lx + w;

        draw_text(ctx, cb, lx, in.y + in.h + 6 * scale, w + 2, fh,
                  lab, t->text_dim);
    }
}

static void draw_scale_bar(struct nk_context *ctx,
                           struct nk_command_buffer *cb,
                           const SvTheme *t, float scale, struct nk_rect in,
                           double m_per_px)
{
    /* A round distance that fits in a quarter of the plot, so the bar can be
     * read off rather than interpolated. */
    double want_m = in.w * 0.25 * m_per_px;
    double bar_m  = sv_geo_nice_step(want_m);
    float  bar_px = (float)(bar_m / m_per_px);
    if (bar_px < 20 * scale || bar_px > in.w)
        return;

    float x = in.x + 12 * scale;
    float y = in.y + in.h - 16 * scale;
    float fh = ctx->style.font->height;

    nk_stroke_line(cb, x, y, x + bar_px, y, 2.0f * scale, t->plot_axis);
    nk_stroke_line(cb, x, y - 4 * scale, x, y + 4 * scale, 2.0f * scale,
                   t->plot_axis);
    nk_stroke_line(cb, x + bar_px, y - 4 * scale, x + bar_px, y + 4 * scale,
                   2.0f * scale, t->plot_axis);

    char lab[32];
    scale_label(lab, sizeof lab, bar_m);
    draw_text(ctx, cb, x, y - fh - 6 * scale, bar_px + 40 * scale, fh,
              lab, t->text_dim);
}

/* True north is straight up in this projection, which is worth stating on the
 * chart rather than leaving the reader to assume it. */
static void draw_north(struct nk_context *ctx, struct nk_command_buffer *cb,
                       const SvTheme *t, float scale, struct nk_rect in)
{
    float x = in.x + in.w - 18 * scale;
    float y = in.y + 14 * scale;
    float h = 20 * scale;
    float fh = ctx->style.font->height;

    nk_stroke_line(cb, x, y + h, x, y, 1.5f * scale, t->plot_axis);
    nk_stroke_line(cb, x, y, x - 4 * scale, y + 6 * scale, 1.5f * scale,
                   t->plot_axis);
    nk_stroke_line(cb, x, y, x + 4 * scale, y + 6 * scale, 1.5f * scale,
                   t->plot_axis);
    draw_text(ctx, cb, x - text_w(ctx, "N") / 2, y + h + 1 * scale,
              20 * scale, fh, "N", t->text_dim);
}

void sv_chart_draw(struct nk_context *ctx, struct nk_rect area,
                   const SvTheme *t, float scale,
                   SvChartView *v,
                   const SvCast *const *casts, int n_casts, int selected,
                   const SvFix *track, int n_track,
                   bool live_valid, bool live_stale,
                   double live_lat, double live_lon,
                   struct nk_vec2 mouse, SvChartInfo *out)
{
    struct nk_command_buffer *cb = nk_window_get_canvas(ctx);

    if (out) {
        memset(out, 0, sizeof *out);
        out->hit = -1;
    }
    if (!cb || area.w < 80 || area.h < 80)
        return;

    float gl = gutter_left(ctx, scale), gb = GUT_B * scale;
    float gt = GUT_T * scale, gr = GUT_R * scale;
    struct nk_rect in = nk_rect(area.x + gl, area.y + gt,
                                area.w - gl - gr, area.h - gt - gb);
    if (in.w < 40 || in.h < 40)
        return;

    nk_fill_rect(cb, area, 4.0f * scale, t->plot_bg);

    int no_fix = 0;
    for (int i = 0; i < n_casts; i++)
        if (casts[i] && !casts[i]->has_fix)
            no_fix++;

    if (v->fit_pending || !v->have_view) {
        do_fit(v, in, casts, n_casts, track, n_track,
               live_valid, live_lat, live_lon);
        v->fit_pending = false;
    }

    if (v->follow && live_valid) {
        v->lat = live_lat;
        v->lon = live_lon;
    }

    if (!v->have_view) {
        /* Nothing has a position yet. Say which of the two reasons it is. */
        const char *msg = (n_casts > 0)
            ? "No cast in memory has a position — the instrument logged them "
              "with no GPS fix"
            : "No positions yet. Casts carry their own fix; the live position "
              "comes from the status broadcast.";
        float fh = ctx->style.font->height;
        draw_text(ctx, cb, in.x + 12 * scale, in.y + in.h / 2 - fh / 2,
                  in.w - 24 * scale, fh, msg, t->text_faint);
        if (out) {
            out->plot = in;
            out->no_fix = no_fix;
        }
        return;
    }

    SvGeoProj pr;
    sv_geo_proj_init(&pr, v->lat, v->lon);

    float cx = in.x + in.w / 2.0f;
    float cy = in.y + in.h / 2.0f;
    double mpp = v->m_per_px;

    draw_graticule(ctx, cb, t, scale, in, &pr, mpp);
    nk_stroke_rect(cb, in, 0.0f, 1.0f, t->plot_axis);

    /*
     * Everything from here to the restore below is positioned by where it is
     * in the world, not by the plot rect, so with the view panned or following
     * a moving vessel it lands outside the chart. Nuklear's stroke and fill
     * commands are not bounded by the rect they were computed from — the track
     * drew itself straight across the toolbar and off the window — so the data
     * layers get their own scissor. It is intersected with the clip already in
     * force, which is the panel's, so this can only ever narrow it.
     */
    struct nk_rect clip_outer = cb->clip;
    struct nk_rect clip_plot = rect_clip(clip_outer, in);
    if (clip_plot.w <= 0.0f || clip_plot.h <= 0.0f)
        clip_plot = nk_rect(in.x, in.y, 0.0f, 0.0f);
    nk_push_scissor(cb, clip_plot);

    /* ---- track ----------------------------------------------------- */
    struct nk_color track_col = fade(t->trace_temp, t->plot_bg, 0.55f);
    float px = 0, py = 0;
    int prev_oc = 0;
    bool have_prev = false;

    for (int i = 0; i < n_track; i++) {
        double e, n;
        sv_geo_forward(&pr, track[i].lat, track[i].lon, &e, &n);
        float x = cx + (float)(e / mpp);
        float y = cy - (float)(n / mpp);
        int oc = outcode(in, x, y);

        /* Both ends off the same edge cannot cross the plot. A segment with
         * one end inside, or ends on opposite sides, is drawn whole and left
         * to the scissor. */
        if (have_prev && (prev_oc & oc) == 0)
            nk_stroke_line(cb, px, py, x, y, 1.4f * scale, track_col);
        px = x;
        py = y;
        prev_oc = oc;
        have_prev = true;
    }

    /* ---- casts ----------------------------------------------------- */
    struct nk_rect taken[MAX_LABELS];
    int n_taken = 0;
    int plotted = 0, off_view = 0;
    int hit = -1;
    float hit_d2 = HIT_R * scale * HIT_R * scale;
    float fh = ctx->style.font->height;

    for (int i = n_casts - 1; i >= 0; i--) {          /* oldest drawn first */
        const SvCast *c = casts[i];
        if (!c || !c->has_fix)
            continue;

        double e, n;
        sv_geo_forward(&pr, c->lat, c->lon, &e, &n);
        float x = cx + (float)(e / mpp);
        float y = cy - (float)(n / mpp);

        /* Counted by whether it is actually on the chart, not by whether it
         * has a position: with the plot clipped, a panel claiming four casts
         * over a chart showing two is just wrong. */
        if (outcode(in, x, y) == 0)
            plotted++;
        else
            off_view++;

        /* Drawn a little beyond the edge as well, so a marker on the boundary
         * appears as the part of itself that belongs inside. */
        if (x < in.x - 40 || x > in.x + in.w + 40 ||
            y < in.y - 40 || y > in.y + in.h + 40)
            continue;

        float dx = mouse.x - x, dy = mouse.y - y;
        if (dx * dx + dy * dy <= hit_d2) {
            hit_d2 = dx * dx + dy * dy;
            hit = i;
        }

        bool sel = (i == selected);
        float r = MARKER_R * scale * (sel ? 1.35f : 1.0f);
        struct nk_color fill = sel ? t->accent
                                   : fade(t->trace_sv, t->plot_bg, 0.85f);

        if (sel)
            nk_stroke_circle(cb, nk_rect(x - r - 4 * scale, y - r - 4 * scale,
                                         (r + 4 * scale) * 2,
                                         (r + 4 * scale) * 2),
                             1.5f * scale, t->accent);

        nk_fill_circle(cb, nk_rect(x - r, y - r, r * 2, r * 2), fill);
        nk_stroke_circle(cb, nk_rect(x - r, y - r, r * 2, r * 2),
                         1.0f * scale, t->plot_bg);

        if (!v->labels)
            continue;

        char lab[64];
        snprintf(lab, sizeof lab, "%02d:%02d  %.1f m",
                 c->hour, c->minute, c->max_depth);

        float lw = text_w(ctx, lab) + 2;
        struct nk_rect lr = nk_rect(x + r + 5 * scale, y - fh / 2, lw, fh);

        /* Flip to the left of the marker rather than run off the edge — a cast
         * near the right-hand side is exactly the one just taken. */
        if (lr.x + lr.w > in.x + in.w)
            lr.x = x - r - 5 * scale - lw;
        if (lr.x < in.x)
            lr.x = in.x;

        /* Drop a label rather than stack it on top of another one. The
         * marker stays: losing a position would be a lie, losing its caption
         * is only an inconvenience. */
        bool clash = false;
        for (int k = 0; k < n_taken && !clash; k++)
            clash = NK_INTERSECT(lr.x, lr.y, lr.w, lr.h,
                                 taken[k].x, taken[k].y,
                                 taken[k].w, taken[k].h) != 0;
        if (clash)
            continue;

        if (n_taken < MAX_LABELS)
            taken[n_taken++] = lr;

        draw_text(ctx, cb, lr.x, lr.y, lr.w, lr.h, lab,
                  sel ? t->text : t->text_dim);
    }

    /* ---- live position --------------------------------------------- */
    if (live_valid) {
        double e, n;
        sv_geo_forward(&pr, live_lat, live_lon, &e, &n);
        float x = cx + (float)(e / mpp);
        float y = cy - (float)(n / mpp);
        float r = 5.0f * scale;

        /* Hollow when the fix has stopped being refreshed. The instrument is
         * silent at the command prompt, which is exactly when the operator is
         * downloading — so this marker is routinely minutes old, and a solid
         * dot claiming to be the vessel would be the chart's biggest lie. */
        struct nk_color col = live_stale ? t->warn : t->ok;

        nk_stroke_line(cb, x - r * 2.2f, y, x + r * 2.2f, y, 1.0f * scale, col);
        nk_stroke_line(cb, x, y - r * 2.2f, x, y + r * 2.2f, 1.0f * scale, col);

        if (live_stale)
            nk_stroke_circle(cb, nk_rect(x - r, y - r, r * 2, r * 2),
                             1.6f * scale, col);
        else
            nk_fill_circle(cb, nk_rect(x - r, y - r, r * 2, r * 2), col);
    }

    /* Back to the panel's clip: what follows is chart furniture, drawn in the
     * plot's own coordinates and allowed to sit in the gutters. */
    nk_push_scissor(cb, clip_outer);

    draw_scale_bar(ctx, cb, t, scale, in, mpp);
    draw_north(ctx, cb, t, scale, in);

    /* ---- cursor readout -------------------------------------------- */
    bool inside = mouse.x >= in.x && mouse.x <= in.x + in.w &&
                  mouse.y >= in.y && mouse.y <= in.y + in.h;
    double clat = 0, clon = 0;

    if (inside) {
        sv_geo_inverse(&pr, (mouse.x - cx) * mpp, (cy - mouse.y) * mpp,
                       &clat, &clon);

        char l1[64], l2[64], box[256];
        double res = mpp * 4.0;          /* label no finer than the pixels */
        double step = res / 111320.0;
        sv_geo_format_lat(l1, sizeof l1, clat, step > 0 ? step : 1.0 / 3600);
        sv_geo_format_lon(l2, sizeof l2, clon, step > 0 ? step : 1.0 / 3600);

        if (hit >= 0 && casts[hit]) {
            const SvCast *c = casts[hit];
            snprintf(box, sizeof box, "%s   %.1f m   %02d:%02d:%02d",
                     c->name[0] ? c->name : "cast", c->max_depth,
                     c->hour, c->minute, c->second);
        } else if (live_valid) {
            char d[48];
            sv_geo_format_distance(d, sizeof d,
                sv_geo_distance_m(live_lat, live_lon, clat, clon));
            snprintf(box, sizeof box, "%s  %s    %s / %03.0f from position",
                     l1, l2, d,
                     sv_geo_bearing_deg(live_lat, live_lon, clat, clon));
        } else {
            snprintf(box, sizeof box, "%s  %s", l1, l2);
        }

        float w = text_w(ctx, box) + 16 * scale;
        float h = fh + 8 * scale;
        float bx = mouse.x + 14 * scale;
        float by = mouse.y - h - 8 * scale;
        if (bx + w > in.x + in.w) bx = in.x + in.w - w;
        if (bx < in.x)            bx = in.x;
        if (by < in.y)            by = mouse.y + 12 * scale;

        struct nk_rect r = nk_rect(bx, by, w, h);
        nk_fill_rect(cb, r, 3.0f * scale, t->panel);
        nk_stroke_rect(cb, r, 3.0f * scale, 1.0f, t->border);
        draw_text(ctx, cb, bx + 8 * scale, by + 4 * scale, w, fh, box,
                  t->text);
    }

    if (out) {
        out->plotted      = plotted;
        out->off_view     = off_view;
        out->no_fix       = no_fix;
        out->track_points = n_track;
        out->cursor_valid = inside;
        out->cursor_lat   = clat;
        out->cursor_lon   = clon;
        out->hit          = hit;
        out->plot         = in;
    }
}
