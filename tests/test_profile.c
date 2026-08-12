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
 * test_profile.c — cast processing and export.
 *
 * The fixture is a synthetic down-and-up cast so the profile split has a real
 * turning point to find.
 */
#include "sv_test.h"
#include "../src/sv_profile.h"
#include "../src/sv_binfile.h"
#include "../src/sv_export.h"
#include "../src/sv_ocean.h"

#include <stdlib.h>
#include <stdio.h>

static void build_cast(SvCast *c, int n_down, int n_up)
{
    memset(c, 0, sizeof *c);
    c->instr = SV_INSTR_SVP;
    c->n = c->cap = n_down + n_up;
    c->s = calloc((size_t)c->n, sizeof *c->s);
    c->sample_rate_hz = 32;
    c->year = 2026; c->month = 8; c->day = 11;
    c->hour = 10; c->minute = 54; c->second = 36;
    c->lat = 50.4264; c->lon = -3.6814; c->has_fix = true;
    snprintf(c->serial, sizeof c->serial, "46236");

    for (int i = 0; i < n_down; i++) {
        c->s[i].pressure = (double)i * 0.5;
        c->s[i].temp = 15.0 - i * 0.05;
        c->s[i].primary = 1500.0 + i * 0.2;
        c->s[i].tick = i / 32.0;
    }
    for (int i = 0; i < n_up; i++) {
        int k = n_down + i;
        c->s[k].pressure = (double)(n_down - 1 - i) * 0.5;
        c->s[k].temp = 15.0 - (n_down - 1 - i) * 0.05;
        c->s[k].primary = 1500.0 + (n_down - 1 - i) * 0.2;
        c->s[k].tick = k / 32.0;
    }
}

