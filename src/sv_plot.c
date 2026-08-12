#include "sv_plot.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

/* Axis gutters, in unscaled points. */
#define GUT_L 58.0f
#define GUT_B 30.0f
#define GUT_T 26.0f
#define GUT_R 14.0f

typedef struct {
    float lo, hi;
} Range;

static bool range_valid(Range r)
{
    return isfinite(r.lo) && isfinite(r.hi) && r.hi > r.lo;
}

/* Grow a range to a round number of divisions so the gridlines land on
 * values a human would have chosen. */
static Range nice_range(double lo, double hi, int target_div)
{
    Range r = { 0, 1 };

    if (!(isfinite(lo) && isfinite(hi)) || hi < lo)
        return r;
    if (hi - lo < 1e-9) {
        hi += 0.5;
        lo -= 0.5;
    }

    double raw = (hi - lo) / (target_div > 0 ? target_div : 5);
    double mag = pow(10.0, floor(log10(raw)));
    double norm = raw / mag;
    double step = (norm <= 1.0) ? 1.0 : (norm <= 2.0) ? 2.0
                : (norm <= 5.0) ? 5.0 : 10.0;
    step *= mag;

    r.lo = (float)(floor(lo / step) * step);
    r.hi = (float)(ceil(hi / step) * step);
    if (!(r.hi > r.lo))
        r.hi = r.lo + (float)step;
    return r;
}

/* Number of decimals worth showing for a given axis span. */
static int decimals_for(float span)
{
    if (span >= 100.0f) return 0;
    if (span >= 10.0f)  return 1;
    if (span >= 1.0f)   return 2;
    return 3;
}

static void draw_text(struct nk_context *ctx, struct nk_command_buffer *cb,
                      float x, float y, float w, float h,
                      const char *s, struct nk_color col)
{
    struct nk_rect r = nk_rect(x, y, w, h);
    nk_draw_text(cb, r, s, (int)strlen(s), ctx->style.font,
                 nk_rgba(0, 0, 0, 0), col);
}

static float text_w(struct nk_context *ctx, const char *s)
{
    const struct nk_user_font *f = ctx->style.font;
    return f->width(f->userdata, f->height, s, (int)strlen(s));
}

/* Value accessor per trace. */
static double sample_value(const SvSample *s, unsigned bit)
{
    switch (bit) {
    case SV_TRACE_SV:      return s->sv;
    case SV_TRACE_TEMP:    return s->temp;
    case SV_TRACE_SAL:     return s->salinity;
    case SV_TRACE_DENSITY: return s->density;
    }
    return 0.0;
}

static struct nk_color trace_colour(const SvTheme *t, unsigned bit)
{
    switch (bit) {
    case SV_TRACE_SV:      return t->trace_sv;
    case SV_TRACE_TEMP:    return t->trace_temp;
    case SV_TRACE_SAL:     return t->trace_sal;
    case SV_TRACE_DENSITY: return t->trace_density;
    }
    return t->text;
}

static const char *trace_label(unsigned bit)
{
    switch (bit) {
    case SV_TRACE_SV:      return "Sound velocity  m/s";
    case SV_TRACE_TEMP:    return "Temperature  DegC";
    case SV_TRACE_SAL:     return "Salinity  PSU";
    case SV_TRACE_DENSITY: return "Density  kg/m3";
    }
    return "";
}

