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
 * sv_profile.h — cast data model and processing
 *
 * Every operation here either fills in derived columns or removes samples;
 * none of them ever grow the array, so the single allocation made by the
 * file reader is the only one a cast ever needs.
 */
#ifndef SV_PROFILE_H
#define SV_PROFILE_H

#include "sv_types.h"

/*
 * Fill in depth, sv, salinity, density and cond for every sample, and refresh
 * the cached extremes.
 *
 * For an SVP the measured primary is sound speed and salinity is inverted
 * from it; for a CTD the primary is conductivity and sound speed is computed.
 * Either way all five derived columns end up populated, which is what the
 * export writers and the plot expect.
 *
 * lat is used for the pressure-to-depth gravity term; pass the cast's own
 * position when it has a fix, SV_DEFAULT_LAT otherwise.
 */
void sv_profile_derive(SvCast *c, double lat);

/* Copy src into dst, including a fresh sample array. dst is zeroed first and
 * owns its array afterwards. Returns false if the allocation failed. */
bool sv_profile_copy(SvCast *dst, const SvCast *src);

/*
 * Keep only the descending (or ascending) part of the profile.
 *
 * The turning point is the deepest sample; everything after it is the up
 * cast. Samples shallower than min_depth are dropped either way, which is
 * what removes the surface soak and the recovery tail.
 */
void sv_profile_keep_downcast(SvCast *c, double min_depth);
void sv_profile_keep_upcast(SvCast *c, double min_depth);

/*
 * Remove spikes: a sample whose sound speed differs from the median of the
 * surrounding window by more than `tol` m/s. Returns the number removed.
 */
int sv_profile_despike(SvCast *c, double tol_ms);

/*
 * Reduce to at most max_points by dropping the sample that contributes least
 * to the shape (largest-triangle decimation on depth/sv). Returns the number
 * removed. A max_points below 2 is ignored.
 */
int sv_profile_thin(SvCast *c, int max_points);

/*
 * Average into fixed depth bins. Samples are replaced by one entry per
 * occupied bin, at the bin's mean depth. Returns the resulting sample count.
 */
int sv_profile_bin(SvCast *c, double bin_m);

/* Mean sound speed over the whole profile — the number surveyors quote. */
double sv_profile_mean_sv(const SvCast *c);

#endif /* SV_PROFILE_H */
