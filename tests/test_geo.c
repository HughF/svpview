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
 * test_geo.c — position maths
 *
 * The check values that matter here are the ones a surveyor would use to
 * decide the chart is lying: a minute of latitude is a nautical mile, a
 * degree of longitude shrinks with the cosine of latitude, and a bearing of
 * 090 points east.
 */
#include "sv_geo.h"
#include "sv_test.h"

static void t_per_degree(void)
{
    /* A minute of latitude is a nautical mile, by definition of the unit. */
    CHECK_NEAR(sv_geo_m_per_deg_lat(50.0) / 60.0, 1852.0, 2.0);

    /* Latitude degrees lengthen towards the poles; longitude degrees shrink. */
    CHECK(sv_geo_m_per_deg_lat(80.0) > sv_geo_m_per_deg_lat(10.0));
    CHECK_NEAR(sv_geo_m_per_deg_lon(0.0), 111319.5, 20.0);
    CHECK_NEAR(sv_geo_m_per_deg_lon(60.0),
               sv_geo_m_per_deg_lon(0.0) * 0.5, 200.0);
    CHECK_NEAR(sv_geo_m_per_deg_lon(90.0), 0.0, 1.0);

    /* And no negative width just past the pole, which would mirror the chart. */
    CHECK(sv_geo_m_per_deg_lon(90.5) >= 0.0);
}

static void t_distance(void)
{
    const double lat = 50.4264, lon = -3.6814;    /* Torbay, from the sim */

    /* One minute north is one nautical mile. */
    CHECK_NEAR(sv_geo_distance_m(lat, lon, lat + 1.0 / 60.0, lon), 1852.0, 2.0);

    /* One minute east is shorter by cos(latitude). */
    CHECK_NEAR(sv_geo_distance_m(lat, lon, lat, lon + 1.0 / 60.0),
               1852.0 * 0.6374, 5.0);

    CHECK_NEAR(sv_geo_distance_m(lat, lon, lat, lon), 0.0, 1e-9);

    /* Symmetric, whichever way round. */
    CHECK_NEAR(sv_geo_distance_m(lat, lon, lat + 0.01, lon + 0.02),
               sv_geo_distance_m(lat + 0.01, lon + 0.02, lat, lon), 1e-6);

    /* Beyond the tangent plane's range it switches to the spherical form.
     * London to New York is 5570 km great-circle. */
    CHECK_NEAR(sv_geo_distance_m(51.5074, -0.1278, 40.7128, -74.0060),
               5570000.0, 20000.0);

    /* Either side of the date line is a short hop, not a lap of the planet. */
    CHECK_NEAR(sv_geo_distance_m(0.0, 179.99, 0.0, -179.99), 2226.0, 20.0);

    /* No fix in means no answer out, rather than a plausible-looking number. */
    CHECK(sv_geo_distance_m(999.0, 999.0, lat, lon) < 0.0);
    CHECK(sv_geo_distance_m(lat, lon, 91.0, 0.0) < 0.0);
}

static void t_bearing(void)
{
    const double lat = 50.4264, lon = -3.6814;

    CHECK_NEAR(sv_geo_bearing_deg(lat, lon, lat + 0.01, lon), 0.0, 0.01);
    CHECK_NEAR(sv_geo_bearing_deg(lat, lon, lat, lon + 0.01), 90.0, 0.01);
    CHECK_NEAR(sv_geo_bearing_deg(lat, lon, lat - 0.01, lon), 180.0, 0.01);

    /* Never negative — a compass has no -90. */
    CHECK_NEAR(sv_geo_bearing_deg(lat, lon, lat, lon - 0.01), 270.0, 0.01);

    /* A north-east leg is not 045 unless the degrees are equal in metres. */
    double b = sv_geo_bearing_deg(lat, lon, lat + 0.01, lon + 0.01);
    CHECK(b > 30.0 && b < 34.0);

    CHECK_NEAR(sv_geo_bearing_deg(lat, lon, lat, lon), 0.0, 1e-9);
    CHECK(sv_geo_bearing_deg(999.0, 999.0, lat, lon) < 0.0);
}

static void t_projection(void)
{
    SvGeoProj p;
    sv_geo_proj_init(&p, 50.4264, -3.6814);

    double e, n;
    sv_geo_forward(&p, 50.4264, -3.6814, &e, &n);
    CHECK_NEAR(e, 0.0, 1e-9);
    CHECK_NEAR(n, 0.0, 1e-9);

    /* North is +n, east is +e. */
    sv_geo_forward(&p, 50.4364, -3.6814, &e, &n);
    CHECK(n > 0.0);
    CHECK_NEAR(e, 0.0, 1e-9);
    CHECK_NEAR(n, 1112.0, 3.0);

    sv_geo_forward(&p, 50.4264, -3.6714, &e, &n);
    CHECK(e > 0.0);
    CHECK_NEAR(e, 709.0, 3.0);

    /* Round trip, to the millimetre. */
    double lat, lon;
    sv_geo_forward(&p, 50.4300, -3.6900, &e, &n);
    sv_geo_inverse(&p, e, n, &lat, &lon);
    CHECK_NEAR(lat, 50.4300, 1e-9);
    CHECK_NEAR(lon, -3.6900, 1e-9);

    /* The forward distance agrees with sv_geo_distance_m, so the chart cannot
     * draw one separation while the readout states another. */
    sv_geo_forward(&p, 50.4300, -3.6900, &e, &n);
    CHECK_NEAR(sqrt(e * e + n * n),
               sv_geo_distance_m(50.4264, -3.6814, 50.4300, -3.6900), 1.0);

    /* At the pole the inverse stays finite rather than dividing by zero. */
    sv_geo_proj_init(&p, 90.0, 0.0);
    sv_geo_inverse(&p, 100.0, 100.0, &lat, &lon);
    CHECK(isfinite(lat) && isfinite(lon));
}