void sv_plot_profile(struct nk_context *ctx, struct nk_rect area,
                     const SvTheme *t, float scale,
                     const SvCast *const *casts, int n_casts, int highlight,
                     unsigned traces, struct nk_vec2 mouse)
{
    struct nk_command_buffer *cb = nk_window_get_canvas(ctx);
    if (!cb || area.w < 60 || area.h < 60)
        return;

    float gl = GUT_L * scale, gb = GUT_B * scale;
    float gt = GUT_T * scale, gr = GUT_R * scale;

    struct nk_rect in = nk_rect(area.x + gl, area.y + gt,
                                area.w - gl - gr, area.h - gt - gb);
    if (in.w < 20 || in.h < 20)
        return;

    nk_fill_rect(cb, area, 4.0f * scale, t->plot_bg);

    /* ---- ranges ---------------------------------------------------- */
    double dmin = 1e30, dmax = -1e30;
    int total = 0;

    for (int i = 0; i < n_casts; i++) {
        const SvCast *c = casts[i];
        if (!c || c->n <= 0)
            continue;
        if (c->min_depth < dmin) dmin = c->min_depth;
        if (c->max_depth > dmax) dmax = c->max_depth;
        total += c->n;
    }

    if (total == 0) {
        const char *msg = "No profile loaded";
        draw_text(ctx, cb, in.x, in.y + in.h / 2 - ctx->style.font->height / 2,
                  in.w, ctx->style.font->height, msg, t->text_faint);
        return;
    }

    Range depth = nice_range(dmin, dmax, 6);
    if (!range_valid(depth))
        return;

    /* Per-trace value ranges, so each trace uses the full width. */
    Range vr[4];
    unsigned bits[4] = { SV_TRACE_SV, SV_TRACE_TEMP,
                         SV_TRACE_SAL, SV_TRACE_DENSITY };
    int n_shown = 0;
    int primary = -1;

    for (int b = 0; b < 4; b++) {
        vr[b] = (Range){ 0, 1 };
        if (!(traces & bits[b]))
            continue;

        double lo = 1e30, hi = -1e30;
        for (int i = 0; i < n_casts; i++) {
            const SvCast *c = casts[i];
            if (!c) continue;
            for (int k = 0; k < c->n; k++) {
                double v = sample_value(&c->s[k], bits[b]);
                if (!isfinite(v)) continue;
                if (v < lo) lo = v;
                if (v > hi) hi = v;
            }
        }
        vr[b] = nice_range(lo, hi, 5);
        if (range_valid(vr[b])) {
            if (primary < 0) primary = b;
            n_shown++;
        }
    }
    if (primary < 0)
        return;

    /* ---- grid ------------------------------------------------------ */
    const int DIVS = 5;
    int vdec = decimals_for(vr[primary].hi - vr[primary].lo);

    /* Gridlines always; labels only where they will not collide with the one
     * before. At a high UI scale on a narrow plot there is not room for all
     * six, and overlapping numbers are worse than fewer of them. */
    float last_right = -1e9f;

    for (int i = 0; i <= DIVS; i++) {
        float fx = in.x + in.w * (float)i / DIVS;
        nk_stroke_line(cb, fx, in.y, fx, in.y + in.h, 1.0f, t->plot_grid);

        char lab[32];
        double v = vr[primary].lo +
                   (vr[primary].hi - vr[primary].lo) * (double)i / DIVS;
        snprintf(lab, sizeof lab, "%.*f", vdec, v);

        float w = text_w(ctx, lab);
        float lx = fx - w / 2;
        if (lx < area.x) lx = area.x;
        if (lx + w > area.x + area.w) lx = area.x + area.w - w;

        if (lx < last_right + 8 * scale)
            continue;
        last_right = lx + w;

        draw_text(ctx, cb, lx, in.y + in.h + 6 * scale,
                  w + 2, ctx->style.font->height, lab, t->text_dim);
    }

    int ddec = decimals_for(depth.hi - depth.lo);
    for (int i = 0; i <= DIVS; i++) {
        float fy = in.y + in.h * (float)i / DIVS;
        nk_stroke_line(cb, in.x, fy, in.x + in.w, fy, 1.0f, t->plot_grid);

        char lab[32];
        double d = depth.lo + (depth.hi - depth.lo) * (double)i / DIVS;
        snprintf(lab, sizeof lab, "%.*f", ddec, d);

        float w = text_w(ctx, lab);
        draw_text(ctx, cb, in.x - 8 * scale - w,
                  fy - ctx->style.font->height / 2,
                  w + 2, ctx->style.font->height, lab, t->text_dim);
    }

    nk_stroke_rect(cb, in, 0.0f, 1.0f, t->plot_axis);

    /* Depth axis caption, at the top left of the gutter. */
    draw_text(ctx, cb, area.x + 4 * scale, area.y + 5 * scale,
              gl, ctx->style.font->height, "Depth m", t->text_dim);

    /* ---- traces ---------------------------------------------------- */
    float dspan = depth.hi - depth.lo;

    for (int b = 0; b < 4; b++) {
        if (!(traces & bits[b]) || !range_valid(vr[b]))
            continue;

        struct nk_color col = trace_colour(t, bits[b]);
        float vspan = vr[b].hi - vr[b].lo;

        for (int i = 0; i < n_casts; i++) {
            const SvCast *c = casts[i];
            if (!c || c->n < 2)
                continue;

            bool dim = (n_casts > 1 && i != highlight);
            struct nk_color cc = col;
            if (dim) {
                cc.a = 90;
                cc.r = (nk_byte)((cc.r + t->plot_bg.r) / 2);
                cc.g = (nk_byte)((cc.g + t->plot_bg.g) / 2);
                cc.b = (nk_byte)((cc.b + t->plot_bg.b) / 2);
            }
            float thick = (dim ? 1.0f : 1.6f) * scale;

            /*
             * Decimated to the pixel grid: a point closer than PIX_MIN to the
             * last one drawn, in both axes, cannot be told apart on screen.
             *
             * This is not only for speed. A 1200-sample cast is 1200 line
             * segments per trace, and four casts with two traces each is
             * ~9600 — which is where Nuklear's 16-bit vertex index used to
             * overflow and abort the program. The last sample is always drawn
             * so the trace still ends at the deepest reading.
             */
            const float PIX_MIN = 0.7f * scale;

            float px = 0, py = 0;
            bool have_prev = false;

            for (int k = 0; k < c->n; k++) {
                double v = sample_value(&c->s[k], bits[b]);
                double d = c->s[k].depth;
                if (!isfinite(v) || !isfinite(d))
                    continue;

                float x = in.x + in.w * (float)((v - vr[b].lo) / vspan);
                float y = in.y + in.h * (float)((d - depth.lo) / dspan);

                if (have_prev) {
                    bool last = (k == c->n - 1);
                    if (!last &&
                        fabsf(x - px) < PIX_MIN && fabsf(y - py) < PIX_MIN)
                        continue;
                    nk_stroke_line(cb, px, py, x, y, thick, cc);
                }
                px = x;
                py = y;
                have_prev = true;
            }
        }
    }

    /* ---- legend ---------------------------------------------------- */
    float lx = in.x + 8 * scale;
    float ly = area.y + 5 * scale;

    for (int b = 0; b < 4; b++) {
        if (!(traces & bits[b]) || !range_valid(vr[b]))
            continue;

        const char *lab = trace_label(bits[b]);
        float w = text_w(ctx, lab);
        float sw = 14 * scale;

        if (lx + sw + 6 * scale + w > in.x + in.w)
            break;

        nk_fill_rect(cb, nk_rect(lx, ly + ctx->style.font->height / 2 - 1.5f * scale,
                                 sw, 3 * scale),
                     1.0f, trace_colour(t, bits[b]));
        draw_text(ctx, cb, lx + sw + 6 * scale, ly, w + 2,
                  ctx->style.font->height, lab, t->text_dim);
        lx += sw + 6 * scale + w + 18 * scale;
    }

    /* ---- cursor ---------------------------------------------------- */
    if (n_shown > 0 && mouse.x >= in.x && mouse.x <= in.x + in.w &&
        mouse.y >= in.y && mouse.y <= in.y + in.h) {

        nk_stroke_line(cb, in.x, mouse.y, in.x + in.w, mouse.y,
                       1.0f, t->plot_axis);

        double d = depth.lo + dspan * (double)((mouse.y - in.y) / in.h);

        /* Nearest sample in the highlighted cast. */
        const SvCast *c = (highlight >= 0 && highlight < n_casts)
                        ? casts[highlight] : NULL;
        char box[96];
        if (c && c->n > 0) {
            int best = 0;
            double bd = 1e30;
            for (int k = 0; k < c->n; k++) {
                double diff = fabs(c->s[k].depth - d);
                if (diff < bd) { bd = diff; best = k; }
            }
            snprintf(box, sizeof box, "%.2f m    %.2f m/s    %.2f DegC",
                     c->s[best].depth, c->s[best].sv, c->s[best].temp);
        } else {
            snprintf(box, sizeof box, "%.2f m", d);
        }

        float w = text_w(ctx, box) + 16 * scale;
        float h = ctx->style.font->height + 8 * scale;
        float bx = mouse.x + 12 * scale;
        float by = mouse.y - h - 6 * scale;

        if (bx + w > in.x + in.w) bx = in.x + in.w - w;
        if (by < in.y)            by = mouse.y + 10 * scale;

        struct nk_rect r = nk_rect(bx, by, w, h);
        nk_fill_rect(cb, r, 3.0f * scale, t->panel);
        nk_stroke_rect(cb, r, 3.0f * scale, 1.0f, t->border);
        draw_text(ctx, cb, bx + 8 * scale, by + 4 * scale,
                  w, ctx->style.font->height, box, t->text);
    }
}

