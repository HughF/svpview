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
#include "sv_profile.h"
#include "sv_ocean.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

void sv_profile_derive(SvCast *c, double lat)
{
    if (!c || c->n <= 0)
        return;

    for (int i = 0; i < c->n; i++) {
        SvSample *s = &c->s[i];

        s->depth = sv_depth_from_pressure(s->pressure, lat);

        if (c->instr == SV_INSTR_CTD) {
            s->cond     = s->primary;
            s->salinity = sv_salinity_from_cond(s->cond, s->temp, s->pressure);
            s->sv       = sv_soundspeed(s->salinity, s->temp, s->pressure);
        } else {
            s->sv       = s->primary;
            s->salinity = sv_salinity_from_soundspeed(s->sv, s->temp,
                                                      s->pressure);
            s->cond     = sv_cond_from_salinity(s->salinity, s->temp,
                                                s->pressure);
        }

        s->density = sv_density(s->salinity, s->temp, s->pressure);
    }

    c->min_depth = c->max_depth = c->s[0].depth;
    c->min_sv    = c->max_sv    = c->s[0].sv;
    c->min_temp  = c->max_temp  = c->s[0].temp;

    for (int i = 1; i < c->n; i++) {
        const SvSample *s = &c->s[i];
        if (s->depth < c->min_depth) c->min_depth = s->depth;
        if (s->depth > c->max_depth) c->max_depth = s->depth;
        if (s->sv    < c->min_sv)    c->min_sv    = s->sv;
        if (s->sv    > c->max_sv)    c->max_sv    = s->sv;
        if (s->temp  < c->min_temp)  c->min_temp  = s->temp;
        if (s->temp  > c->max_temp)  c->max_temp  = s->temp;
    }
}

bool sv_profile_copy(SvCast *dst, const SvCast *src)
{
    if (!dst || !src)
        return false;

    *dst = *src;
    dst->s = NULL;
    dst->n = dst->cap = 0;

    if (src->n <= 0)
        return true;

    dst->s = calloc((size_t)src->n, sizeof *dst->s);
    if (!dst->s) {
        dst->cap = 0;
        return false;
    }
    memcpy(dst->s, src->s, (size_t)src->n * sizeof *dst->s);
    dst->n = dst->cap = src->n;
    return true;
}

/* Index of the deepest sample. */
static int deepest(const SvCast *c)
{
    int at = 0;
    for (int i = 1; i < c->n; i++)
        if (c->s[i].depth > c->s[at].depth)
            at = i;
    return at;
}

static void keep_range(SvCast *c, int from, int to, double min_depth)
{
    int w = 0;
    for (int i = from; i <= to && i < c->n; i++)
        if (c->s[i].depth >= min_depth)
            c->s[w++] = c->s[i];
    c->n = w;
}

void sv_profile_keep_downcast(SvCast *c, double min_depth)
{
    if (!c || c->n <= 1)
        return;
    keep_range(c, 0, deepest(c), min_depth);
}

void sv_profile_keep_upcast(SvCast *c, double min_depth)
{
    if (!c || c->n <= 1)
        return;

    int d = deepest(c);
    int w = 0;
    for (int i = d; i < c->n; i++)
        if (c->s[i].depth >= min_depth)
            c->s[w++] = c->s[i];
    c->n = w;

    /* An up cast reads shallow-to-deep once reversed, which is what every
     * export format wants. */
    for (int i = 0, j = c->n - 1; i < j; i++, j--) {
        SvSample t = c->s[i];
        c->s[i] = c->s[j];
        c->s[j] = t;
    }
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int sv_profile_despike(SvCast *c, double tol_ms)
{
    enum { WIN = 5 };                  /* odd, so there is a true median */

    if (!c || c->n < WIN || tol_ms <= 0.0)
        return 0;

    char *drop = calloc((size_t)c->n, 1);
    if (!drop)
        return 0;

    for (int i = 0; i < c->n; i++) {
        int lo = i - WIN / 2, hi = i + WIN / 2;
        if (lo < 0) { hi -= lo; lo = 0; }
        if (hi >= c->n) { lo -= (hi - c->n + 1); hi = c->n - 1; }
        if (lo < 0) lo = 0;

        double w[WIN];
        int n = 0;
        for (int j = lo; j <= hi && n < WIN; j++)
            w[n++] = c->s[j].sv;

        qsort(w, (size_t)n, sizeof w[0], cmp_double);
        if (fabs(c->s[i].sv - w[n / 2]) > tol_ms)
            drop[i] = 1;
    }

    int w = 0, removed = 0;
    for (int i = 0; i < c->n; i++) {
        if (drop[i]) removed++;
        else c->s[w++] = c->s[i];
    }
    c->n = w;

    free(drop);
    return removed;
}

int sv_profile_thin(SvCast *c, int max_points)
{
    if (!c || max_points < 2 || c->n <= max_points)
        return 0;

    int target = max_points;
    int before = c->n;

    /*
     * Repeatedly drop the interior point whose removal changes the profile
     * least — the one with the smallest triangle area against its
     * neighbours in (sv, depth). Endpoints are always kept.
     *
     * O(n) per removal is fine here: casts are thousands of points and this
     * runs once, off the render path.
     */
    while (c->n > target) {
        int worst = -1;
        double worst_area = 0.0;

        for (int i = 1; i < c->n - 1; i++) {
            double ax = c->s[i - 1].sv, ay = c->s[i - 1].depth;
            double bx = c->s[i].sv,     by = c->s[i].depth;
            double cx = c->s[i + 1].sv, cy = c->s[i + 1].depth;
            double area = fabs((bx - ax) * (cy - ay) - (cx - ax) * (by - ay));

            if (worst < 0 || area < worst_area) {
                worst = i;
                worst_area = area;
            }
        }
        if (worst < 0)
            break;

        memmove(&c->s[worst], &c->s[worst + 1],
                (size_t)(c->n - worst - 1) * sizeof *c->s);
        c->n--;
    }

    return before - c->n;
}

int sv_profile_bin(SvCast *c, double bin_m)
{
    if (!c || c->n <= 0 || bin_m <= 0.0)
        return c ? c->n : 0;

    int w = 0;
    int i = 0;

    while (i < c->n) {
        double base = floor(c->s[i].depth / bin_m);

        SvSample acc;
        memset(&acc, 0, sizeof acc);
        int n = 0;

        while (i < c->n && floor(c->s[i].depth / bin_m) == base) {
            const SvSample *s = &c->s[i];
            acc.tick     += s->tick;
            acc.primary  += s->primary;
            acc.pressure += s->pressure;
            acc.temp     += s->temp;
            acc.optics1  += s->optics1;
            acc.optics2  += s->optics2;
            acc.depth    += s->depth;
            acc.sv       += s->sv;
            acc.salinity += s->salinity;
            acc.density  += s->density;
            acc.cond     += s->cond;
            n++;
            i++;
        }

        if (n > 0) {
            double inv = 1.0 / n;
            acc.tick *= inv;      acc.primary  *= inv;  acc.pressure *= inv;
            acc.temp *= inv;      acc.optics1  *= inv;  acc.optics2  *= inv;
            acc.depth *= inv;     acc.sv       *= inv;  acc.salinity *= inv;
            acc.density *= inv;   acc.cond     *= inv;
            c->s[w++] = acc;
        }
    }

    c->n = w;
    return c->n;
}

double sv_profile_mean_sv(const SvCast *c)
{
    if (!c || c->n <= 0)
        return 0.0;

    double sum = 0.0;
    for (int i = 0; i < c->n; i++)
        sum += c->s[i].sv;
    return sum / c->n;
}
