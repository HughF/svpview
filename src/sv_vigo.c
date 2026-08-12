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
#include "sv_vigo.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

/* The literal Ocean sends. Vigo only dispatches on character 0, but matching
 * the observed traffic exactly costs nothing and keeps the winch's UDP
 * console readable for the operator. */
#define WINCH_GO "Valeport-Winch-Go"

size_t sv_vigo_probe(char *buf, size_t cap)
{
    int n = snprintf(buf, cap, "Q,%s", WINCH_GO);
    return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

size_t sv_vigo_depth(char *buf, size_t cap, int seq, double depth_m)
{
    int n = snprintf(buf, cap, "VP-%03d,%s,%.3f", seq, WINCH_GO, depth_m);
    return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

size_t sv_vigo_fail(char *buf, size_t cap, int seq)
{
    int n = snprintf(buf, cap, "F-%03d,%s", seq, WINCH_GO);
    return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

int sv_vigo_next_seq(int seq)
{
    seq++;
    if (seq > 999 || seq < 1)
        seq = 1;                 /* never 0: VP-000 is Vigo's own message */
    return seq;
}

SvVigoReply sv_vigo_classify(const char *d, size_t len)
{
    if (!d)
        return SV_VIGO_REPLY_NONE;

    while (len && (d[len - 1] == '\0' || d[len - 1] == '\r' ||
                   d[len - 1] == '\n' || d[len - 1] == ' '))
        len--;
    if (len == 0)
        return SV_VIGO_REPLY_NONE;

    if (len == 3 && memcmp(d, "ACK", 3) == 0)
        return SV_VIGO_REPLY_ACK;
    if (len == 5 && memcmp(d, "BTRST", 5) == 0)
        return SV_VIGO_REPLY_BTRST;
    if (len == 15 && memcmp(d, "VIGO responding", 15) == 0)
        return SV_VIGO_REPLY_RESPONDING;

    return SV_VIGO_REPLY_UNKNOWN;
}

const char *sv_vigo_depth_rejection(double depth_m, double requested_m)
{
    if (!isfinite(depth_m))
        return "depth is not a number";
    if (depth_m <= 0.0)
        return "depth must be greater than zero";

    if (requested_m > 0.0) {
        if (depth_m < requested_m / 10.0)
            return "depth is less than a tenth of the requested cast depth";
        if (depth_m > requested_m * 10.0)
            return "depth is more than ten times the requested cast depth";
    }
    return NULL;
}
