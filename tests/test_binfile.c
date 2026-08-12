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
 * test_binfile.c — .bin decoder, including the corrupt-file cases.
 *
 * There is no real instrument file to hand, so the fixtures are built here
 * field by field from the layout in the integration guide section 10. That
 * makes the builder itself a check on the layout: the header sizes it
 * produces are compared against the totals the guide quotes, which are
 * derived independently of the field list.
 *
 *   variant 734      110 + 36 + 100                     = 246
 *   variant 735 ABC   50 + 145 + 100 + 50, less the 50  = 295
 *   variant 735 D0 CTD 50 + 162 + 100 + 50, less the 50 = 312
 */
#include "sv_test.h"
#include "../src/sv_binfile.h"
#include "../src/sv_profile.h"

#include <stdlib.h>

/* ---- little-endian writer ------------------------------------------ */

typedef struct {
    unsigned char b[8192];
    size_t n;
} Buf;

static void w8(Buf *b, unsigned v)      { b->b[b->n++] = (unsigned char)v; }
static void w16(Buf *b, unsigned v)     { w8(b, v & 0xFF); w8(b, (v >> 8) & 0xFF); }
static void w32(Buf *b, uint32_t v)
{
    w8(b, v & 0xFF); w8(b, (v >> 8) & 0xFF);
    w8(b, (v >> 16) & 0xFF); w8(b, (v >> 24) & 0xFF);
}
static void wf(Buf *b, float f)
{
    union { float f; uint32_t u; } u;
    u.f = f;
    w32(b, u.u);
}
static void wpad(Buf *b, size_t n)      { while (n--) w8(b, 0); }
static void wstr(Buf *b, const char *s, size_t field)
{
    size_t i = 0;
    for (; s[i] && i < field; i++) w8(b, (unsigned char)s[i]);
    wpad(b, field - i);
}
static void wbcd(Buf *b, int v)         { w8(b, ((v / 10) << 4) | (v % 10)); }

/* ---- fixture builder ----------------------------------------------- */

typedef struct {
    const char *fw;          /* version string written into the header  */
    int   type;              /* 1 = SV, 2 = CTD                         */
    int   optics;            /* 0 = none                                */
    int   n_samples;
    bool  unicode_site;
} Fixture;

/* Returns the declared header_size, i.e. the byte count from the size field
 * through the ETX inclusive — which is what the parser adds to the end of
 * the logger version string to find the data. */
static size_t build(Buf *b, const Fixture *fx)
{
    memset(b, 0, sizeof *b);

    /* Logger firmware string, CRLF-terminated, length deliberately not 50. */
    const char *logger = "SWIFT LOGGER 1.2.3";
    for (const char *p = logger; *p; p++) w8(b, (unsigned char)*p);
    w8(b, '\r'); w8(b, '\n');

    size_t hdr_start = b->n;
    bool is_ctd = (fx->type == 2);
    bool is_734 = (strstr(fx->fw, "0650734") != NULL);
    bool is_d0  = (sv_firmware_variant(fx->fw) == SV_FW_0650735_D0);

    w16(b, 0);                       /* header_size, patched below      */
    w8(b, 1);                        /* family: SWiFT                   */
    w8(b, (unsigned)fx->type);
    w8(b, 32);                       /* tick rate                       */

    wbcd(b, 20); wbcd(b, 21);        /* ccyy = 2021                     */
    wbcd(b, 7); wbcd(b, 28);         /* mm dd                           */
    wbcd(b, 10); wbcd(b, 54); wbcd(b, 36);

    wf(b, 50.4264f);
    wf(b, -3.6814f);
    wf(b, 46103.0f);                 /* serial as a float               */
    wstr(b, fx->fw, 35);
    wpad(b, 3);                      /* cal date                        */
    w8(b, 32);                       /* sample rate                     */
    w8(b, 1);                        /* operating mode                  */
    wf(b, 10.204f);                  /* pressure tare                   */

    wpad(b, is_ctd ? 12 : 8);        /* primary parameter cal           */
    wpad(b, 12);                     /* pressure cal                    */
    if (is_ctd && is_d0) wpad(b, 12);/* temperature resistor cal        */
    wpad(b, 12);                     /* temperature cal                 */

    wf(b, 61.6f);                    /* battery hours                   */
    wf(b, 3.71f);                    /* battery volts                   */
    w8(b, 1);                        /* tare subtracted                 */
    if (is_ctd && is_d0) w8(b, 0);   /* cage fitted                     */

    wpad(b, 36);                     /* three user cal blocks           */

    if (fx->unicode_site) {
        w8(b, 0x01); w8(b, 0x01);
        const char *s = "Valeport Test Site";
        size_t used = 2;
        for (const char *p = s; *p && used + 2 <= 100; p++) {
            w8(b, (unsigned char)*p); w8(b, 0);
            used += 2;
        }
        wpad(b, 100 - used);
    } else {
        wstr(b, "Valeport Test Site", 100);
    }

    if (!is_734) {
        w8(b, (unsigned)fx->optics);
        wpad(b, 48);                 /* four optics cal blocks          */
    }

    w8(b, 0x03);                     /* ETX                             */

    size_t hdr_size = b->n - hdr_start;
    b->b[hdr_start]     = (unsigned char)(hdr_size & 0xFF);
    b->b[hdr_start + 1] = (unsigned char)((hdr_size >> 8) & 0xFF);

    /* Samples: a plausible descending profile. */
    for (int i = 0; i < fx->n_samples; i++) {
        w32(b, (uint32_t)(i * 32));                  /* one per second  */
        wf(b, is_ctd ? 42.7f : (float)(1500.0 + i)); /* primary         */
        wf(b, (float)(i * 2));                       /* pressure dBar   */
        wf(b, (float)(15.0 - i * 0.1));              /* temperature     */
        if (fx->optics) { wf(b, 36.3f); wf(b, 26.4f); }
    }

    return hdr_size;
}