static void t_bounds(void)
{
    SvGeoBounds b;
    sv_geo_bounds_reset(&b);
    CHECK(b.n == 0);

    double lat, lon;
    CHECK(!sv_geo_bounds_centre(&b, &lat, &lon));

    /* A no-fix position must not drag the bounds to 999. */
    sv_geo_bounds_add(&b, 999.0, 999.0);
    CHECK(b.n == 0);

    sv_geo_bounds_add(&b, 50.40, -3.70);
    sv_geo_bounds_add(&b, 50.50, -3.60);
    CHECK(b.n == 2);
    CHECK(sv_geo_bounds_centre(&b, &lat, &lon));
    CHECK_NEAR(lat, 50.45, 1e-9);
    CHECK_NEAR(lon, -3.65, 1e-9);

    double w, h;
    sv_geo_bounds_extent_m(&b, &w, &h);
    CHECK_NEAR(h, 11120.0, 30.0);
    CHECK(w > 0.0 && w < h);          /* 0.1 deg of longitude is shorter */

    /* One position: real centre, zero extent. Fit has to cope with this. */
    sv_geo_bounds_reset(&b);
    sv_geo_bounds_add(&b, 50.4264, -3.6814);
    sv_geo_bounds_extent_m(&b, &w, &h);
    CHECK_NEAR(w, 0.0, 1e-9);
    CHECK_NEAR(h, 0.0, 1e-9);
}

static void t_nice_step(void)
{
    CHECK_NEAR(sv_geo_nice_step(1.0), 1.0, 1e-9);
    CHECK_NEAR(sv_geo_nice_step(3.0), 2.0, 1e-9);
    CHECK_NEAR(sv_geo_nice_step(7.0), 5.0, 1e-9);
    CHECK_NEAR(sv_geo_nice_step(9.99), 5.0, 1e-9);
    CHECK_NEAR(sv_geo_nice_step(10.0), 10.0, 1e-9);
    CHECK_NEAR(sv_geo_nice_step(1234.0), 1000.0, 1e-9);
    CHECK_NEAR(sv_geo_nice_step(0.037), 0.02, 1e-9);

    /* Never zero, whatever it is handed — it divides the scale bar. */
    CHECK(sv_geo_nice_step(0.0) > 0.0);
    CHECK(sv_geo_nice_step(-5.0) > 0.0);
}

static void t_grid_step(void)
{
    /* Steps are sexagesimal: whole degrees, minutes or seconds, never
     * 0.037 of a degree. */
    CHECK_NEAR(sv_geo_grid_step_deg(20.0, 4), 5.0, 1e-12);
    CHECK_NEAR(sv_geo_grid_step_deg(0.5, 5), 5.0 / 60, 1e-12);
    CHECK_NEAR(sv_geo_grid_step_deg(0.02, 4), 10.0 / 3600, 1e-12);   /* 18" asked, 10" given */

    /* Never denser than asked for, and never zero however far it is zoomed. */
    for (double span = 90.0; span > 1e-7; span /= 3.0) {
        double s = sv_geo_grid_step_deg(span, 5);
        CHECK(s > 0.0);
        CHECK(s <= span / 5.0 + 1e-12 || s == 0.01 / 3600);
    }
    CHECK(sv_geo_grid_step_deg(0.0, 5) > 0.0);
    CHECK(sv_geo_grid_step_deg(10.0, 0) > 0.0);
}

static void t_format(void)
{
    char buf[64];

    sv_geo_format_lat(buf, sizeof buf, 50.4264, 1.0 / 60);
    CHECK_STR(buf, "50\xc2\xb0 25.6' N");

    sv_geo_format_lat(buf, sizeof buf, 50.4264, 1.0 / 3600);
    CHECK_STR(buf, "50\xc2\xb0 25.584' N");

    /* Hemisphere from the sign, magnitude always positive. */
    sv_geo_format_lat(buf, sizeof buf, -33.8688, 1.0 / 60);
    CHECK_STR(buf, "33\xc2\xb0 52.1' S");
    sv_geo_format_lon(buf, sizeof buf, -3.6814, 1.0 / 60);
    CHECK_STR(buf, "3\xc2\xb0 40.9' W");
    sv_geo_format_lon(buf, sizeof buf, 151.2093, 1.0 / 60);
    CHECK_STR(buf, "151\xc2\xb0 12.6' E");

    /* Minutes that round up to 60 carry into the degrees. */
    sv_geo_format_lat(buf, sizeof buf, 50.99999, 1.0 / 60);
    CHECK_STR(buf, "51\xc2\xb0 0.0' N");

    sv_geo_format_lat(buf, sizeof buf, 1.0 / 0.0, 1.0 / 60);
    CHECK_STR(buf, "\xe2\x80\x94");

    sv_geo_format_distance(buf, sizeof buf, 0.0);
    CHECK_STR(buf, "0 m");
    sv_geo_format_distance(buf, sizeof buf, 864.4);
    CHECK_STR(buf, "864 m");
    sv_geo_format_distance(buf, sizeof buf, 1852.0);
    CHECK_STR(buf, "1.85 km  (1.00 nm)");
    sv_geo_format_distance(buf, sizeof buf, -1.0);
    CHECK_STR(buf, "\xe2\x80\x94");
}

TEST_MAIN("sv_geo",
    t_per_degree();
    t_distance();
    t_bearing();
    t_projection();
    t_bounds();
    t_nice_step();
    t_grid_step();
    t_format();
)
