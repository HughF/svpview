/*
 * sv_ui.c — window shell, pages and dialogs
 *
 * Layout rule that keeps everything aligned: the shell computes an exact
 * pixel rect for every region from the window size, and each region is its
 * own Nuklear window at that rect. Nothing is positioned by flow, so no
 * region can push another out of place at any window size or UI scale.
 *
 * Inside a region, forms use one row template — a fixed-width label column
 * and a dynamic control column — so every label and every control in the
 * program lines up on the same two edges.
 *
 * Dialogs are modal: a scrim window covers the page and swallows input, and
 * every dialog carries the same three buttons in the same order, right
 * aligned: Cancel discards, Apply commits and stays open, OK commits and
 * closes. The title bar close button and Escape both mean Cancel.
 *
 * A dialog is sized to its content rather than to a guessed constant: the
 * body is measured while it is drawn and the window height follows, so no
 * dialog is taller than it needs to be or hides its own controls behind a
 * scrollbar. Only if the content genuinely exceeds the window does the body
 * scroll, and the button row stays pinned regardless.
 */
/*
 * NK_IMPLEMENTATION has to be defined before anything pulls in nk.h, which
 * several of the headers below do — nk.h has an include guard, so a later
 * definition would silently do nothing and the link would fail on every
 * Nuklear symbol.
 */
#define NK_IMPLEMENTATION
#define NK_SDL_RENDERER_IMPLEMENTATION
#include "nk.h"
#include "nuklear_sdl_renderer.h"

#include "sv_ui.h"
#include "sv_theme.h"
#include "sv_plot.h"
#include "sv_chart.h"
#include "sv_profile.h"
#include "sv_export.h"
#include "sv_vigo.h"
#include "sv_version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Unscaled metrics, multiplied by the HiDPI scale at use. */
#define STATUS_H   46.0f
#define ACTION_H   34.0f
#define RAIL_W    132.0f
#define ROW_H      26.0f
#define LABEL_W   190.0f
#define BTN_W      96.0f
#define BTN_H      32.0f

/* Clear space under a dialog's button row. Without it the buttons sit hard
 * against the window border and read as clipped. */
#define DLG_BOTTOM_MARGIN 12.0f

/* Smallest window the layout still works in, at UI scale 1. The width comes
 * from the status strip: its fixed columns add up to 900, and narrower than
 * this the deploy column is squeezed until "NOT ready to deploy" is cut off
 * mid-word — the one line in the program that must never be misread. */
#define WIN_MIN_W 1120
#define WIN_MIN_H 640

/* Fraction of the desktop's usable area a default-sized window may occupy. */
#define WIN_MAX_FRAC 0.85f

typedef enum {
    PAGE_LIVE = 0, PAGE_PROFILE, PAGE_CHART, PAGE_FILES, PAGE_SETTINGS,
    PAGE_LOG,
    PAGE_COUNT
} SvPage;

static const char *PAGE_NAME[PAGE_COUNT] = {
    "Live", "Profile", "Chart", "Files", "Settings", "Log"
};

typedef enum {
    DLG_NONE = 0, DLG_CONNECT, DLG_SETTINGS, DLG_EXPORT, DLG_PROCESS,
    DLG_NETWORK, DLG_ABOUT
} SvDialog;

struct SvUi {
    struct nk_context *ctx;
    SDL_Window        *win;
    SDL_Renderer      *ren;
    SvApp             *app;

    const SvTheme *theme;
    bool     dark;
    float    scale;
    SvPage   page;
    bool     quit;

    SvDialog dialog;
    char     dlg_error[160];
    char     dlg_note[160];

    /* connect dialog */
    PlatPortInfo ports[PLAT_MAX_PORTS];
    int      n_ports, sel_port;

    /* network dialog */
    PlatNetIf nifs[PLAT_MAX_IFS];
    int      n_nifs, sel_nif;      /* sel_nif < 0 = default route */

    /* settings dialog */
    SvConfig cfg;
    char     site_edit[SV_SITE_LEN];

    /* export dialog */
    int      export_fmt;
    char     export_dir[SV_MAX_PATH];
    char     export_name[SV_MAX_NAME];

    /* process dialog */
    int      proc_downcast, proc_despike, proc_thin_to;
    float    proc_bin;

    unsigned traces;
    int      overlay;

    /* chart page */
    SvChartView chart;
    SvChartInfo chart_info;
    float    chart_drag_px;        /* how far this drag has gone         */

    /* log page follow-the-tail state */
    int      log_seen;
    float    log_max_off;

    /* dialog auto-sizing */
    SvDialog dlg_measured;
    float    dlg_natural_h;

    char     title[192];       /* last title pushed to the window manager */
};

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static float S(const SvUi *ui, float v) { return v * ui->scale; }

/*
 * Height for the big plot on a page laid out as: one control row, a gap, the
 * plot, a gap, one footer row.
 *
 * Nuklear inserts window.spacing.y between every row, so subtracting only the
 * rows' own heights overruns the page by four spacings and pushes the footer
 * out of sight — which is exactly what happened to the profile page's summary
 * line at UI scale 2.
 */
static float plot_row_height(SvUi *ui, struct nk_rect page)
{
    float rows    = S(ui, ROW_H) * 2.0f + S(ui, 4) * 2.0f;
    float spacing = ui->ctx->style.window.spacing.y * 4.0f;
    float h = page.h - rows - spacing;

    if (h < S(ui, 100))
        h = page.h * 0.7f;
    return h;
}

/* A two-column form row. Every form in the program uses this, which is why
 * the labels and controls all share the same two edges. */
static void form_row(SvUi *ui, float h)
{
    nk_layout_row_template_begin(ui->ctx, S(ui, h));
    nk_layout_row_template_push_static(ui->ctx, S(ui, LABEL_W));
    nk_layout_row_template_push_dynamic(ui->ctx);
    nk_layout_row_template_end(ui->ctx);
}

static void form_label(SvUi *ui, const char *text)
{
    nk_label(ui->ctx, text, NK_TEXT_RIGHT);
}

/* Key/value line for read-only display. */
static void info_row(SvUi *ui, const char *k, const char *v)
{
    form_row(ui, ROW_H);
    nk_label_colored(ui->ctx, k, NK_TEXT_RIGHT, ui->theme->text_dim);
    nk_label(ui->ctx, v, NK_TEXT_LEFT);
}

static void info_rowf(SvUi *ui, const char *k, const char *fmt, ...)
{
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    info_row(ui, k, buf);
}

static void section(SvUi *ui, const char *title)
{
    nk_layout_row_dynamic(ui->ctx, S(ui, 22.0f), 1);
    nk_label_colored(ui->ctx, title, NK_TEXT_LEFT, ui->theme->accent);
}

static void gap(SvUi *ui, float h)
{
    nk_layout_row_dynamic(ui->ctx, S(ui, h), 1);
    nk_spacing(ui->ctx, 1);
}

/*
 * Height for a list group holding `rows` rows of `row_h`, so the box is the
 * size of what is in it rather than a fixed guess — with a floor so an empty
 * list still has room for its "nothing found" message, and a ceiling so a
 * machine with a dozen adapters does not produce a dialog taller than the
 * screen.
 */
static float list_height(SvUi *ui, int rows, float row_h, float extra,
                         float min_h, float max_h)
{
    struct nk_context *c = ui->ctx;
    float pitch = S(ui, row_h) + c->style.window.spacing.y;
    float h = rows * pitch + S(ui, extra)
            + c->style.window.group_padding.y * 2.0f;

    if (h < S(ui, min_h)) h = S(ui, min_h);
    if (h > S(ui, max_h)) h = S(ui, max_h);
    return h;
}

/* A button that reads as the primary action. */
static bool primary_button(SvUi *ui, const char *label)
{
    struct nk_context *c = ui->ctx;
    const SvTheme *t = ui->theme;

    nk_style_push_style_item(c, &c->style.button.normal,
                             nk_style_item_color(t->accent));
    nk_style_push_style_item(c, &c->style.button.hover,
                             nk_style_item_color(t->accent_hover));
    nk_style_push_style_item(c, &c->style.button.active,
                             nk_style_item_color(t->accent_dim));
    nk_style_push_color(c, &c->style.button.text_normal, t->text_on_accent);
    nk_style_push_color(c, &c->style.button.text_hover, t->text_on_accent);
    nk_style_push_color(c, &c->style.button.border_color, t->accent);

    bool hit = nk_button_label(c, label) != 0;

    nk_style_pop_color(c);
    nk_style_pop_color(c);
    nk_style_pop_color(c);
    nk_style_pop_style_item(c);
    nk_style_pop_style_item(c);
    nk_style_pop_style_item(c);
    return hit;
}

static struct nk_color link_colour(const SvUi *ui, SvLinkState s)
{
    switch (s) {
    case SV_LINK_CLOSED: return ui->theme->text_faint;
    case SV_LINK_RUN:    return ui->theme->ok;
    case SV_LINK_BUSY:   return ui->theme->warn;
    default:             return ui->theme->accent;
    }
}

static const char *link_text(SvLinkState s)
{
    switch (s) {
    case SV_LINK_CLOSED:      return "Not connected";
    case SV_LINK_IDENTIFYING: return "Identifying";
    case SV_LINK_INTERRUPTED: return "Interrupted";
    case SV_LINK_RUN:         return "Run mode";
    case SV_LINK_BUSY:        return "Busy";
    }
    return "?";
}

/* ------------------------------------------------------------------ */
/* Deploy-flag diagnosis                                               */
/*                                                                     */
/* Guide section 4.4: a zero flag has four causes and the operator needs */
/* the fix, not the code.                                              */
/* ------------------------------------------------------------------ */

static const char *deploy_advice(const SvState *st, bool simulate)
{
    if (!st->status_valid)
        return "No status broadcast yet — the instrument sends one every "
               "10 s while in smart profile mode at the surface.";

    if (st->status.ready_to_deploy)
        return NULL;

    if (!simulate && st->link != SV_LINK_CLOSED)
        return "GPS is off while the USB comms cable is plugged in. "
               "Unplug it and use the Bluetooth key.";
    if (!st->status.has_fix)
        return "No GPS fix. Move to open sky and wait — a hot fix takes "
               "under 30 s, a cold one a few minutes.";
    return "Pressure reads above the trigger point, or the GPS clock is "
           "stale. Power cycle with a valid fix so a new file and tare "
           "are created.";
}

/* ------------------------------------------------------------------ */
/* Status strip                                                        */
/* ------------------------------------------------------------------ */