TEST_MAIN("sv_binfile",
{
    Buf b;
    SvCast c;
    SvBinInfo nfo;
    const char *err;

    /* ---- variant detection ----------------------------------------- */
    CHECK(sv_firmware_variant("0650734") == SV_FW_0650734);
    CHECK(sv_firmware_variant("0650735A9 Jun 29 2018 12:09") == SV_FW_0650735_ABC);
    CHECK(sv_firmware_variant("0650735C1") == SV_FW_0650735_ABC);
    CHECK(sv_firmware_variant("0650735D0") == SV_FW_0650735_D0);
    CHECK(sv_firmware_variant("0650735E2") == SV_FW_0650735_D0);
    CHECK(sv_firmware_variant("nonsense") == SV_FW_UNKNOWN);
    CHECK(sv_firmware_variant(NULL) == SV_FW_UNKNOWN);

    /* ---- 0650734, SV, no optics ------------------------------------ */
    Fixture f734 = { "0650734", 1, 0, 10, false };
    size_t hs = build(&b, &f734);
    CHECK(hs == 246);                       /* guide: 110 + 36 + 100    */

    err = sv_binfile_parse(b.b, b.n, &c, &nfo);
    CHECK_STR(err ? err : "ok", "ok");
    CHECK(nfo.variant == SV_FW_0650734);
    CHECK(nfo.header_size == 246);
    CHECK(nfo.header_size_trusted);
    CHECK(nfo.record_size == 16);
    CHECK(!nfo.truncated);
    CHECK(c.n == 10);
    CHECK(c.instr == SV_INSTR_SVP);
    CHECK(!c.has_optics);
    CHECK_STR(c.serial, "46103");
    CHECK_STR(c.site, "Valeport Test Site");
    CHECK(c.year == 2021 && c.month == 7 && c.day == 28);
    CHECK(c.hour == 10 && c.minute == 54 && c.second == 36);
    CHECK_NEAR(c.lat, 50.4264, 1e-4);
    CHECK_NEAR(c.lon, -3.6814, 1e-4);
    CHECK(c.has_fix);
    CHECK_NEAR(c.tare_dbar, 10.204, 1e-4);
    CHECK(c.tare_subtracted);
    CHECK_NEAR(c.battery_hours, 61.6, 1e-3);
    CHECK_NEAR(c.s[0].primary, 1500.0, 1e-3);
    CHECK_NEAR(c.s[9].primary, 1509.0, 1e-3);
    CHECK_NEAR(c.s[0].pressure, 0.0, 1e-6);
    CHECK_NEAR(c.s[9].pressure, 18.0, 1e-4);
    CHECK_NEAR(c.s[1].tick, 1.0, 1e-9);     /* 32 ticks at 32 Hz = 1 s  */
    sv_cast_free(&c);

    /* ---- 0650735 A/B/C, SV with optics ----------------------------- */
    Fixture f735 = { "0650735A9 Jun 29 2018 12:09", 1, 8, 20, false };
    hs = build(&b, &f735);
    CHECK(hs == 295);                       /* guide: 145 + 100 + 50    */

    err = sv_binfile_parse(b.b, b.n, &c, &nfo);
    CHECK_STR(err ? err : "ok", "ok");
    CHECK(nfo.variant == SV_FW_0650735_ABC);
    CHECK(nfo.record_size == 24);
    CHECK(c.has_optics);
    CHECK(c.optics == SV_OPTICS_TURBIDITY);
    CHECK(c.n == 20);
    CHECK_NEAR(c.s[0].optics1, 36.3, 1e-3);
    CHECK_NEAR(c.s[0].optics2, 26.4, 1e-3);
    sv_cast_free(&c);

    /* ---- 0650735 D0, CTD ------------------------------------------- */
    Fixture fd0 = { "0650735D0", 2, 0, 5, true };
    hs = build(&b, &fd0);
    CHECK(hs == 312);                       /* guide: 162 + 100 + 50    */

    err = sv_binfile_parse(b.b, b.n, &c, &nfo);
    CHECK_STR(err ? err : "ok", "ok");
    CHECK(nfo.variant == SV_FW_0650735_D0);
    CHECK(c.instr == SV_INSTR_CTD);
    CHECK_STR(c.site, "Valeport Test Site");   /* decoded from UTF-16LE */
    CHECK_NEAR(c.s[0].primary, 42.7, 1e-3);    /* conductivity          */

    /* A CTD cast derives sound speed rather than measuring it. */
    sv_profile_derive(&c, 50.0);
    CHECK(c.s[0].sv > 1400.0 && c.s[0].sv < 1600.0);
    CHECK(c.s[0].salinity > 20.0 && c.s[0].salinity < 40.0);
    CHECK(c.s[0].density > 1000.0 && c.s[0].density < 1050.0);
    sv_cast_free(&c);

    /* ---- corrupt input --------------------------------------------- */

    /* Empty and tiny. */
    CHECK(sv_binfile_parse(NULL, 0, &c, NULL) != NULL);
    CHECK(sv_binfile_parse("", 0, &c, NULL) != NULL);
    CHECK(sv_binfile_parse("short", 5, &c, NULL) != NULL);

    /* No CRLF anywhere. */
    {
        unsigned char junk[256];
        memset(junk, 'x', sizeof junk);
        CHECK(sv_binfile_parse(junk, sizeof junk, &c, NULL) != NULL);
    }

    /* Truncated mid-header: every prefix must fail cleanly, never read out
     * of bounds. Run under ASan this is the real test. */
    build(&b, &f735);
    for (size_t cut = 1; cut < 300; cut++) {
        err = sv_binfile_parse(b.b, cut, &c, NULL);
        if (!err) sv_cast_free(&c);
    }

    /* Truncated mid-record: the partial record is dropped, the rest kept. */
    build(&b, &f734);
    err = sv_binfile_parse(b.b, b.n - 7, &c, &nfo);
    CHECK_STR(err ? err : "ok", "ok");
    CHECK(nfo.truncated);
    CHECK(c.n == 9);
    sv_cast_free(&c);

    /* Header with no samples at all. */
    build(&b, &f734);
    CHECK(sv_binfile_parse(b.b, 246 + 20, &c, NULL) != NULL);

    /* Wrong instrument family. */
    build(&b, &f734);
    b.b[20 + 2] = 9;
    CHECK(sv_binfile_parse(b.b, b.n, &c, NULL) != NULL);

    /* An absurd declared header size must not send the reader off the end.
     * The field walk still lands on the ETX, so the file is recovered — with
     * the declared size marked untrusted. */
    build(&b, &f734);
    b.b[20] = 0xFF; b.b[21] = 0xFF;
    err = sv_binfile_parse(b.b, b.n, &c, &nfo);
    CHECK_STR(err ? err : "ok", "ok");
    CHECK(!nfo.header_size_trusted);
    CHECK(nfo.data_offset == 20 + 246);
    CHECK(c.n == 10);
    if (!err) sv_cast_free(&c);

    /* Absurd header size *and* no ETX to recover from: give up cleanly. */
    build(&b, &f734);
    b.b[20] = 0xFF; b.b[21] = 0xFF;
    for (size_t i = 0; i < b.n; i++)
        if (b.b[i] == 0x03) b.b[i] = 0x00;
    err = sv_binfile_parse(b.b, b.n, &c, NULL);
    CHECK(err != NULL);
    if (!err) sv_cast_free(&c);

    /* Header size wrong by a few bytes: recoverable by finding the ETX. */
    build(&b, &f734);
    b.b[20] = (unsigned char)((246 - 3) & 0xFF);
    err = sv_binfile_parse(b.b, b.n, &c, &nfo);
    CHECK_STR(err ? err : "ok", "ok");
    CHECK(!nfo.header_size_trusted);
    CHECK(nfo.data_offset == 20 + 246);
    CHECK(c.n == 10);
    sv_cast_free(&c);

    /* Every single-byte corruption is survivable — no crash, no hang. */
    build(&b, &f735);
    size_t total = b.n;
    for (size_t i = 0; i < total; i += 7) {
        Buf t = b;
        t.b[i] ^= 0xFF;
        err = sv_binfile_parse(t.b, t.n, &c, NULL);
        if (!err) sv_cast_free(&c);
    }
})
