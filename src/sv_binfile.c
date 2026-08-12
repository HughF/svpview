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
#include "sv_binfile.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Bounded cursor                                                      */
/*                                                                     */
/* Every read goes through this. Once a read runs off the end the       */
/* cursor latches bad and all later reads return zero, so a parse can   */
/* be written straight-line and checked once at the end.                */
/* ------------------------------------------------------------------ */

typedef struct {
    const unsigned char *p;
    size_t len;
    size_t at;
    bool   bad;
} Cur;

static bool cur_have(Cur *c, size_t n)
{
    if (c->bad || c->at + n > c->len) {
        c->bad = true;
        return false;
    }
    return true;
}

static unsigned cur_u8(Cur *c)
{
    if (!cur_have(c, 1)) return 0;
    return c->p[c->at++];
}

static unsigned cur_u16le(Cur *c)
{
    if (!cur_have(c, 2)) return 0;
    unsigned v = (unsigned)c->p[c->at] | ((unsigned)c->p[c->at + 1] << 8);
    c->at += 2;
    return v;
}

static uint32_t cur_u32le(Cur *c)
{
    if (!cur_have(c, 4)) return 0;
    uint32_t v = (uint32_t)c->p[c->at]
               | ((uint32_t)c->p[c->at + 1] << 8)
               | ((uint32_t)c->p[c->at + 2] << 16)
               | ((uint32_t)c->p[c->at + 3] << 24);
    c->at += 4;
    return v;
}

/* IEEE-754 little-endian, which is what the instrument writes. Assembled by
 * hand rather than type-punned so this stays correct on a big-endian host. */
static float cur_f32le(Cur *c)
{
    uint32_t b = cur_u32le(c);
    float f;
    unsigned char le[4] = {
        (unsigned char)(b & 0xFF), (unsigned char)((b >> 8) & 0xFF),
        (unsigned char)((b >> 16) & 0xFF), (unsigned char)((b >> 24) & 0xFF)
    };
    /* Host float layout: memcpy of the native byte order. */
    union { float f; unsigned char b[4]; } u;
    static const int host_le = 1;
    if (*(const char *)&host_le) {
        memcpy(u.b, le, 4);
    } else {
        u.b[0] = le[3]; u.b[1] = le[2]; u.b[2] = le[1]; u.b[3] = le[0];
    }
    f = u.f;
    return f;
}

static void cur_skip(Cur *c, size_t n)
{
    if (cur_have(c, n))
        c->at += n;
}

static void cur_str(Cur *c, char *out, size_t n, size_t cap)
{
    size_t copy = (n < cap - 1) ? n : cap - 1;
    if (!cur_have(c, n)) {
        out[0] = '\0';
        return;
    }
    memcpy(out, c->p + c->at, copy);
    out[copy] = '\0';
    c->at += n;

    /* Trim trailing NULs and spaces — these are fixed-width fields. */
    size_t l = strlen(out);
    while (l && (out[l - 1] == ' ' || out[l - 1] == '\r' || out[l - 1] == '\n'))
        out[--l] = '\0';
}

/* Packed BCD: two decimal digits per byte. */
static int bcd2(unsigned b)
{
    return (int)((b >> 4) & 0x0F) * 10 + (int)(b & 0x0F);
}

/* ------------------------------------------------------------------ */
/* Variant detection                                                   */
/* ------------------------------------------------------------------ */

SvFirmwareVariant sv_firmware_variant(const char *v)
{
    if (!v)
        return SV_FW_UNKNOWN;

    if (strstr(v, "0650734"))
        return SV_FW_0650734;

    const char *p = strstr(v, "0650735");
    if (!p)
        return SV_FW_UNKNOWN;

    /* The letter after the number is the sub-variant: A/B/C, then D0 onwards.
     * Anything from 'D' up gets the D0 layout. */
    for (const char *q = p + 7; *q; q++) {
        if (*q >= 'A' && *q <= 'Z')
            return (*q >= 'D') ? SV_FW_0650735_D0 : SV_FW_0650735_ABC;
        if (*q != ' ')
            break;
    }
    return SV_FW_0650735_ABC;
}

/* ------------------------------------------------------------------ */
/* Parse                                                               */
/* ------------------------------------------------------------------ */

#define MAX_RECORDS 2000000        /* 32 Hz for 17 hours — far beyond a cast */

