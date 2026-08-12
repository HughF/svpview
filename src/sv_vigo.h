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
 * sv_vigo.h — Vigo winch profiler UDP message codec
 *
 * Message building and reply classification only; the socket lives in
 * plat_net. Reference: docs/VIGO_INTERFACE.md.
 */
#ifndef SV_VIGO_H
#define SV_VIGO_H

#include "sv_types.h"
#include <stddef.h>

typedef enum {
    SV_VIGO_REPLY_NONE = 0,
    SV_VIGO_REPLY_RESPONDING,   /* "VIGO responding" — answer to a probe */
    SV_VIGO_REPLY_ACK,          /* "ACK"   — depth accepted              */
    SV_VIGO_REPLY_BTRST,        /* "BTRST" — spool jogged in, retry      */
    SV_VIGO_REPLY_UNKNOWN
} SvVigoReply;

/*
 * Build the outgoing datagrams. Each writes a NUL-terminated string and
 * returns its length excluding the NUL, or 0 if the buffer was too small.
 * The datagram itself is sent without the NUL.
 *
 * seq is the VP number. It must change between consecutive casts or Vigo's
 * identical-message dedupe will discard the second one; sv_vigo_next_seq()
 * produces a usable sequence that never yields 0 (VP-000 is reserved for
 * Vigo's own synthetic manual-depth message).
 */
size_t sv_vigo_probe(char *buf, size_t cap);
size_t sv_vigo_depth(char *buf, size_t cap, int seq, double depth_m);
size_t sv_vigo_fail(char *buf, size_t cap, int seq);

int sv_vigo_next_seq(int seq);

/* Classify a reply datagram. Tolerant of trailing whitespace and NULs. */
SvVigoReply sv_vigo_classify(const char *data, size_t len);

/*
 * Would Vigo accept this depth?
 *
 * Mirrors the server-side validation in vigoServer.js so the operator is
 * warned before the datagram goes out rather than being left wondering why
 * the winch ignored it. Returns NULL if the depth is acceptable, otherwise
 * a short reason (a string literal).
 *
 * requested_m is the depth the cast was commanded to; pass 0 if unknown, in
 * which case only the finite-and-positive rules are applied.
 */
const char *sv_vigo_depth_rejection(double depth_m, double requested_m);

#endif /* SV_VIGO_H */
