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
 * sv_ocean.h — oceanographic conversions
 *
 * UNESCO 1983 (Fofonoff & Millard) unless stated. Pure functions, no state,
 * no I/O — everything here is unit-tested against published check values in
 * tests/test_ocean.c.
 *
 * Pressure is gauge pressure in dBar (what the instrument reports, tare
 * already applied) unless a function says otherwise.
 */
#ifndef SV_OCEAN_H
#define SV_OCEAN_H

/* Depth (m) from pressure (dBar) at latitude (decimal degrees).
 * UNESCO 1983 eqn 25. Use SV_DEFAULT_LAT when there is no GPS fix. */
double sv_depth_from_pressure(double p_dbar, double lat_deg);

#define SV_DEFAULT_LAT 45.0

/* Sound speed (m/s) — Chen & Millero 1977 / UNESCO 1983.
 * Valid 0-40 PSU, 0-40 degC, 0-1000 bar. */
double sv_soundspeed(double sal_psu, double temp_c, double p_dbar);

/* Practical salinity (PSU) from conductivity (mS/cm), PSS-78.
 * Returns 0 for conductivity at or below zero. */
double sv_salinity_from_cond(double cond_mScm, double temp_c, double p_dbar);

/* Conductivity (mS/cm) from practical salinity — numeric inverse of the
 * above. Returns 0 for salinity at or below zero. */
double sv_cond_from_salinity(double sal_psu, double temp_c, double p_dbar);

/* Practical salinity (PSU) implied by a measured sound speed — the numeric
 * inverse of sv_soundspeed(). This is how a SWiFT *SVP* (which measures
 * sound speed directly and has no conductivity cell) gets a salinity, and it
 * matches the "Calculated" salinity column Ocean writes into its VP2 files.
 *
 * The inversion is monotonic over the valid range and is solved by bisection
 * on 0-42 PSU. Out-of-range sound speeds clamp to the interval ends. */
double sv_salinity_from_soundspeed(double sv_ms, double temp_c, double p_dbar);

/* In-situ density (kg/m3) — EOS-80 international equation of state. */
double sv_density(double sal_psu, double temp_c, double p_dbar);

/* Gravity (m/s2) at latitude — UNESCO 1983. Exposed for the depth tests. */
double sv_gravity(double lat_deg);

#endif /* SV_OCEAN_H */