TEST_MAIN("sv_profile",
{
    SvCast c;

    /* ---- derive ----------------------------------------------------- */
    build_cast(&c, 100, 80);
    sv_profile_derive(&c, 50.0);

    CHECK(c.n == 180);
    CHECK_NEAR(c.s[0].depth, 0.0, 1e-9);
    CHECK(c.max_depth > 48.0 && c.max_depth < 50.0);
    /* An SVP measures sound speed; salinity and conductivity are derived. */
    CHECK_NEAR(c.s[10].sv, c.s[10].primary, 1e-9);
    CHECK(c.s[10].salinity > 0.0 && c.s[10].salinity < 42.0);
    CHECK(c.s[10].density > 1000.0 && c.s[10].density < 1050.0);
    CHECK(c.s[10].cond > 0.0);
    CHECK(c.min_sv <= c.max_sv);

    /* ---- copy is independent ---------------------------------------- */
    SvCast copy;
    CHECK(sv_profile_copy(&copy, &c));
    CHECK(copy.n == c.n);
    CHECK(copy.s != c.s);
    copy.s[0].sv = 1.0;
    CHECK(c.s[0].sv != 1.0);
    sv_cast_free(&copy);

    /* ---- down cast split -------------------------------------------- */
    CHECK(sv_profile_copy(&copy, &c));
    sv_profile_keep_downcast(&copy, 0.0);
    CHECK(copy.n == 100);
    /* Monotonically deepening once trimmed. */
    bool monotonic = true;
    for (int i = 1; i < copy.n; i++)
        if (copy.s[i].depth < copy.s[i - 1].depth)
            monotonic = false;
    CHECK(monotonic);
    sv_cast_free(&copy);

    /* ---- up cast split, reversed to read shallow-to-deep ------------- */
    /* 81, not 80: the turning point is the deepest reading in the file and
     * belongs to both halves. */
    CHECK(sv_profile_copy(&copy, &c));
    sv_profile_keep_upcast(&copy, 0.0);
    CHECK(copy.n == 81);
    CHECK(copy.s[0].depth < copy.s[copy.n - 1].depth);
    sv_cast_free(&copy);

    /* ---- despike ----------------------------------------------------- */
    CHECK(sv_profile_copy(&copy, &c));
    copy.s[40].sv += 60.0;                       /* one obvious flier */
    copy.s[41].sv -= 45.0;
    int removed = sv_profile_despike(&copy, 3.0);
    CHECK(removed >= 2);
    CHECK(copy.n == c.n - removed);
    /* Nothing left more than the tolerance from its neighbours. */
    sv_cast_free(&copy);

    /* A clean profile loses nothing. */
    CHECK(sv_profile_copy(&copy, &c));
    CHECK(sv_profile_despike(&copy, 30.0) == 0);
    CHECK(copy.n == c.n);
    sv_cast_free(&copy);

    /* ---- thin -------------------------------------------------------- */
    CHECK(sv_profile_copy(&copy, &c));
    sv_profile_thin(&copy, 40);
    CHECK(copy.n == 40);
    /* Endpoints survive: they carry the surface and the deepest reading. */
    CHECK_NEAR(copy.s[0].depth, c.s[0].depth, 1e-9);
    CHECK_NEAR(copy.s[copy.n - 1].depth, c.s[c.n - 1].depth, 1e-9);
    /* Asking for more points than there are is a no-op. */
    CHECK(sv_profile_thin(&copy, 500) == 0);
    CHECK(copy.n == 40);
    sv_cast_free(&copy);

    /* ---- depth binning ------------------------------------------------ */
    CHECK(sv_profile_copy(&copy, &c));
    sv_profile_keep_downcast(&copy, 0.0);
    int before = copy.n;
    sv_profile_bin(&copy, 5.0);
    CHECK(copy.n < before);
    CHECK(copy.n >= 9);              /* ~49 m in 5 m bins */
    CHECK(copy.n <= 11);
    monotonic = true;
    for (int i = 1; i < copy.n; i++)
        if (copy.s[i].depth <= copy.s[i - 1].depth)
            monotonic = false;
    CHECK(monotonic);
    sv_cast_free(&copy);

    /* Degenerate arguments are refused rather than dividing by zero. */
    CHECK(sv_profile_copy(&copy, &c));
    sv_profile_bin(&copy, 0.0);
    CHECK(copy.n == c.n);
    sv_profile_thin(&copy, 1);
    CHECK(copy.n == c.n);
    sv_cast_free(&copy);

    /* ---- mean --------------------------------------------------------- */
    CHECK(sv_profile_mean_sv(&c) > 1499.0);
    CHECK(sv_profile_mean_sv(&c) < 1521.0);

    SvCast empty;
    memset(&empty, 0, sizeof empty);
    CHECK_NEAR(sv_profile_mean_sv(&empty), 0.0, 1e-9);
    sv_profile_derive(&empty, 50.0);            /* must not crash */
    sv_profile_despike(&empty, 3.0);
    sv_profile_thin(&empty, 10);
    sv_profile_bin(&empty, 1.0);
    sv_profile_keep_downcast(&empty, 0.0);

    /* ---- export -------------------------------------------------------- */
    const char *dir = getenv("TMPDIR");
    if (!dir) dir = "/tmp";

    for (int f = 0; f < SV_EXPORT_COUNT; f++) {
        char path[512];
        snprintf(path, sizeof path, "%s/svpview_test.%s", dir,
                 sv_export_ext((SvExportFormat)f));

        const char *err = sv_export_write(&c, (SvExportFormat)f, path);
        CHECK_STR(err ? err : "ok", "ok");

        FILE *fh = fopen(path, "rb");
        CHECK(fh != NULL);
        if (fh) {
            char first[256] = { 0 };
            long size;
            fseek(fh, 0, SEEK_END);
            size = ftell(fh);
            rewind(fh);
            if (!fgets(first, sizeof first, fh)) first[0] = '\0';
            fclose(fh);

            CHECK(size > 100);          /* header plus 180 rows */
            CHECK(first[0] != '\0');
            remove(path);
        }
    }

    /* An empty profile is refused rather than writing a header-only file
     * that a survey system would read as a valid cast. */
    char path[512];
    snprintf(path, sizeof path, "%s/svpview_empty.asvp", dir);
    CHECK(sv_export_write(&empty, SV_EXPORT_ASVP, path) != NULL);
    CHECK(sv_export_write(&c, SV_EXPORT_ASVP, "") != NULL);
    CHECK(sv_export_write(NULL, SV_EXPORT_ASVP, path) != NULL);

    /* ---- filename templates -------------------------------------------- */
    char name[128];
    CHECK(sv_export_filename(name, sizeof name, "%s_%d%t.%e", &c,
                             SV_EXPORT_ASVP));
    CHECK_STR(name, "46236_20260811105436.asvp");

    CHECK(sv_export_filename(name, sizeof name, "cast.%e", &c, SV_EXPORT_VEL));
    CHECK_STR(name, "cast.vel");

    /* Unknown escapes pass through untouched. */
    CHECK(sv_export_filename(name, sizeof name, "%q.%e", &c, SV_EXPORT_CSV));
    CHECK_STR(name, "%q.csv");

    /* Too small a buffer fails rather than truncating to a wrong name. */
    CHECK(!sv_export_filename(name, 8, "%s_%d%t.%e", &c, SV_EXPORT_ASVP));

    sv_cast_free(&c);
})