void sv_plot_live(struct nk_context *ctx, struct nk_rect area,
                  const SvTheme *t, float scale,
                  const SvLive *samples, int n)
{
    struct nk_command_buffer *cb = nk_window_get_canvas(ctx);
    if (!cb || area.w < 60 || area.h < 40)
        return;

    nk_fill_rect(cb, area, 4.0f * scale, t->plot_bg);

    float gl = GUT_L * scale, gb = GUT_B * scale;
    float gt = GUT_T * scale, gr = GUT_R * scale;
    struct nk_rect in = nk_rect(area.x + gl, area.y + gt,
                                area.w - gl - gr, area.h - gt - gb);
    if (in.w < 20 || in.h < 20)
        return;

    if (n < 2) {
        draw_text(ctx, cb, in.x, in.y + in.h / 2, in.w,
                  ctx->style.font->height,
                  "Waiting for continuous-mode data", t->text_faint);
        return;
    }

    double lo = 1e30, hi = -1e30;
    for (int i = 0; i < n; i++) {
        double v = samples[i].primary;
        if (!isfinite(v)) continue;
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }

    Range r = nice_range(lo, hi, 4);
    if (!range_valid(r))
        return;

    int dec = decimals_for(r.hi - r.lo);
    for (int i = 0; i <= 4; i++) {
        float fy = in.y + in.h * (float)i / 4;
        nk_stroke_line(cb, in.x, fy, in.x + in.w, fy, 1.0f, t->plot_grid);

        char lab[32];
        snprintf(lab, sizeof lab, "%.*f",
                 dec, r.hi - (r.hi - r.lo) * (double)i / 4);
        float w = text_w(ctx, lab);
        draw_text(ctx, cb, in.x - 8 * scale - w,
                  fy - ctx->style.font->height / 2,
                  w + 2, ctx->style.font->height, lab, t->text_dim);
    }

    nk_stroke_rect(cb, in, 0.0f, 1.0f, t->plot_axis);
    draw_text(ctx, cb, area.x + 4 * scale, area.y + 5 * scale, gl * 2,
              ctx->style.font->height,
              samples[n - 1].primary_is_cond ? "Conductivity mS/cm"
                                             : "Sound velocity m/s",
              t->text_dim);

    float px = 0, py = 0;
    for (int i = 0; i < n; i++) {
        double v = samples[i].primary;
        if (!isfinite(v))
            continue;

        float x = in.x + in.w * (float)i / (float)(n - 1);
        float y = in.y + in.h * (float)(1.0 - (v - r.lo) / (r.hi - r.lo));
        if (i > 0)
            nk_stroke_line(cb, px, py, x, y, 1.6f * scale, t->trace_sv);
        px = x;
        py = y;
    }
}
