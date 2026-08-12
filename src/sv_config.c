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
 * sv_config.c — instrument settings helpers
 *
 * Ranges are the ones in the integration guide section 3; clamping happens
 * here rather than in the UI so a value can never reach the instrument out
 * of range regardless of how it was entered.
 */
#include "sv_types.h"

#include <string.h>
#include <math.h>

static bool clamp_d(double *v, double lo, double hi)
{
    double was = *v;
    if (!isfinite(*v)) *v = lo;
    if (*v < lo) *v = lo;
    if (*v > hi) *v = hi;
    return *v != was;
}

static bool clamp_i(int *v, int lo, int hi)
{
    int was = *v;
    if (*v < lo) *v = lo;
    if (*v > hi) *v = hi;
    return *v != was;
}

bool sv_config_clamp(SvConfig *c)
{
    if (!c)
        return false;

    bool changed = false;
    changed |= clamp_i(&c->operating_mode, 0, 1);
    changed |= clamp_i(&c->direction, 0, 1);
    changed |= clamp_d(&c->trigger_depth, 0.1, 100.0);
    changed |= clamp_d(&c->depth_increment, 0.1, 100.0);
    changed |= clamp_d(&c->trigger_step, 0.5, 100.0);
    changed |= clamp_i(&c->require_fix, 0, 1);
    changed |= clamp_i(&c->auto_power_min, 1, 9999);
    changed |= clamp_i(&c->bt_sleep_enabled, 0, 1);

    c->site[SV_SITE_LEN - 1] = '\0';
    return changed;
}

bool sv_config_equal(const SvConfig *a, const SvConfig *b)
{
    if (!a || !b)
        return false;

    return a->operating_mode   == b->operating_mode
        && a->direction        == b->direction
        && a->trigger_depth    == b->trigger_depth
        && a->depth_increment  == b->depth_increment
        && a->trigger_step     == b->trigger_step
        && a->require_fix      == b->require_fix
        && a->auto_power_min   == b->auto_power_min
        && a->bt_sleep_enabled == b->bt_sleep_enabled
        && strcmp(a->site, b->site) == 0;
}
