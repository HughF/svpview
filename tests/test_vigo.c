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
 * test_vigo.c — Vigo profiler UDP message codec.
 *
 * The depth message is checked against the traffic captured in
 * Vigo/docs/Swift sample UDP messages.txt:
 *     VP-001,Valeport-Winch-Go,1.434
 */
#include "sv_test.h"
#include "../src/sv_vigo.h"

#define S(x) x, strlen(x)

TEST_MAIN("sv_vigo",
{
    char b[128];

    /* ---- depth message matches captured traffic byte for byte ------ */
    CHECK(sv_vigo_depth(b, sizeof b, 1, 1.434) > 0);
    CHECK_STR(b, "VP-001,Valeport-Winch-Go,1.434");

    CHECK(sv_vigo_depth(b, sizeof b, 3, 1.294) > 0);
    CHECK_STR(b, "VP-003,Valeport-Winch-Go,1.294");

    /* Vigo takes the last comma-separated field as the depth. */
    sv_vigo_depth(b, sizeof b, 42, 25.493);
    CHECK_STR(strrchr(b, ',') + 1, "25.493");

    /* Three-digit zero padding holds above and below ten. */
    sv_vigo_depth(b, sizeof b, 7, 100.0);
    CHECK_STR(b, "VP-007,Valeport-Winch-Go,100.000");
    sv_vigo_depth(b, sizeof b, 999, 5.0);
    CHECK_STR(b, "VP-999,Valeport-Winch-Go,5.000");

    /* First character is what Vigo dispatches on. */
    CHECK(b[0] == 'V');
    CHECK(sv_vigo_probe(b, sizeof b) > 0);
    CHECK(b[0] == 'Q');
    CHECK(sv_vigo_fail(b, sizeof b, 1) > 0);
    CHECK(b[0] == 'F');

    /* Undersized buffers fail rather than emitting a truncated datagram —
     * a truncated depth field would be read by the winch as a real depth. */
    CHECK(sv_vigo_depth(b, 10, 1, 1.434) == 0);
    CHECK(sv_vigo_probe(b, 3) == 0);

    /* ---- sequence numbering ---------------------------------------- */
    /* Never 0: VP-000 is Vigo's own synthetic manual-depth message. */
    CHECK(sv_vigo_next_seq(0) == 1);
    CHECK(sv_vigo_next_seq(1) == 2);
    CHECK(sv_vigo_next_seq(999) == 1);
    CHECK(sv_vigo_next_seq(-5) == 1);

    /* Consecutive casts to the same depth must not produce identical
     * datagrams, or Vigo's dedupe drops the second. */
    char first[128], second[128];
    int seq = 0;
    seq = sv_vigo_next_seq(seq);
    sv_vigo_depth(first, sizeof first, seq, 25.0);
    seq = sv_vigo_next_seq(seq);
    sv_vigo_depth(second, sizeof second, seq, 25.0);
    CHECK(strcmp(first, second) != 0);

    /* ---- reply classification -------------------------------------- */
    CHECK(sv_vigo_classify(S("ACK")) == SV_VIGO_REPLY_ACK);
    CHECK(sv_vigo_classify(S("BTRST")) == SV_VIGO_REPLY_BTRST);
    CHECK(sv_vigo_classify(S("VIGO responding")) == SV_VIGO_REPLY_RESPONDING);
    CHECK(sv_vigo_classify(S("ACK\r\n")) == SV_VIGO_REPLY_ACK);
    CHECK(sv_vigo_classify("ACK\0\0", 5) == SV_VIGO_REPLY_ACK);
    CHECK(sv_vigo_classify(S("")) == SV_VIGO_REPLY_NONE);
    CHECK(sv_vigo_classify(NULL, 0) == SV_VIGO_REPLY_NONE);
    CHECK(sv_vigo_classify(S("something else")) == SV_VIGO_REPLY_UNKNOWN);
    /* Prefixes must not be mistaken for the real thing. */
    CHECK(sv_vigo_classify(S("AC")) == SV_VIGO_REPLY_UNKNOWN);
    CHECK(sv_vigo_classify(S("ACKNOWLEDGED")) == SV_VIGO_REPLY_UNKNOWN);

    /* ---- server-side depth rules, mirrored -------------------------- */
    CHECK(sv_vigo_depth_rejection(25.0, 25.0) == NULL);
    CHECK(sv_vigo_depth_rejection(25.0, 0.0) == NULL);
    CHECK(sv_vigo_depth_rejection(0.0, 0.0) != NULL);
    CHECK(sv_vigo_depth_rejection(-1.0, 0.0) != NULL);
    CHECK(sv_vigo_depth_rejection(1.0 / 0.0, 0.0) != NULL);
    /* Outside 0.1x - 10x the requested depth. */
    CHECK(sv_vigo_depth_rejection(2.0, 25.0) != NULL);
    CHECK(sv_vigo_depth_rejection(300.0, 25.0) != NULL);
    CHECK(sv_vigo_depth_rejection(2.5, 25.0) == NULL);
    CHECK(sv_vigo_depth_rejection(250.0, 25.0) == NULL);
})