/*
 * Is `off` a usable data start — i.e. is the byte immediately before it the
 * header's ETX terminator?
 *
 * Every candidate offset is filtered through here precisely because the
 * offsets are computed from a length field inside the file. Doing the bounds
 * test at the point of use, rather than trusting the arithmetic that
 * produced the offset, is the only version of this that stays correct when
 * header_size is hostile.
 */
static bool etx_before(const unsigned char *p, size_t len,
                       size_t min_off, size_t off)
{
    return off > min_off && off <= len && p[off - 1] == 0x03;
}

const char *sv_binfile_parse(const void *data, size_t len,
                             SvCast *cast, SvBinInfo *info)
{
    if (!data || !cast)
        return "no data";

    memset(cast, 0, sizeof *cast);

    SvBinInfo nfo;
    memset(&nfo, 0, sizeof nfo);

    if (len < 64)
        return "file too short to hold a header";

    const unsigned char *p = data;

    /*
     * The logger prepends its own firmware version as a CRLF-terminated
     * string of unspecified length ("50 (not fixed)"). Find that terminator
     * inside a bounded window rather than trusting any stated length.
     */
    size_t fw_end = 0;
    size_t window = (len < 128) ? len : 128;
    for (size_t i = 0; i + 1 < window; i++) {
        if (p[i] == '\r' && p[i + 1] == '\n') {
            fw_end = i + 2;
            break;
        }
    }
    if (fw_end == 0)
        return "no logger version string (missing CRLF)";

    Cur c = { p, len, fw_end, false };

    nfo.header_size = cur_u16le(&c);

    unsigned family = cur_u8(&c);
    unsigned type   = cur_u8(&c);
    unsigned tick   = cur_u8(&c);

    if (c.bad)
        return "truncated header";
    if (family != 1)
        return "not a SWiFT file (bad instrument family)";
    if (type != 1 && type != 2)
        return "unknown instrument type";

    cast->instr = (type == 2) ? SV_INSTR_CTD : SV_INSTR_SVP;

    /* ccyymmddhhmmss packed BCD, 7 bytes. */
    if (!cur_have(&c, 7))
        return "truncated header (date)";
    int cc = bcd2(cur_u8(&c));
    int yy = bcd2(cur_u8(&c));
    cast->year   = cc * 100 + yy;
    cast->month  = bcd2(cur_u8(&c));
    cast->day    = bcd2(cur_u8(&c));
    cast->hour   = bcd2(cur_u8(&c));
    cast->minute = bcd2(cur_u8(&c));
    cast->second = bcd2(cur_u8(&c));

    cast->lat = cur_f32le(&c);
    cast->lon = cur_f32le(&c);
    cast->has_fix = !(cast->lat >= 999.0 || cast->lon >= 999.0 ||
                      cast->lat <= -999.0 || cast->lon <= -999.0);

    double serial = cur_f32le(&c);
    snprintf(cast->serial, sizeof cast->serial, "%.0f", serial);

    cur_str(&c, cast->firmware, 35, sizeof cast->firmware);
    nfo.variant = sv_firmware_variant(cast->firmware);

    cur_skip(&c, 3);                             /* cal_date d/m/y */
    unsigned sample_rate = cur_u8(&c);
    cur_skip(&c, 1);                             /* operating mode */

    cast->tare_dbar = cur_f32le(&c);

    /* Primary-parameter cal block: 2 floats for SV, 3 for conductivity. */
    cur_skip(&c, (cast->instr == SV_INSTR_CTD) ? 12 : 8);
    cur_skip(&c, 12);                            /* pressure cal        */
    if (cast->instr == SV_INSTR_CTD && nfo.variant == SV_FW_0650735_D0)
        cur_skip(&c, 12);                        /* temp resistor cal   */
    cur_skip(&c, 12);                            /* temperature cal     */

    cast->battery_hours = cur_f32le(&c);
    cast->battery_v     = cur_f32le(&c);
    cast->tare_subtracted = (cur_u8(&c) != 0);

    if (cast->instr == SV_INSTR_CTD && nfo.variant == SV_FW_0650735_D0)
        cur_skip(&c, 1);                         /* cage fitted         */

    cur_skip(&c, 12 * 3);                        /* user cal blocks     */

    /* Site info: 100 bytes, ASCII or UTF-16LE flagged by a 0x01 0x01 prefix
     * from firmware D0 onwards. */
    if (cur_have(&c, 100)) {
        const unsigned char *si = c.p + c.at;
        if (si[0] == 0x01 && si[1] == 0x01) {
            size_t w = 0;
            for (size_t i = 2; i + 1 < 100 && w + 1 < sizeof cast->site; i += 2) {
                unsigned ch = (unsigned)si[i] | ((unsigned)si[i + 1] << 8);
                if (ch == 0)
                    break;
                cast->site[w++] = (ch < 0x80) ? (char)ch : '?';
            }
            cast->site[w] = '\0';
        } else {
            memcpy(cast->site, si, 100);
            cast->site[100] = '\0';
        }
        c.at += 100;
    }

    if (nfo.variant != SV_FW_0650734) {
        unsigned opt = cur_u8(&c);
        if (opt <= SV_OPTICS_TURBIDITY)
            cast->optics = (SvOpticsType)opt;
        cast->has_optics = (cast->optics != SV_OPTICS_NONE);
        cur_skip(&c, 12 * 4);                    /* optics cal blocks   */
    }

    cast->sample_rate_hz = sample_rate ? sample_rate : (tick ? tick : 32);
    nfo.record_size = cast->has_optics ? 24 : 16;

    /*
     * Locate the sample data. The declared header_size is the primary
     * source, but the guide is ambiguous about whether it includes the size
     * field itself, and the three variant totals it quotes are inconsistent
     * with each other. So: trust it only if the byte immediately before the
     * data is the 0x03 ETX terminator, otherwise search a small window, and
     * fall back to where the field walk actually ended.
     */
    size_t found = 0;

    /* A declared size that would put the data outside the file is not a
     * candidate at all — checking it would mean reading out of bounds. */
    size_t cand = 0;
    if (nfo.header_size > 0 && nfo.header_size <= len - fw_end)
        cand = fw_end + nfo.header_size;

    if (etx_before(p, len, fw_end, cand)) {
        found = cand;
        nfo.header_size_trusted = true;
    } else if (cand) {
        for (int d = 1; d <= 8 && !found; d++) {
            if (etx_before(p, len, fw_end, cand + (size_t)d))
                found = cand + (size_t)d;
            else if (cand >= (size_t)d && etx_before(p, len, fw_end, cand - (size_t)d))
                found = cand - (size_t)d;
        }
    }

    if (!found) {
        /* Field walk landed on the ETX? */
        if (!c.bad && c.at < len && p[c.at] == 0x03)
            found = c.at + 1;
        else
            return "header end marker (ETX) not found";
    }

    nfo.data_offset = found;
    if (nfo.data_offset > len)
        return "header extends past end of file";

    size_t avail = len - nfo.data_offset;
    size_t n = avail / nfo.record_size;
    nfo.truncated = (avail % nfo.record_size) != 0;

    if (n == 0)
        return "no samples in file";
    if (n > MAX_RECORDS)
        return "implausible sample count";

    cast->s = calloc(n, sizeof *cast->s);
    if (!cast->s)
        return "out of memory";
    cast->cap = (int)n;

    Cur d = { p, len, nfo.data_offset, false };
    double rate = cast->sample_rate_hz > 0 ? cast->sample_rate_hz : 32.0;

    for (size_t i = 0; i < n; i++) {
        uint32_t t = cur_u32le(&d);
        float primary  = cur_f32le(&d);
        float pressure = cur_f32le(&d);
        float temp     = cur_f32le(&d);
        float o1 = 0, o2 = 0;
        if (cast->has_optics) {
            o1 = cur_f32le(&d);
            o2 = cur_f32le(&d);
        }
        if (d.bad)
            break;

        SvSample *s = &cast->s[cast->n++];
        s->tick     = (double)t / rate;
        s->primary  = primary;
        s->pressure = pressure;
        s->temp     = temp;
        s->optics1  = o1;
        s->optics2  = o2;
    }

    if (cast->n == 0) {
        sv_cast_free(cast);
        return "no readable samples";
    }

    nfo.n_records = cast->n;
    if (info)
        *info = nfo;
    return NULL;
}

void sv_cast_free(SvCast *cast)
{
    if (!cast)
        return;
    free(cast->s);
    cast->s = NULL;
    cast->n = cast->cap = 0;
}