static void draw_status(SvUi *ui, struct nk_rect r)
{
    struct nk_context *c = ui->ctx;
    const SvTheme *t = ui->theme;
    const SvState *st = sv_app_state(ui->app);

    nk_style_push_style_item(c, &c->style.window.fixed_background,
                             nk_style_item_color(t->rail));
    nk_style_push_vec2(c, &c->style.window.padding,
                       nk_vec2(S(ui, 14), S(ui, 8)));

    if (nk_begin(c, "status", r, NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_template_begin(c, S(ui, 30));
        nk_layout_row_template_push_static(c, S(ui, 240));  /* instrument */
        nk_layout_row_template_push_static(c, S(ui, 150));  /* link       */
        nk_layout_row_template_push_static(c, S(ui, 140));  /* battery    */
        nk_layout_row_template_push_static(c, S(ui, 220));  /* fix        */
        nk_layout_row_template_push_dynamic(c);             /* deploy     */
        nk_layout_row_template_push_static(c, S(ui, 150));  /* winch      */
        nk_layout_row_template_end(c);

        char buf[128];
        if (st->serial[0])
            snprintf(buf, sizeof buf, "SWiFT %s", st->serial);
        else
            snprintf(buf, sizeof buf, "SWiFT");
        nk_label(c, buf, NK_TEXT_LEFT);

        nk_label_colored(c, link_text(st->link), NK_TEXT_LEFT,
                         link_colour(ui, st->link));

        if (st->status_valid)
            snprintf(buf, sizeof buf, "%.1f h battery",
                     st->status.battery_hours);
        else
            snprintf(buf, sizeof buf, "battery —");
        nk_label_colored(c, buf, NK_TEXT_LEFT,
                         (st->status_valid && st->status.battery_hours < 12.0)
                         ? t->alarm : t->text_dim);

        if (st->status_valid && st->status.has_fix)
            snprintf(buf, sizeof buf, "%.4f  %.4f",
                     st->status.lat, st->status.lon);
        else
            snprintf(buf, sizeof buf, "no fix");
        nk_label_colored(c, buf, NK_TEXT_LEFT,
                         (st->status_valid && st->status.has_fix)
                         ? t->text_dim : t->warn);

        if (st->status_valid) {
            bool ok = st->status.ready_to_deploy;
            nk_label_colored(c, ok ? "Ready to deploy"
                                   : "NOT ready to deploy",
                             NK_TEXT_LEFT, ok ? t->ok : t->alarm);
        } else {
            nk_label_colored(c, "awaiting status", NK_TEXT_LEFT, t->text_faint);
        }

        const char *w = st->winch == SV_WINCH_REACHABLE ? "Winch reachable"
                      : st->winch == SV_WINCH_SILENT    ? "Winch silent"
                                                        : "Winch unknown";
        nk_label_colored(c, w, NK_TEXT_RIGHT,
                         st->winch == SV_WINCH_REACHABLE ? t->ok
                       : st->winch == SV_WINCH_SILENT    ? t->warn
                                                         : t->text_faint);
    }
    nk_end(c);

    nk_style_pop_vec2(c);
    nk_style_pop_style_item(c);
}

/* ------------------------------------------------------------------ */
/* Navigation rail                                                     */
/* ------------------------------------------------------------------ */

static void draw_rail(SvUi *ui, struct nk_rect r)
{
    struct nk_context *c = ui->ctx;
    const SvTheme *t = ui->theme;

    nk_style_push_style_item(c, &c->style.window.fixed_background,
                             nk_style_item_color(t->rail));
    nk_style_push_vec2(c, &c->style.window.padding,
                       nk_vec2(S(ui, 8), S(ui, 12)));

    if (nk_begin(c, "rail", r, NK_WINDOW_NO_SCROLLBAR)) {
        for (int i = 0; i < PAGE_COUNT; i++) {
            nk_layout_row_dynamic(c, S(ui, 32), 1);
            nk_bool on = (ui->page == (SvPage)i);
            if (nk_selectable_label(c, PAGE_NAME[i], NK_TEXT_LEFT, &on) && on)
                ui->page = (SvPage)i;
        }

        gap(ui, 10);
        nk_layout_row_dynamic(c, S(ui, 30), 1);
        if (nk_button_label(c, "About...")) {
            ui->dlg_error[0] = ui->dlg_note[0] = '\0';
            ui->dialog = DLG_ABOUT;
        }

        nk_layout_row_dynamic(c, S(ui, 30), 1);
        if (nk_button_label(c, ui->dark ? "Light theme" : "Dark theme")) {
            ui->dark = !ui->dark;
            ui->theme = ui->dark ? &SV_THEME_DARK : &SV_THEME_LIGHT;
            sv_theme_apply(c, ui->theme, ui->scale);
        }
    }
    nk_end(c);

    nk_style_pop_vec2(c);
    nk_style_pop_style_item(c);
}

/* ------------------------------------------------------------------ */
/* Action bar                                                          */
/* ------------------------------------------------------------------ */

static void draw_action(SvUi *ui, struct nk_rect r)
{
    struct nk_context *c = ui->ctx;
    const SvTheme *t = ui->theme;
    const SvState *st = sv_app_state(ui->app);

    nk_style_push_style_item(c, &c->style.window.fixed_background,
                             nk_style_item_color(t->rail));
    nk_style_push_vec2(c, &c->style.window.padding,
                       nk_vec2(S(ui, 14), S(ui, 7)));

    if (nk_begin(c, "action", r, NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_template_begin(c, S(ui, 20));
        nk_layout_row_template_push_dynamic(c);
        nk_layout_row_template_push_static(c, S(ui, 260));
        nk_layout_row_template_end(c);

        char buf[220];
        if (st->error[0]) {
            snprintf(buf, sizeof buf, "%s", st->error);
            nk_label_colored(c, buf, NK_TEXT_LEFT, t->alarm);
        } else if (st->job != SV_JOB_NONE) {
            snprintf(buf, sizeof buf, "%s  %d/%d",
                     st->job_label, st->job_step, st->job_steps);
            nk_label_colored(c, buf, NK_TEXT_LEFT, t->warn);
        } else {
            int n = sv_app_cast_count(ui->app);
            const SvCast *cast = sv_app_cast(ui->app, sv_app_selected(ui->app));
            if (cast)
                snprintf(buf, sizeof buf,
                         "%d cast%s loaded   ·   %s: %.2f m, %d samples",
                         n, n == 1 ? "" : "s", cast->name,
                         cast->max_depth, cast->n);
            else
                snprintf(buf, sizeof buf, "No casts loaded");
            nk_label_colored(c, buf, NK_TEXT_LEFT, t->text_dim);
        }

        if (st->winch_ack_ms)
            snprintf(buf, sizeof buf, "Winch acknowledged %.2f m",
                     st->winch_last_depth);
        else
            snprintf(buf, sizeof buf, "No depth reported yet");
        nk_label_colored(c, buf, NK_TEXT_RIGHT,
                         st->winch_ack_ms ? t->ok : t->text_faint);
    }
    nk_end(c);

    nk_style_pop_vec2(c);
    nk_style_pop_style_item(c);
}

/* ------------------------------------------------------------------ */
/* Pages                                                               */
/* ------------------------------------------------------------------ */

static void page_live(SvUi *ui, struct nk_rect r)
{
    struct nk_context *c = ui->ctx;
    const SvState *st = sv_app_state(ui->app);

    float plot_h = r.h - S(ui, 210);
    if (plot_h < S(ui, 120))
        plot_h = r.h * 0.5f;

    nk_layout_row_dynamic(c, plot_h, 1);
    struct nk_rect area = nk_widget_bounds(c);
    nk_spacing(c, 1);
    sv_plot_live(c, area, ui->theme, ui->scale, st->live, st->live_n);

    gap(ui, 8);
    section(ui, "Instrument");

    info_row(ui, "Port", st->port[0] ? st->port : "—");
    info_row(ui, "Serial number", st->serial[0] ? st->serial : "—");
    info_row(ui, "Firmware", st->firmware[0] ? st->firmware : "—");

    if (st->status_valid) {
        info_row(ui, "Last recorded file", st->status.last_file);
        info_rowf(ui, "Checksum failures", "%d", st->checksum_bad);
    }

    const char *advice = deploy_advice(st, true);
    if (advice) {
        gap(ui, 6);
        nk_layout_row_dynamic(c, S(ui, 40), 1);
        nk_label_colored_wrap(c, advice, ui->theme->warn);
    }
}

static void page_profile(SvUi *ui, struct nk_rect r)
{
    struct nk_context *c = ui->ctx;
    int n = sv_app_cast_count(ui->app);
    int sel = sv_app_selected(ui->app);

    /* Trace toggles and actions, on one aligned row. */
    nk_layout_row_template_begin(c, S(ui, ROW_H));
    nk_layout_row_template_push_static(c, S(ui, 62));
    nk_layout_row_template_push_static(c, S(ui, 110));
    nk_layout_row_template_push_static(c, S(ui, 92));
    nk_layout_row_template_push_static(c, S(ui, 92));
    nk_layout_row_template_push_static(c, S(ui, 100));
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_push_static(c, S(ui, BTN_W));
    nk_layout_row_template_push_static(c, S(ui, BTN_W));
    nk_layout_row_template_end(c);

    nk_label_colored(c, "Traces", NK_TEXT_RIGHT, ui->theme->text_dim);

    struct { const char *l; unsigned bit; } tr[4] = {
        { "Velocity", SV_TRACE_SV },   { "Temp", SV_TRACE_TEMP },
        { "Salinity", SV_TRACE_SAL },  { "Density", SV_TRACE_DENSITY }
    };
    for (int i = 0; i < 4; i++) {
        nk_bool on = (ui->traces & tr[i].bit) != 0;
        nk_checkbox_label(c, tr[i].l, &on);
        if (on) ui->traces |= tr[i].bit;
        else    ui->traces &= ~tr[i].bit;
    }

    nk_spacing(c, 1);

    if (nk_button_label(c, "Process...") && n > 0)
        ui->dialog = DLG_PROCESS;
    if (nk_button_label(c, "Export...") && n > 0) {
        const SvCast *cast = sv_app_cast(ui->app, sel);
        if (cast)
            sv_export_filename(ui->export_name, sizeof ui->export_name,
                               "%s_%d%t.%e", cast,
                               (SvExportFormat)ui->export_fmt);
        ui->dialog = DLG_EXPORT;
        ui->dlg_error[0] = ui->dlg_note[0] = '\0';
    }

    gap(ui, 4);

    /* Plot on the left, cast list on the right. */
    float body_h = plot_row_height(ui, r);

    nk_layout_row_template_begin(c, body_h);
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_push_static(c, S(ui, 250));
    nk_layout_row_template_end(c);

    struct nk_rect area = nk_widget_bounds(c);
    nk_spacing(c, 1);

    const SvCast *list[SV_MAX_CASTS];
    int count = 0;
    if (ui->overlay) {
        for (int i = 0; i < n && count < SV_MAX_CASTS; i++)
            list[count++] = sv_app_cast(ui->app, i);
    } else if (sel >= 0) {
        list[count++] = sv_app_cast(ui->app, sel);
    }

    /* No cursor readout while a dialog is up: the plot is behind the scrim
     * and a readout that still followed the mouse would look live. */
    struct nk_vec2 m = (ui->dialog == DLG_NONE)
                     ? nk_vec2(c->input.mouse.pos.x, c->input.mouse.pos.y)
                     : nk_vec2(-1.0f, -1.0f);
    sv_plot_profile(c, area, ui->theme, ui->scale, list, count,
                    ui->overlay ? sel : 0, ui->traces, m);

    if (nk_group_begin(c, "casts", NK_WINDOW_BORDER)) {
        nk_layout_row_dynamic(c, S(ui, 20), 1);
        nk_label_colored(c, "Casts", NK_TEXT_LEFT, ui->theme->accent);

        nk_layout_row_dynamic(c, S(ui, 24), 1);
        nk_bool ov = ui->overlay != 0;
        nk_checkbox_label(c, "Overlay all", &ov);
        ui->overlay = ov;

        for (int i = 0; i < n; i++) {
            const SvCast *cast = sv_app_cast(ui->app, i);
            if (!cast)
                continue;

            char lab[96];
            snprintf(lab, sizeof lab, "%02d:%02d:%02d   %.1f m",
                     cast->hour, cast->minute, cast->second, cast->max_depth);

            nk_layout_row_dynamic(c, S(ui, 26), 1);
            nk_bool on = (i == sel);
            if (nk_selectable_label(c, lab, NK_TEXT_LEFT, &on) && on)
                sv_app_select_cast(ui->app, i);
        }

        if (n == 0) {
            nk_layout_row_dynamic(c, S(ui, 40), 1);
            nk_label_colored_wrap(c, "Download a file, or open one from the "
                                     "Files page.", ui->theme->text_faint);
        }
        nk_group_end(c);
    }

    /* Summary of the selected cast, aligned with everything else. */
    const SvCast *cast = sv_app_cast(ui->app, sel);
    if (cast) {
        gap(ui, 4);
        nk_layout_row_template_begin(c, S(ui, ROW_H));
        for (int i = 0; i < 4; i++)
            nk_layout_row_template_push_dynamic(c);
        nk_layout_row_template_end(c);

        char b[4][96];
        snprintf(b[0], sizeof b[0], "Max depth  %.2f m", cast->max_depth);
        snprintf(b[1], sizeof b[1], "Mean velocity  %.2f m/s",
                 sv_profile_mean_sv(cast));
        snprintf(b[2], sizeof b[2], "Velocity  %.1f – %.1f m/s",
                 cast->min_sv, cast->max_sv);
        snprintf(b[3], sizeof b[3], "Temperature  %.2f – %.2f DegC",
                 cast->min_temp, cast->max_temp);
        for (int i = 0; i < 4; i++)
            nk_label_colored(c, b[i], NK_TEXT_LEFT, ui->theme->text_dim);
    }
}

/*
 * Chart page — where the casts are, in plan.
 *
 * Positions come from two sources with different precision, which the panel
 * states rather than blending: each cast carries the fix written into its file
 * header, and the live marker and track come from the $PVBB broadcast, which
 * is only given to four decimal places.
 */
static void page_chart(SvUi *ui, struct nk_rect r)
{
    struct nk_context *c = ui->ctx;
    const SvState *st = sv_app_state(ui->app);
    const SvTheme *t = ui->theme;
    int n = sv_app_cast_count(ui->app);
    int sel = sv_app_selected(ui->app);

    bool live = st->status_valid && st->status.has_fix;
    double live_lat = st->status.lat, live_lon = st->status.lon;

    /* Controls, on the same aligned row idiom as the profile page. */
    nk_layout_row_template_begin(c, S(ui, ROW_H));
    nk_layout_row_template_push_static(c, S(ui, 62));
    nk_layout_row_template_push_static(c, S(ui, 46));
    nk_layout_row_template_push_static(c, S(ui, 46));
    nk_layout_row_template_push_static(c, S(ui, 110));
    nk_layout_row_template_push_static(c, S(ui, 130));
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_end(c);

    if (nk_button_label(c, "Fit"))
        sv_chart_request_fit(&ui->chart);

    struct nk_vec2 centre = nk_vec2(ui->chart_info.plot.x +
                                    ui->chart_info.plot.w / 2,
                                    ui->chart_info.plot.y +
                                    ui->chart_info.plot.h / 2);
    if (nk_button_label(c, "+"))
        sv_chart_zoom(&ui->chart, &ui->chart_info, 2.0, centre);
    if (nk_button_label(c, "-"))
        sv_chart_zoom(&ui->chart, &ui->chart_info, 0.5, centre);

    nk_bool labels = ui->chart.labels;
    nk_checkbox_label(c, "Labels", &labels);
    ui->chart.labels = labels;

    nk_bool follow = ui->chart.follow;
    nk_checkbox_label(c, "Follow position", &follow);
    if (follow && !live)
        follow = nk_false;                  /* nothing to follow */
    ui->chart.follow = follow;

    nk_spacing(c, 1);

    gap(ui, 4);

    float body_h = plot_row_height(ui, r);

    nk_layout_row_template_begin(c, body_h);
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_push_static(c, S(ui, 250));
    nk_layout_row_template_end(c);

    struct nk_rect area = nk_widget_bounds(c);
    nk_spacing(c, 1);

    const SvCast *list[SV_MAX_CASTS];
    int count = 0;
    for (int i = 0; i < n && count < SV_MAX_CASTS; i++)
        list[count++] = sv_app_cast(ui->app, i);

    /* Interaction, before the draw, so the frame the user sees is the one they
     * asked for rather than the one before it. */
    struct nk_rect plot = ui->chart_info.plot;
    bool interactive = (ui->dialog == DLG_NONE) && plot.w > 1.0f;

    if (interactive) {
        if (nk_input_is_mouse_hovering_rect(&c->input, plot) &&
            c->input.mouse.scroll_delta.y != 0.0f) {
            double f = pow(1.25, c->input.mouse.scroll_delta.y);
            sv_chart_zoom(&ui->chart, &ui->chart_info, f,
                          nk_vec2(c->input.mouse.pos.x, c->input.mouse.pos.y));
        }

        if (nk_input_has_mouse_click_down_in_rect(&c->input, NK_BUTTON_LEFT,
                                                 plot, nk_true)) {
            struct nk_vec2 d = c->input.mouse.delta;
            if (d.x != 0.0f || d.y != 0.0f) {
                sv_chart_pan(&ui->chart, d.x, d.y);
                ui->chart_drag_px += fabsf(d.x) + fabsf(d.y);
            }
        }

        /* A click selects the cast under it, but only if this was a click and
         * not the end of a pan. */
        if (nk_input_is_mouse_click_in_rect(&c->input, NK_BUTTON_LEFT, plot)) {
            if (ui->chart_drag_px < S(ui, 4) && ui->chart_info.hit >= 0)
                sv_app_select_cast(ui->app, ui->chart_info.hit);
            ui->chart_drag_px = 0.0f;
        }
        if (!nk_input_is_mouse_down(&c->input, NK_BUTTON_LEFT))
            ui->chart_drag_px = 0.0f;
    }

    struct nk_vec2 m = (ui->dialog == DLG_NONE)
                     ? nk_vec2(c->input.mouse.pos.x, c->input.mouse.pos.y)
                     : nk_vec2(-1.0f, -1.0f);

    sv_chart_draw(c, area, t, ui->scale, &ui->chart, list, count, sel,
                  st->track, st->track_n, live, live_lat, live_lon,
                  m, &ui->chart_info);

    /* ---- side panel ------------------------------------------------- */
    if (nk_group_begin(c, "chartside", NK_WINDOW_BORDER)) {
        char buf[128], l1[48], l2[48];

        nk_layout_row_dynamic(c, S(ui, 20), 1);
        nk_label_colored(c, "Position", NK_TEXT_LEFT, t->accent);

        nk_layout_row_dynamic(c, S(ui, 22), 1);
        if (live) {
            sv_geo_format_lat(l1, sizeof l1, live_lat, 1.0 / 3600);
            sv_geo_format_lon(l2, sizeof l2, live_lon, 1.0 / 3600);
            nk_label_colored(c, l1, NK_TEXT_LEFT, t->text);
            nk_layout_row_dynamic(c, S(ui, 22), 1);
            nk_label_colored(c, l2, NK_TEXT_LEFT, t->text);
        } else {
            nk_label_colored(c, st->status_valid ? "No GPS fix"
                                                 : "No status broadcast yet",
                             NK_TEXT_LEFT, t->warn);
        }

        nk_layout_row_dynamic(c, S(ui, 20), 1);
        snprintf(buf, sizeof buf, "%d track point%s",
                 st->track_n, st->track_n == 1 ? "" : "s");
        nk_label_colored(c, buf, NK_TEXT_LEFT, t->text_dim);

        gap(ui, 6);
        nk_layout_row_dynamic(c, S(ui, 20), 1);
        nk_label_colored(c, "Casts", NK_TEXT_LEFT, t->accent);

        for (int i = 0; i < n; i++) {
            const SvCast *cast = sv_app_cast(ui->app, i);
            if (!cast)
                continue;

            char lab[96];
            if (cast->has_fix && live) {
                char d[48];
                sv_geo_format_distance(d, sizeof d,
                    sv_geo_distance_m(live_lat, live_lon,
                                      cast->lat, cast->lon));
                snprintf(lab, sizeof lab, "%02d:%02d   %s",
                         cast->hour, cast->minute, d);
            } else if (cast->has_fix) {
                sv_geo_format_lat(l1, sizeof l1, cast->lat, 1.0 / 60);
                snprintf(lab, sizeof lab, "%02d:%02d   %s",
                         cast->hour, cast->minute, l1);
            } else {
                snprintf(lab, sizeof lab, "%02d:%02d   no position",
                         cast->hour, cast->minute);
            }

            nk_layout_row_dynamic(c, S(ui, 26), 1);
            nk_bool on = (i == sel);
            if (nk_selectable_label(c, lab, NK_TEXT_LEFT, &on) && on) {
                sv_app_select_cast(ui->app, i);
                ui->chart.follow = false;
            }
        }

        if (n == 0) {
            nk_layout_row_dynamic(c, S(ui, 54), 1);
            nk_label_colored_wrap(c, "No casts in memory. Download one, or "
                                     "open a .bin from the Files page.",
                                  t->text_faint);
        }

        /* A cast that was logged without a fix has no marker, so say so here
         * rather than let the chart quietly show fewer casts than the list. */
        if (ui->chart_info.no_fix > 0) {
            gap(ui, 4);
            nk_layout_row_dynamic(c, S(ui, 40), 1);
            snprintf(buf, sizeof buf,
                     "%d cast%s not shown: logged with no GPS fix.",
                     ui->chart_info.no_fix,
                     ui->chart_info.no_fix == 1 ? "" : "s");
            nk_label_colored_wrap(c, buf, t->warn);
        }

        nk_group_end(c);
    }

    /* Footer: what the view is showing, and the separation of the selected
     * cast from the current position — the two numbers an operator deciding
     * whether to dip again actually wants. */
    gap(ui, 4);
    nk_layout_row_template_begin(c, S(ui, ROW_H));
    for (int i = 0; i < 3; i++)
        nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_end(c);

    char f[3][128];
    if (ui->chart.have_view) {
        char across[48];
        sv_geo_format_distance(across, sizeof across,
                               ui->chart_info.plot.w * ui->chart.m_per_px);
        snprintf(f[0], sizeof f[0], "View  %s across", across);
    } else {
        snprintf(f[0], sizeof f[0], "View  —");
    }

    snprintf(f[1], sizeof f[1], "%d cast%s plotted", ui->chart_info.plotted,
             ui->chart_info.plotted == 1 ? "" : "s");

    const SvCast *cast = sv_app_cast(ui->app, sel);
    if (cast && cast->has_fix && live) {
        char d[48];
        sv_geo_format_distance(d, sizeof d,
            sv_geo_distance_m(live_lat, live_lon, cast->lat, cast->lon));
        snprintf(f[2], sizeof f[2], "Selected  %s / %03.0f from position", d,
                 sv_geo_bearing_deg(live_lat, live_lon, cast->lat, cast->lon));
    } else {
        snprintf(f[2], sizeof f[2], "Selected  —");
    }

    for (int i = 0; i < 3; i++)
        nk_label_colored(c, f[i], NK_TEXT_LEFT, t->text_dim);
}

static void page_files(SvUi *ui, struct nk_rect r)
{
    struct nk_context *c = ui->ctx;
    const SvState *st = sv_app_state(ui->app);
    bool busy = (st->link == SV_LINK_BUSY);

    nk_layout_row_template_begin(c, S(ui, ROW_H));
    nk_layout_row_template_push_static(c, S(ui, LABEL_W));
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_push_static(c, S(ui, 110));
    nk_layout_row_template_push_static(c, S(ui, 110));
    nk_layout_row_template_end(c);

    nk_label_colored(c, "Directory", NK_TEXT_RIGHT, ui->theme->text_dim);
    nk_label(c, st->cwd[0] ? st->cwd : "\\", NK_TEXT_LEFT);

    if (busy) nk_widget_disable_begin(c);
    if (nk_button_label(c, "Root"))
        sv_app_list_dir(ui->app, "\\");
    if (nk_button_label(c, "Refresh"))
        sv_app_list_dir(ui->app, NULL);
    if (busy) nk_widget_disable_end(c);

    if (st->job == SV_JOB_DOWNLOAD) {
        gap(ui, 4);
        form_row(ui, ROW_H);
        form_label(ui, "Downloading");
        char b[SV_MAX_NAME + 48];
        snprintf(b, sizeof b, "%s - %zu bytes", st->dl_name, st->dl_bytes);
        nk_label_colored(c, b, NK_TEXT_LEFT, ui->theme->warn);
    }

    gap(ui, 6);

    float body_h = r.h - S(ui, 110);
    if (body_h < S(ui, 120))
        body_h = r.h * 0.7f;

    nk_layout_row_dynamic(c, body_h, 1);
    if (nk_group_begin(c, "filelist", NK_WINDOW_BORDER)) {
        nk_layout_row_template_begin(c, S(ui, 22));
        nk_layout_row_template_push_dynamic(c);
        nk_layout_row_template_push_static(c, S(ui, 110));
        nk_layout_row_template_push_static(c, S(ui, 110));
        nk_layout_row_template_end(c);
        nk_label_colored(c, "Name", NK_TEXT_LEFT, ui->theme->text_dim);
        nk_label_colored(c, "Size", NK_TEXT_RIGHT, ui->theme->text_dim);
        nk_spacing(c, 1);

        for (int i = 0; i < st->n_files; i++) {
            const SvFileRow *f = &st->files[i];

            nk_layout_row_template_begin(c, S(ui, 26));
            nk_layout_row_template_push_dynamic(c);
            nk_layout_row_template_push_static(c, S(ui, 110));
            nk_layout_row_template_push_static(c, S(ui, 110));
            nk_layout_row_template_end(c);

            char name[SV_MAX_NAME + 8];
            snprintf(name, sizeof name, "%s%s", f->is_dir ? "[ ] " : "",
                     f->name);
            nk_label(c, name, NK_TEXT_LEFT);

            if (f->is_dir) {
                nk_spacing(c, 1);
            } else {
                char sz[32];
                snprintf(sz, sizeof sz, "%ld", f->size);
                nk_label_colored(c, sz, NK_TEXT_RIGHT, ui->theme->text_dim);
            }

            if (busy) nk_widget_disable_begin(c);
            if (f->is_dir) {
                if (nk_button_label(c, "Open")) {
                    char path[SV_MAX_PATH];
                    snprintf(path, sizeof path, "%.256s\\%.120s",
                             st->cwd[0] ? st->cwd : "", f->name);
                    sv_app_list_dir(ui->app, path);
                }
            } else if (strstr(f->name, ".bin")) {
                if (nk_button_label(c, "Download"))
                    sv_app_download(ui->app, f->name);
            } else {
                nk_spacing(c, 1);
            }
            if (busy) nk_widget_disable_end(c);
        }

        if (st->n_files == 0) {
            nk_layout_row_dynamic(c, S(ui, 40), 1);
            nk_label_colored_wrap(c,
                "Connect to the instrument, then Refresh to list the card.",
                ui->theme->text_faint);
        }
        nk_group_end(c);
    }
}

static void page_settings(SvUi *ui, struct nk_rect r)
{
    struct nk_context *c = ui->ctx;
    const SvState *st = sv_app_state(ui->app);
    bool busy = (st->link == SV_LINK_BUSY);
    (void)r;

    section(ui, "Connection");

    info_row(ui, "Port", st->port[0] ? st->port : "not connected");
    info_row(ui, "State", link_text(st->link));

    gap(ui, 4);
    nk_layout_row_template_begin(c, S(ui, 30));
    nk_layout_row_template_push_static(c, S(ui, LABEL_W));
    nk_layout_row_template_push_static(c, S(ui, 120));
    nk_layout_row_template_push_static(c, S(ui, 120));
    nk_layout_row_template_push_static(c, S(ui, 120));
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_end(c);
    nk_spacing(c, 1);

    if (busy) nk_widget_disable_begin(c);
    if (st->link == SV_LINK_CLOSED) {
        if (primary_button(ui, "Connect...")) {
            ui->n_ports = plat_serial_list(ui->ports, PLAT_MAX_PORTS);
            ui->sel_port = 0;
            ui->dlg_error[0] = ui->dlg_note[0] = '\0';
            ui->dialog = DLG_CONNECT;
        }
        nk_spacing(c, 2);
    } else {
        if (nk_button_label(c, "Disconnect"))
            sv_app_disconnect(ui->app);
        if (nk_button_label(c, "Run mode"))
            sv_app_run_mode(ui->app);
        if (nk_button_label(c, "Interrupt"))
            sv_app_interrupt(ui->app);
    }
    if (busy) nk_widget_disable_end(c);
    nk_spacing(c, 1);

    gap(ui, 10);
    section(ui, "Instrument settings");

    if (!st->device_valid) {
        info_row(ui, "", "Not read from the instrument yet.");
    } else {
        const SvConfig *d = &st->device;
        info_row(ui, "Operating mode",
                 d->operating_mode ? "Smart profile" : "Continuous");
        info_row(ui, "Direction", d->direction ? "Up cast" : "Down cast");
        info_rowf(ui, "Trigger depth", "%.2f m", d->trigger_depth);
        info_rowf(ui, "Depth increment", "%.2f m", d->depth_increment);
        info_rowf(ui, "Trigger step", "%.2f m", d->trigger_step);
        info_row(ui, "Require GPS fix", d->require_fix ? "Yes" : "No");
        info_rowf(ui, "Auto power down",
                  d->auto_power_min >= 9999 ? "disabled" : "%d min",
                  d->auto_power_min);
        info_row(ui, "Bluetooth sleep",
                 d->bt_sleep_enabled ? "Enabled" : "Disabled");
        info_row(ui, "Site", d->site);
    }

    nk_layout_row_template_begin(c, S(ui, 30));
    nk_layout_row_template_push_static(c, S(ui, LABEL_W));
    nk_layout_row_template_push_static(c, S(ui, 120));
    nk_layout_row_template_push_static(c, S(ui, 120));
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_end(c);
    nk_spacing(c, 1);

    bool offline = (st->link == SV_LINK_CLOSED);
    if (busy || offline) nk_widget_disable_begin(c);
    if (nk_button_label(c, "Read"))
        sv_app_read_config(ui->app);
    if (nk_button_label(c, "Edit...")) {
        ui->cfg = st->device;
        snprintf(ui->site_edit, sizeof ui->site_edit, "%s", st->device.site);
        ui->dlg_error[0] = ui->dlg_note[0] = '\0';
        ui->dialog = DLG_SETTINGS;
    }
    if (busy || offline) nk_widget_disable_end(c);
    nk_spacing(c, 1);

    gap(ui, 10);
    section(ui, "Winch depth reporting");

    info_rowf(ui, "Winch", "%s",
              st->winch == SV_WINCH_REACHABLE ? "reachable"
            : st->winch == SV_WINCH_SILENT    ? "no reply to the last probe"
                                              : "not probed");
    info_rowf(ui, "Network adapter", "%s%s%s",
              st->net_if[0] ? st->net_if : "default route",
              st->net_ip[0] ? "  -  " : "",
              st->net_ip[0] ? st->net_ip : "");
    info_row(ui, "Broadcast address",
             st->net_bcast[0] ? st->net_bcast : "255.255.255.255");
    info_row(ui, "Last message sent",
             st->winch_last[0] ? st->winch_last : "none");

    nk_layout_row_template_begin(c, S(ui, 30));
    nk_layout_row_template_push_static(c, S(ui, LABEL_W));
    nk_layout_row_template_push_static(c, S(ui, 150));
    nk_layout_row_template_push_static(c, S(ui, 190));
    nk_layout_row_template_push_static(c, S(ui, 170));
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_end(c);
    nk_spacing(c, 1);

    if (nk_button_label(c, "Probe winch"))
        sv_app_probe_winch(ui->app);

    if (sv_app_cast_count(ui->app) == 0) nk_widget_disable_begin(c);
    if (primary_button(ui, "Report selected cast depth")) {
        const char *err = sv_app_report_depth(ui->app);
        if (err)
            snprintf(ui->dlg_error, sizeof ui->dlg_error, "%s", err);
    }
    if (sv_app_cast_count(ui->app) == 0) nk_widget_disable_end(c);

    if (nk_button_label(c, "Select adapter...")) {
        ui->n_nifs = plat_net_list_ifs(ui->nifs, PLAT_MAX_IFS);
        ui->sel_nif = -1;
        for (int i = 0; i < ui->n_nifs; i++)
            if (st->net_ip[0] && strcmp(ui->nifs[i].ip, st->net_ip) == 0)
                ui->sel_nif = i;
        ui->dlg_error[0] = ui->dlg_note[0] = '\0';
        ui->dialog = DLG_NETWORK;
    }
    nk_spacing(c, 1);
}

static void page_log(SvUi *ui, struct nk_rect r)
{
    struct nk_context *c = ui->ctx;

    int   n     = sv_app_log_count(ui->app);
    float row   = S(ui, 18);
    float pitch = row + c->style.window.spacing.y;
    float view  = r.h - S(ui, 30);

    /*
     * Follow the newest line, but only while the operator is already at the
     * bottom. Scrolling up to read something and having the view yanked back
     * on the next log line makes the page unusable during a download, so a
     * deliberate scroll away from the bottom stops the follow until they
     * return to it.
     */
    nk_uint sx = 0, sy = 0;
    nk_group_get_scroll(c, "log", &sx, &sy);

    float content = (float)n * pitch + c->style.window.group_padding.y * 2.0f;
    float max_off = content - view;
    if (max_off < 0.0f)
        max_off = 0.0f;

    if (n != ui->log_seen) {
        /* Compare against the maximum offset *before* this batch arrived. */
        bool was_at_bottom = (ui->log_max_off <= 0.0f) ||
                             ((float)sy >= ui->log_max_off - pitch);
        if (was_at_bottom)
            sy = (nk_uint)max_off;

        ui->log_seen = n;
    }
    ui->log_max_off = max_off;

    if ((float)sy > max_off)
        sy = (nk_uint)max_off;
    nk_group_set_scroll(c, "log", sx, sy);

    nk_layout_row_dynamic(c, view, 1);
    if (nk_group_begin(c, "log", NK_WINDOW_BORDER)) {
        for (int i = 0; i < n; i++) {
            nk_layout_row_dynamic(c, row, 1);
            const char *line = sv_app_log_line(ui->app, i);
            struct nk_color col = ui->theme->text_dim;
            if (strncmp(line, "error", 5) == 0)  col = ui->theme->alarm;
            else if (line[0] == '>')             col = ui->theme->accent;
            else if (line[0] == '<')             col = ui->theme->ok;
            nk_label_colored(c, line, NK_TEXT_LEFT, col);
        }
        nk_group_end(c);
    }
}

/* ------------------------------------------------------------------ */
/* Dialogs                                                             */
/* ------------------------------------------------------------------ */

typedef enum { DLG_R_NONE = 0, DLG_R_APPLY, DLG_R_OK, DLG_R_CANCEL } DlgResult;

/*
 * The button row every dialog ends with. Right aligned, fixed widths, always
 * the same order — Cancel, Apply, OK — so the primary action is always in
 * the same place regardless of dialog size.
 */
static DlgResult dialog_buttons(SvUi *ui, bool can_apply)
{
    struct nk_context *c = ui->ctx;
    DlgResult res = DLG_R_NONE;

    nk_layout_row_template_begin(c, S(ui, BTN_H));
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_push_static(c, S(ui, BTN_W));
    nk_layout_row_template_push_static(c, S(ui, BTN_W));
    nk_layout_row_template_push_static(c, S(ui, BTN_W));
    nk_layout_row_template_end(c);

    nk_spacing(c, 1);

    if (nk_button_label(c, "Cancel"))
        res = DLG_R_CANCEL;

    if (!can_apply) nk_widget_disable_begin(c);
    if (nk_button_label(c, "Apply") && can_apply)
        res = DLG_R_APPLY;
    if (!can_apply) nk_widget_disable_end(c);

    if (primary_button(ui, "OK"))
        res = DLG_R_OK;

    return res;
}

static void dialog_message(SvUi *ui)
{
    if (ui->dlg_error[0]) {
        nk_layout_row_dynamic(ui->ctx, S(ui, 34), 1);
        nk_label_colored_wrap(ui->ctx, ui->dlg_error, ui->theme->alarm);
    } else if (ui->dlg_note[0]) {
        nk_layout_row_dynamic(ui->ctx, S(ui, 34), 1);
        nk_label_colored_wrap(ui->ctx, ui->dlg_note, ui->theme->ok);
    }
}

static void dlg_connect_body(SvUi *ui)
{
    struct nk_context *c = ui->ctx;

    nk_layout_row_dynamic(c, S(ui, 40), 1);
    nk_label_colored_wrap(c,
        "Both the USB cable and the Valeport Bluetooth key appear as serial "
        "ports. 230400 baud, 8N1.", ui->theme->text_dim);

    gap(ui, 6);

    nk_layout_row_dynamic(c,
        list_height(ui, ui->n_ports, 26.0f, 0.0f, 74.0f, 300.0f), 1);
    if (nk_group_begin(c, "ports", NK_WINDOW_BORDER)) {
        for (int i = 0; i < ui->n_ports; i++) {
            nk_layout_row_dynamic(c, S(ui, 26), 1);
            nk_bool on = (i == ui->sel_port);
            char lab[300];
            snprintf(lab, sizeof lab, "%s   —   %s",
                     ui->ports[i].path, ui->ports[i].label);
            if (nk_selectable_label(c, lab, NK_TEXT_LEFT, &on) && on)
                ui->sel_port = i;
        }
        if (ui->n_ports == 0) {
            nk_layout_row_dynamic(c, S(ui, 40), 1);
            nk_label_colored_wrap(c,
                "No serial ports found. Plug in the cable or the Bluetooth "
                "key, then press Rescan.", ui->theme->warn);
        }
        nk_group_end(c);
    }

    nk_layout_row_template_begin(c, S(ui, 30));
    nk_layout_row_template_push_static(c, S(ui, 110));
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_end(c);
    if (nk_button_label(c, "Rescan")) {
        ui->n_ports = plat_serial_list(ui->ports, PLAT_MAX_PORTS);
        ui->sel_port = 0;
    }
    nk_spacing(c, 1);

}

static bool dlg_connect_ready(SvUi *ui) { return ui->n_ports > 0; }

static void dlg_connect_commit(SvUi *ui, DlgResult r)
{
    if (r == DLG_R_CANCEL) {
        ui->dialog = DLG_NONE;
    } else if (r == DLG_R_APPLY || r == DLG_R_OK) {
        if (ui->n_ports == 0) {
            snprintf(ui->dlg_error, sizeof ui->dlg_error,
                     "No port selected.");
            return;
        }
        bool ok = sv_app_connect(ui->app, ui->ports[ui->sel_port].path);
        if (!ok) {
            snprintf(ui->dlg_error, sizeof ui->dlg_error, "%s",
                     sv_app_state(ui->app)->error);
        } else {
            ui->dlg_error[0] = '\0';
            snprintf(ui->dlg_note, sizeof ui->dlg_note, "Connected.");
            if (r == DLG_R_OK)
                ui->dialog = DLG_NONE;
        }
    }
}

static void dlg_settings_body(SvUi *ui)
{
    struct nk_context *c = ui->ctx;

    section(ui, "Operation");

    form_row(ui, ROW_H);
    form_label(ui, "Operating mode");
    static const char *modes[] = { "Continuous", "Smart profile" };
    ui->cfg.operating_mode = nk_combo(c, modes, 2, ui->cfg.operating_mode,
                                      (int)S(ui, 22),
                                      nk_vec2(S(ui, 220), S(ui, 120)));

    form_row(ui, ROW_H);
    form_label(ui, "Profile direction");
    static const char *dirs[] = { "Down cast", "Up cast" };
    ui->cfg.direction = nk_combo(c, dirs, 2, ui->cfg.direction,
                                 (int)S(ui, 22),
                                 nk_vec2(S(ui, 220), S(ui, 120)));

    gap(ui, 6);
    section(ui, "Smart profile");

    form_row(ui, ROW_H);
    form_label(ui, "Trigger depth  m");
    nk_property_double(c, "#", 0.1, &ui->cfg.trigger_depth, 100.0, 0.1,
                       (float)0.05);

    form_row(ui, ROW_H);
    form_label(ui, "Depth increment  m");
    nk_property_double(c, "#", 0.1, &ui->cfg.depth_increment, 100.0, 0.1,
                       (float)0.05);

    form_row(ui, ROW_H);
    form_label(ui, "Trigger step  m");
    nk_property_double(c, "#", 0.5, &ui->cfg.trigger_step, 100.0, 0.1,
                       (float)0.05);

    nk_layout_row_template_begin(c, S(ui, 34));
    nk_layout_row_template_push_static(c, S(ui, LABEL_W));
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_end(c);
    nk_spacing(c, 1);
    nk_label_colored_wrap(c,
        "The trigger step must exceed the local sea and swell, or the file "
        "closes early.", ui->theme->text_faint);

    gap(ui, 6);
    section(ui, "Power and comms");

    form_row(ui, ROW_H);
    form_label(ui, "Require GPS fix");
    nk_bool fix = ui->cfg.require_fix != 0;
    nk_checkbox_label(c, "before logging in continuous mode", &fix);
    ui->cfg.require_fix = fix;

    form_row(ui, ROW_H);
    form_label(ui, "Bluetooth sleep");
    nk_bool sl = ui->cfg.bt_sleep_enabled != 0;
    nk_checkbox_label(c, "allow wake from sleep over Bluetooth", &sl);
    ui->cfg.bt_sleep_enabled = sl;

    form_row(ui, ROW_H);
    form_label(ui, "Auto power down  min");
    nk_property_int(c, "#", 1, &ui->cfg.auto_power_min, 9999, 1, 1.0f);

    form_row(ui, ROW_H);
    form_label(ui, "");
    nk_label_colored(c,
        ui->cfg.auto_power_min >= 9999 ? "9999 disables auto power down"
                                       : "",
        NK_TEXT_LEFT, ui->theme->text_faint);

    gap(ui, 6);
    section(ui, "Site");

    form_row(ui, ROW_H);
    form_label(ui, "Site information");
    nk_edit_string_zero_terminated(c, NK_EDIT_FIELD, ui->site_edit,
                                   SV_SITE_LEN - 1, nk_filter_default);

    snprintf(ui->cfg.site, sizeof ui->cfg.site, "%s", ui->site_edit);

}

static bool dlg_settings_ready(SvUi *ui)
{
    const SvState *st = sv_app_state(ui->app);
    return st->device_valid ? !sv_config_equal(&ui->cfg, &st->device) : true;
}

static void dlg_settings_commit(SvUi *ui, DlgResult r)
{
    if (r == DLG_R_CANCEL) {
        ui->dialog = DLG_NONE;
    } else if (r == DLG_R_APPLY || r == DLG_R_OK) {
        sv_app_write_config(ui->app, &ui->cfg);
        snprintf(ui->dlg_note, sizeof ui->dlg_note,
                 "Written, and read back to verify.");
        if (r == DLG_R_OK)
            ui->dialog = DLG_NONE;
    }
}

static void dlg_export_body(SvUi *ui)
{
    struct nk_context *c = ui->ctx;
    const SvCast *cast = sv_app_cast(ui->app, sv_app_selected(ui->app));

    if (!cast) {
        nk_layout_row_dynamic(c, S(ui, 30), 1);
        nk_label_colored(c, "No cast selected.", NK_TEXT_LEFT,
                         ui->theme->warn);
        return;
    }

    section(ui, "Format");

    form_row(ui, ROW_H);
    form_label(ui, "Export as");
    const char *names[SV_EXPORT_COUNT];
    for (int i = 0; i < SV_EXPORT_COUNT; i++)
        names[i] = sv_export_name((SvExportFormat)i);
    int was = ui->export_fmt;
    ui->export_fmt = nk_combo(c, names, SV_EXPORT_COUNT, ui->export_fmt,
                              (int)S(ui, 22), nk_vec2(S(ui, 260), S(ui, 180)));
    if (ui->export_fmt != was)
        sv_export_filename(ui->export_name, sizeof ui->export_name,
                           "%s_%d%t.%e", cast,
                           (SvExportFormat)ui->export_fmt);

    gap(ui, 6);
    section(ui, "Destination");

    form_row(ui, ROW_H);
    form_label(ui, "Folder");
    nk_edit_string_zero_terminated(c, NK_EDIT_FIELD, ui->export_dir,
                                   sizeof ui->export_dir - 1,
                                   nk_filter_default);

    form_row(ui, ROW_H);
    form_label(ui, "File name");
    nk_edit_string_zero_terminated(c, NK_EDIT_FIELD, ui->export_name,
                                   sizeof ui->export_name - 1,
                                   nk_filter_default);

    form_row(ui, ROW_H);
    form_label(ui, "Cast");
    char b[280];
    snprintf(b, sizeof b, "%s - %d samples to %.2f m",
             cast->name, cast->n, cast->max_depth);
    nk_label_colored(c, b, NK_TEXT_LEFT, ui->theme->text_dim);

}

static bool dlg_export_ready(SvUi *ui)
{
    return ui->export_dir[0] && ui->export_name[0]
        && sv_app_cast(ui->app, sv_app_selected(ui->app)) != NULL;
}

static void dlg_export_commit(SvUi *ui, DlgResult r)
{
    bool ready = dlg_export_ready(ui);
    if (r == DLG_R_CANCEL) {
        ui->dialog = DLG_NONE;
    } else if ((r == DLG_R_APPLY || r == DLG_R_OK) && ready) {
        char path[SV_MAX_PATH];
        if (!plat_path_join(path, sizeof path, ui->export_dir,
                            ui->export_name)) {
            snprintf(ui->dlg_error, sizeof ui->dlg_error, "Path too long.");
            return;
        }

        const char *err = sv_app_export(ui->app, sv_app_selected(ui->app),
                                        (SvExportFormat)ui->export_fmt, path);
        if (err) {
            snprintf(ui->dlg_error, sizeof ui->dlg_error, "%s", err);
        } else {
            ui->dlg_error[0] = '\0';
            snprintf(ui->dlg_note, sizeof ui->dlg_note, "Written to %.120s",
                     path);
            if (r == DLG_R_OK)
                ui->dialog = DLG_NONE;
        }
    } else if (r == DLG_R_OK) {
        snprintf(ui->dlg_error, sizeof ui->dlg_error,
                 "A folder and a file name are needed.");
    }
}

static void dlg_process_body(SvUi *ui)
{
    struct nk_context *c = ui->ctx;
    const SvCast *cast = sv_app_cast(ui->app, sv_app_selected(ui->app));

    nk_layout_row_dynamic(c, S(ui, 34), 1);
    nk_label_colored_wrap(c,
        "Processing is applied to the selected cast in place. Reload the "
        "file to get the raw profile back.", ui->theme->text_dim);

    gap(ui, 4);
    section(ui, "Steps");

    form_row(ui, ROW_H);
    form_label(ui, "Trim");
    nk_bool dc = ui->proc_downcast != 0;
    nk_checkbox_label(c, "keep the down cast only", &dc);
    ui->proc_downcast = dc;

    form_row(ui, ROW_H);
    form_label(ui, "Despike");
    nk_bool ds = ui->proc_despike != 0;
    nk_checkbox_label(c, "remove samples over 3 m/s off the local median",
                      &ds);
    ui->proc_despike = ds;

    form_row(ui, ROW_H);
    form_label(ui, "Depth bin  m");
    nk_property_float(c, "#", 0.0f, &ui->proc_bin, 50.0f, 0.1f, 0.05f);

    form_row(ui, ROW_H);
    form_label(ui, "Thin to  points");
    nk_property_int(c, "#", 0, &ui->proc_thin_to, 20000, 10, 5.0f);

    form_row(ui, ROW_H);
    form_label(ui, "");
    nk_label_colored(c, "0 disables that step", NK_TEXT_LEFT,
                     ui->theme->text_faint);

    if (cast) {
        form_row(ui, ROW_H);
        form_label(ui, "Current");
        char b[128];
        snprintf(b, sizeof b, "%d samples, %.2f m", cast->n, cast->max_depth);
        nk_label_colored(c, b, NK_TEXT_LEFT, ui->theme->text_dim);
    }

}

static bool dlg_process_ready(SvUi *ui)
{
    return sv_app_cast(ui->app, sv_app_selected(ui->app)) != NULL;
}

static void dlg_process_commit(SvUi *ui, DlgResult r)
{
    const SvCast *cast = sv_app_cast(ui->app, sv_app_selected(ui->app));
    if (r == DLG_R_CANCEL) {
        ui->dialog = DLG_NONE;
    } else if ((r == DLG_R_APPLY || r == DLG_R_OK) && cast) {
        sv_app_process(ui->app, ui->proc_downcast, ui->proc_despike,
                       ui->proc_bin, ui->proc_thin_to);
        const SvCast *now = sv_app_cast(ui->app, sv_app_selected(ui->app));
        snprintf(ui->dlg_note, sizeof ui->dlg_note,
                 "Now %d samples.", now ? now->n : 0);
        if (r == DLG_R_OK)
            ui->dialog = DLG_NONE;
    }
}

static void dlg_network_body(SvUi *ui)
{
    struct nk_context *c = ui->ctx;
    const SvState *st = sv_app_state(ui->app);

    nk_layout_row_dynamic(c, S(ui, 46), 1);
    nk_label_colored_wrap(c,
        "Depth reports are broadcast to the winch on UDP 8090. On a machine "
        "with more than one adapter, pick the one on the survey network - "
        "otherwise the report goes out of the wrong port and the winch never "
        "hears it. Only connected adapters are listed; plug in and rescan.",
        ui->theme->text_dim);

    gap(ui, 4);
    section(ui, "Adapters");

    nk_layout_row_dynamic(c,
        list_height(ui, ui->n_nifs, 24.0f, 30.0f, 84.0f, 300.0f), 1);
    if (nk_group_begin(c, "nifs", NK_WINDOW_BORDER)) {
        nk_layout_row_template_begin(c, S(ui, 22));
        nk_layout_row_template_push_static(c, S(ui, 140));
        nk_layout_row_template_push_static(c, S(ui, 130));
        nk_layout_row_template_push_static(c, S(ui, 130));
        nk_layout_row_template_push_dynamic(c);
        nk_layout_row_template_end(c);
        nk_label_colored(c, "Adapter",   NK_TEXT_LEFT, ui->theme->text_dim);
        nk_label_colored(c, "Address",   NK_TEXT_LEFT, ui->theme->text_dim);
        nk_label_colored(c, "Mask",      NK_TEXT_LEFT, ui->theme->text_dim);
        nk_label_colored(c, "Broadcast", NK_TEXT_LEFT, ui->theme->text_dim);

        for (int i = 0; i < ui->n_nifs; i++) {
            const PlatNetIf *n = &ui->nifs[i];

            nk_layout_row_dynamic(c, S(ui, 24), 1);
            nk_bool on = (i == ui->sel_nif);
            char lab[256];
            snprintf(lab, sizeof lab, "%-14s  %-15s  %-15s  %s",
                     n->name, n->ip, n->mask, n->bcast);
            if (nk_selectable_label(c, lab, NK_TEXT_LEFT, &on) && on)
                ui->sel_nif = i;
        }

        if (ui->n_nifs == 0) {
            nk_layout_row_dynamic(c, S(ui, 36), 1);
            nk_label_colored_wrap(c,
                "No connected adapters found. Plug into the survey network "
                "and press Rescan.", ui->theme->warn);
        }
        nk_group_end(c);
    }

    nk_layout_row_template_begin(c, S(ui, 26));
    nk_layout_row_template_push_static(c, S(ui, 110));
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_end(c);
    if (nk_button_label(c, "Rescan")) {
        ui->n_nifs = plat_net_list_ifs(ui->nifs, PLAT_MAX_IFS);
        if (ui->sel_nif >= ui->n_nifs)
            ui->sel_nif = -1;
    }
    nk_bool def = (ui->sel_nif < 0);
    nk_checkbox_label(c, "use the default route instead", &def);
    if (def) ui->sel_nif = -1;
    else if (ui->sel_nif < 0 && ui->n_nifs > 0) ui->sel_nif = 0;

    gap(ui, 4);
    info_row(ui, "Currently sending on",
             st->net_if[0] ? st->net_if : "default route");
    info_row(ui, "Broadcast address",
             st->net_bcast[0] ? st->net_bcast : "255.255.255.255");
}

static bool dlg_network_ready(SvUi *ui)
{
    return ui->sel_nif < 0 || ui->sel_nif < ui->n_nifs;
}

static void dlg_network_commit(SvUi *ui, DlgResult r)
{
    if (r == DLG_R_CANCEL) {
        ui->dialog = DLG_NONE;
        return;
    }
    if (r != DLG_R_APPLY && r != DLG_R_OK)
        return;

    const PlatNetIf *n = (ui->sel_nif >= 0 && ui->sel_nif < ui->n_nifs)
                       ? &ui->nifs[ui->sel_nif] : NULL;

    const char *err = sv_app_set_interface(ui->app, n);
    if (err) {
        snprintf(ui->dlg_error, sizeof ui->dlg_error, "%s", err);
        return;
    }

    ui->dlg_error[0] = '\0';
    snprintf(ui->dlg_note, sizeof ui->dlg_note, "Broadcasting to %s",
             n ? n->bcast : "255.255.255.255");
    if (r == DLG_R_OK)
        ui->dialog = DLG_NONE;
}

static void dlg_about_body(SvUi *ui)
{
    struct nk_context *c = ui->ctx;
    const SvState *st = sv_app_state(ui->app);

    nk_layout_row_dynamic(c, S(ui, 26), 1);
    nk_label_colored(c, SVPVIEW_NAME "  " SVPVIEW_VERSION, NK_TEXT_LEFT,
                     ui->theme->accent);

    nk_layout_row_dynamic(c, S(ui, 20), 1);
    nk_label_colored(c, SVPVIEW_TAGLINE, NK_TEXT_LEFT, ui->theme->text);

    gap(ui, 6);
    nk_layout_row_dynamic(c, S(ui, 46), 1);
    nk_label_colored_wrap(c,
        "Configures a Valeport SWiFT SVP, CTD or SWiFTplus, downloads and "
        "plots its casts, exports to the survey formats, and reports each "
        "cast's achieved depth to a C-MAX Vigo winch. It replaces Valeport "
        "Ocean and VigoDepthRelay.", ui->theme->text_dim);

    gap(ui, 6);
    section(ui, "This build");

    info_row(ui, "Version", SVPVIEW_VERSION);
    info_row(ui, "Built", __DATE__ " " __TIME__);

    SDL_version linked;
    SDL_GetVersion(&linked);
    info_rowf(ui, "SDL", "%d.%d.%d at runtime, built against %d.%d.%d",
              linked.major, linked.minor, linked.patch,
              SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
    info_row(ui, "Interface", "Nuklear, vendored in third_party/");
    info_rowf(ui, "UI scale", "%.2f  (override with SVPVIEW_SCALE)",
              (double)ui->scale);

    gap(ui, 6);
    section(ui, "Instrument");

    info_row(ui, "Port", st->port[0] ? st->port : "not connected");
    info_row(ui, "Serial number", st->serial[0] ? st->serial : "-");
    info_row(ui, "Firmware", st->firmware[0] ? st->firmware : "-");

    gap(ui, 6);
    section(ui, "Reference");

    nk_layout_row_dynamic(c, S(ui, 32), 1);
    nk_label_colored_wrap(c,
        "Protocol: SWiFT Integration Guide MANUAL-68251662-19 issue 2.1. "
        "Winch: UDP 8090, see docs/VIGO_INTERFACE.md.", ui->theme->text_faint);
}

static const char *dialog_title(SvDialog d)
{
    switch (d) {
    case DLG_CONNECT:  return "Connect to instrument";
    case DLG_SETTINGS: return "Instrument settings";
    case DLG_EXPORT:   return "Export profile";
    case DLG_PROCESS:  return "Process profile";
    case DLG_NETWORK:  return "Network adapter";
    case DLG_ABOUT:    return "About " SVPVIEW_NAME;
    default:           return "";
    }
}

/*
 * Width is a design decision per dialog; height is only the value used for
 * the very first frame, before the body has been measured. After that the
 * dialog is sized to its content — see draw_dialog().
 */
static struct nk_vec2 dialog_size(SvDialog d)
{
    switch (d) {
    case DLG_CONNECT:  return nk_vec2(560, 380);
    case DLG_SETTINGS: return nk_vec2(660, 600);
    case DLG_EXPORT:   return nk_vec2(620, 380);
    case DLG_PROCESS:  return nk_vec2(620, 400);
    case DLG_NETWORK:  return nk_vec2(760, 440);
    case DLG_ABOUT:    return nk_vec2(620, 520);
    default:           return nk_vec2(500, 300);
    }
}

static void draw_dialog(SvUi *ui, int w, int h)
{
    struct nk_context *c = ui->ctx;
    const SvTheme *t = ui->theme;

    /* Scrim: covers the page, dims it, and takes every click that is not on
     * the dialog — which is what makes the dialog modal. */
    nk_style_push_style_item(c, &c->style.window.fixed_background,
                             nk_style_item_color(t->scrim));
    if (nk_begin(c, "scrim", nk_rect(0, 0, (float)w, (float)h),
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT)) {
    }
    nk_end(c);
    nk_style_pop_style_item(c);

    struct nk_vec2 sz = dialog_size(ui->dialog);
    float dw = S(ui, sz.x), dh = S(ui, sz.y);

    /*
     * Height comes from the content once it has been measured, so a dialog
     * is never taller than it needs to be and never hides its own controls
     * behind a scrollbar. The measurement is taken while drawing, so it is
     * one frame behind: the first frame uses the fallback above and every
     * frame after uses the real figure. A scrollbar still appears if the
     * content genuinely cannot fit the window.
     */
    if (ui->dlg_measured == ui->dialog && ui->dlg_natural_h > 0.0f)
        dh = ui->dlg_natural_h;

    if (dw > w - S(ui, 40)) dw = w - S(ui, 40);
    if (dh > h - S(ui, 40)) dh = h - S(ui, 40);

    struct nk_rect r = nk_rect((w - dw) / 2, (h - dh) / 2, dw, dh);

    nk_style_push_float(c, &c->style.window.border, 1.0f);
    nk_style_push_color(c, &c->style.window.border_color, t->border);

    /*
     * nk_begin does not resize a window that already exists and is movable,
     * so the measured size has to be pushed in explicitly — and before
     * nk_begin, both because Nuklear asserts against setting the bounds of
     * the window it is currently processing, and because doing it afterwards
     * leaves the previous frame's border drawn at the old size.
     */
    nk_window_set_bounds(c, "dialog", r);

    if (nk_begin_titled(c, "dialog", dialog_title(ui->dialog), r,
                        NK_WINDOW_BORDER | NK_WINDOW_TITLE |
                        NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE |
                        NK_WINDOW_NO_SCROLLBAR)) {
        nk_window_set_focus(c, "dialog");

        /*
         * The button row is pinned: the body scrolls inside a group sized to
         * whatever is left over, so Cancel/Apply/OK are on screen at every
         * dialog size and UI scale. Letting them flow with the content put
         * them below the fold on the settings dialog.
         */
        /* Reserve the button row, the optional message row, the spacing
         * between each of them and the window's bottom padding. Getting this
         * short by even a few pixels clips the buttons, so it is worked out
         * from the style rather than guessed. */
        float spacing = c->style.window.spacing.y;
        float pad_y   = c->style.window.padding.y;
        float msg_h   = (ui->dlg_error[0] || ui->dlg_note[0])
                      ? S(ui, 38) + spacing : 0.0f;

        struct nk_rect region = nk_window_get_content_region(c);
        float body_h = region.h - S(ui, BTN_H) - spacing - msg_h - pad_y
                     - S(ui, DLG_BOTTOM_MARGIN);
        if (body_h < S(ui, 60))
            body_h = S(ui, 60);

        /* Chrome must be measured out here: inside the group,
         * nk_window_get_content_region() reports the *group's* region, not
         * the dialog's, which silently inflated this by 70 px. */
        float chrome = nk_window_get_bounds(c).h - region.h;
        float used = 0.0f;

        nk_layout_row_dynamic(c, body_h, 1);
        if (nk_group_begin(c, "dlgbody", 0)) {
            struct nk_rect top = nk_layout_widget_bounds(c);

            switch (ui->dialog) {
            case DLG_CONNECT:  dlg_connect_body(ui);  break;
            case DLG_SETTINGS: dlg_settings_body(ui); break;
            case DLG_EXPORT:   dlg_export_body(ui);   break;
            case DLG_PROCESS:  dlg_process_body(ui);  break;
            case DLG_NETWORK:  dlg_network_body(ui);  break;
            case DLG_ABOUT:    dlg_about_body(ui);    break;
            default: break;
            }

            /*
             * Opening a zero-height row advances the layout cursor past the
             * last real row without adding anything to the content: reading
             * the position without it can return a slot still inside the
             * final row, leaving the dialog one row short with its last
             * control clipped. Nothing is drawn into the row — a widget here
             * would count as content and bring back a scrollbar. The row's
             * leading gap is not content either, so it comes off again.
             */
            nk_layout_row_dynamic(c, 0.0f, 1);
            used = nk_layout_widget_bounds(c).y - top.y
                 - c->style.window.spacing.y;

            nk_group_end(c);
        }

        if (used > 0.0f) {
            float reserve = region.h - body_h;
            ui->dlg_natural_h = chrome + reserve + used
                              + c->style.window.group_padding.y * 2.0f;
            ui->dlg_measured = ui->dialog;
        }

        dialog_message(ui);

        bool ready = false;
        switch (ui->dialog) {
        case DLG_CONNECT:  ready = dlg_connect_ready(ui);  break;
        case DLG_SETTINGS: ready = dlg_settings_ready(ui); break;
        case DLG_EXPORT:   ready = dlg_export_ready(ui);   break;
        case DLG_PROCESS:  ready = dlg_process_ready(ui);  break;
        default: break;
        }

        DlgResult res;
        if (ui->dialog == DLG_ABOUT) {
            /* One button, because Apply and Cancel have nothing to act on in
             * a dialog that changes nothing. */
            nk_layout_row_template_begin(c, S(ui, BTN_H));
            nk_layout_row_template_push_dynamic(c);
            nk_layout_row_template_push_static(c, S(ui, BTN_W));
            nk_layout_row_template_end(c);
            nk_spacing(c, 1);
            res = primary_button(ui, "OK") ? DLG_R_OK : DLG_R_NONE;
            if (res == DLG_R_OK)
                ui->dialog = DLG_NONE;
        } else {
            res = dialog_buttons(ui, ready);
        }
        if (res != DLG_R_NONE) {
            switch (ui->dialog) {
            case DLG_CONNECT:  dlg_connect_commit(ui, res);  break;
            case DLG_SETTINGS: dlg_settings_commit(ui, res); break;
            case DLG_EXPORT:   dlg_export_commit(ui, res);   break;
            case DLG_PROCESS:  dlg_process_commit(ui, res);  break;
            case DLG_NETWORK:  dlg_network_commit(ui, res);  break;
            default: break;
            }
        }
    } else {
        /* Closed with the title-bar control: same as Cancel. */
        ui->dialog = DLG_NONE;
    }
    nk_end(c);

    nk_style_pop_color(c);
    nk_style_pop_float(c);
}

/* ------------------------------------------------------------------ */
/* Shell                                                               */
/* ------------------------------------------------------------------ */

/*
 * Keep the window-manager title current: the version so a screenshot or a
 * support call identifies the build, and the instrument so an operator with
 * two profilers open can tell the windows apart from the taskbar.
 */
static void update_title(SvUi *ui)
{
    const SvState *st = sv_app_state(ui->app);
    char want[192];

    if (st->serial[0])
        snprintf(want, sizeof want, "%s %s  -  SWiFT %.32s on %.80s",
                 SVPVIEW_NAME, SVPVIEW_VERSION, st->serial, st->port);
    else if (st->port[0])
        snprintf(want, sizeof want, "%s %s  -  %.120s",
                 SVPVIEW_NAME, SVPVIEW_VERSION, st->port);
    else
        snprintf(want, sizeof want, "%s %s  -  %s",
                 SVPVIEW_NAME, SVPVIEW_VERSION, SVPVIEW_TAGLINE);

    if (strcmp(want, ui->title) != 0) {
        snprintf(ui->title, sizeof ui->title, "%s", want);
        SDL_SetWindowTitle(ui->win, ui->title);
    }
}

void sv_ui_frame(SvUi *ui, int w, int h)
{
    struct nk_context *c = ui->ctx;

    update_title(ui);

    float sh = S(ui, STATUS_H);
    float ah = S(ui, ACTION_H);
    float rw = S(ui, RAIL_W);
    float body_h = (float)h - sh - ah;
    if (body_h < 100.0f)
        body_h = 100.0f;

    draw_status(ui, nk_rect(0, 0, (float)w, sh));
    draw_rail(ui, nk_rect(0, sh, rw, body_h));
    draw_action(ui, nk_rect(0, (float)h - ah, (float)w, ah));

    struct nk_rect content = nk_rect(rw, sh, (float)w - rw, body_h);

    nk_style_push_style_item(c, &c->style.window.fixed_background,
                             nk_style_item_color(ui->theme->bg));
    if (nk_begin(c, "content", content,
                 (ui->page == PAGE_SETTINGS) ? 0 : NK_WINDOW_NO_SCROLLBAR)) {
        struct nk_rect inner = nk_window_get_content_region(c);
        switch (ui->page) {
        case PAGE_LIVE:     page_live(ui, inner);     break;
        case PAGE_PROFILE:  page_profile(ui, inner);  break;
        case PAGE_CHART:    page_chart(ui, inner);    break;
        case PAGE_FILES:    page_files(ui, inner);    break;
        case PAGE_SETTINGS: page_settings(ui, inner); break;
        case PAGE_LOG:      page_log(ui, inner);      break;
        default: break;
        }
    }
    nk_end(c);
    nk_style_pop_style_item(c);

    if (ui->dialog != DLG_NONE)
        draw_dialog(ui, w, h);
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static float detect_scale(SDL_Window *win, SDL_Renderer *ren)
{
    const char *env = getenv("SVPVIEW_SCALE");
    if (env && atof(env) > 0.1)
        return (float)atof(env);

    int ww = 0, wh = 0, dw = 0, dh = 0;
    SDL_GetWindowSize(win, &ww, &wh);
    SDL_GetRendererOutputSize(ren, &dw, &dh);

    float scale = (ww > 0) ? (float)dw / (float)ww : 1.0f;

    float ddpi = 0;
    if (SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(win), &ddpi, NULL, NULL) == 0) {
        float dpi_scale = ddpi / 96.0f;
        if (dpi_scale > scale)
            scale = dpi_scale;
    }

    if (scale < 1.0f) scale = 1.0f;
    if (scale > 4.0f) scale = 4.0f;
    return scale;
}

SvUi *sv_ui_create(SDL_Window *win, SDL_Renderer *ren, SvApp *app)
{
    SvUi *ui = calloc(1, sizeof *ui);
    if (!ui)
        return NULL;

    ui->win = win;
    ui->ren = ren;
    ui->app = app;
    ui->dark = true;
    ui->theme = &SV_THEME_DARK;
    ui->scale = detect_scale(win, ren);
    ui->page = PAGE_PROFILE;
    ui->traces = SV_TRACE_SV | SV_TRACE_TEMP;
    ui->chart.labels = true;
    ui->proc_downcast = 1;
    ui->proc_despike = 1;
    ui->export_fmt = SV_EXPORT_ASVP;
    ui->sel_nif = -1;

    if (!plat_config_dir(ui->export_dir, sizeof ui->export_dir))
        snprintf(ui->export_dir, sizeof ui->export_dir, ".");

    ui->ctx = nk_sdl_init(win, ren);
    if (!ui->ctx) {
        free(ui);
        return NULL;
    }

    /* One font atlas, baked at physical pixel size so text stays crisp on a
     * HiDPI display instead of being scaled up from 1x. */
    struct nk_font_atlas *atlas = NULL;
    nk_sdl_font_stash_begin(&atlas);

    /* Glyph range: Nuklear's default is bare ASCII, which turns every
     * en dash, degree sign and middle dot in the interface into '?'. */
    static const nk_rune ranges[] = {
        0x0020, 0x00FF,          /* Latin-1: degree, middle dot, accents */
        0x2010, 0x2027,          /* dashes, quotes, bullets              */
        0
    };

    struct nk_font_config cfg = nk_font_config(14.0f * ui->scale);
    cfg.range = ranges;
    cfg.oversample_h = 2;
    cfg.oversample_v = 1;
    cfg.pixel_snap = 0;

    struct nk_font *font = NULL;
    static const char *candidates[] = {
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/Library/Fonts/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "C:\\Windows\\Fonts\\segoeui.ttf",
        NULL
    };
    for (int i = 0; candidates[i] && !font; i++)
        font = nk_font_atlas_add_from_file(atlas, candidates[i],
                                           14.0f * ui->scale, &cfg);
    nk_sdl_font_stash_end();

    if (font)
        nk_style_set_font(ui->ctx, &font->handle);

    sv_theme_apply(ui->ctx, ui->theme, ui->scale);
    return ui;
}

void sv_ui_fit_window(SvUi *ui, int base_w, int base_h)
{
    if (!ui)
        return;

    /* Every metric in the layout is written at scale 1 and multiplied by
     * ui->scale as it is drawn, so a window left at the base size leaves the
     * interface only base/scale of usable room — half of it on a 192 dpi
     * panel, which is what "opens very small" looks like. Ask for
     * base * scale physical pixels.
     *
     * SDL_SetWindowSize speaks window units: pixels on X11 and Windows, but
     * points on a Retina Mac, where the window system has already applied the
     * factor. Dividing by the drawable/window ratio covers both — on the Mac
     * it cancels the scale back out and the window stays base-sized. */
    int ww = 0, wh = 0, dw = 0, dh = 0;
    SDL_GetWindowSize(ui->win, &ww, &wh);
    SDL_GetRendererOutputSize(ui->ren, &dw, &dh);
    float px_per_unit = (ww > 0 && dw > 0) ? (float)dw / (float)ww : 1.0f;

    float unit_scale = ui->scale / px_per_unit;
    float w = (float)base_w * unit_scale;
    float h = (float)base_h * unit_scale;

    /* Keep inside the area the desktop actually leaves free, so the window
     * doesn't open with its action bar behind a panel or off the bottom of the
     * screen. One shrink factor for both axes, so the proportions hold. */
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(ui->win),
                                   &usable) == 0 &&
        usable.w > 0 && usable.h > 0) {
        float max_w = (float)usable.w * WIN_MAX_FRAC;
        float max_h = (float)usable.h * WIN_MAX_FRAC;
        float shrink = 1.0f;
        if (w > max_w)
            shrink = max_w / w;
        if (h > max_h && max_h / h < shrink)
            shrink = max_h / h;
        w *= shrink;
        h *= shrink;
    }

    int min_w = (int)((float)WIN_MIN_W * unit_scale);
    int min_h = (int)((float)WIN_MIN_H * unit_scale);
    SDL_SetWindowMinimumSize(ui->win, min_w, min_h);

    if (w < (float)min_w) w = (float)min_w;
    if (h < (float)min_h) h = (float)min_h;

    SDL_SetWindowSize(ui->win, (int)w, (int)h);
    SDL_SetWindowPosition(ui->win, SDL_WINDOWPOS_CENTERED,
                          SDL_WINDOWPOS_CENTERED);
}

void sv_ui_destroy(SvUi *ui)
{
    if (!ui)
        return;
    nk_sdl_shutdown();
    free(ui);
}

void sv_ui_input_begin(SvUi *ui) { nk_input_begin(ui->ctx); }
void sv_ui_input_end(SvUi *ui)   { nk_input_end(ui->ctx); }

bool sv_ui_handle_event(SvUi *ui, SDL_Event *e)
{
    if (e->type == SDL_QUIT) {
        ui->quit = true;
        return true;
    }
    if (e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_ESCAPE &&
        ui->dialog != DLG_NONE) {
        ui->dialog = DLG_NONE;     /* Escape is Cancel */
        return true;
    }
    return nk_sdl_handle_event(e) != 0;
}

void sv_ui_render(SvUi *ui)
{
    nk_sdl_render(NK_ANTI_ALIASING_ON);
}

void sv_ui_clear_colour(const SvUi *ui, Uint8 *r, Uint8 *g, Uint8 *b)
{
    *r = ui->theme->bg.r;
    *g = ui->theme->bg.g;
    *b = ui->theme->bg.b;
}

bool sv_ui_quit_requested(const SvUi *ui) { return ui->quit; }
