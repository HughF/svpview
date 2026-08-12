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
#include "sv_proto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

/*
 * Split `s` on commas into at most `max` NUL-terminated fields copied into
 * `store` (a flat buffer of `store_cap` bytes). Returns the field count.
 * Nothing is written past the end of store, and a field too long to fit is
 * truncated rather than dropped so the field count stays meaningful.
 */
static int split_csv(const char *s, size_t len, char **field, int max,
                     char *store, size_t store_cap)
{
    int n = 0;
    size_t w = 0;

    if (store_cap == 0 || max <= 0)
        return 0;

    /* Last byte is a permanent NUL: every unused or clamped field slot points
     * at it, so a caller reading field[i] for any i < max is always safe. */
    store[store_cap - 1] = '\0';

    field[0] = store;
    for (size_t i = 0; i <= len && n < max; i++) {
        bool end = (i == len) || (s[i] == ',');
        if (end) {
            if (w + 1 < store_cap) store[w++] = '\0';
            n++;
            if (i < len && n < max)
                field[n] = (w + 1 < store_cap) ? store + w
                                               : store + store_cap - 1;
        } else if (w + 2 < store_cap) {
            store[w++] = s[i];
        }
    }
    for (int i = n; i < max; i++)
        field[i] = store + (store_cap - 1);
    return n;
}

static bool digits_only(const char *s, int n)
{
    for (int i = 0; i < n; i++)
        if (!isdigit((unsigned char)s[i]))
            return false;
    return true;
}

/* Read n digits as an integer. Caller has already checked they are digits. */
static int take_int(const char *s, int n)
{
    int v = 0;
    for (int i = 0; i < n; i++)
        v = v * 10 + (s[i] - '0');
    return v;
}

/* ------------------------------------------------------------------ */
/* NMEA                                                                */
/* ------------------------------------------------------------------ */

unsigned sv_nmea_checksum(const char *s, size_t len)
{
    unsigned cs = 0;
    size_t i = 0;

    if (len && s[0] == '$')
        i = 1;
    for (; i < len && s[i] != '*'; i++)
        cs ^= (unsigned char)s[i];
    return cs & 0xFF;
}

/* Parse YYYYMMDD + hhmmss date/time fields into a live record. */
static bool parse_datetime(const char *d, const char *t, SvLive *lv)
{
    if (strlen(d) != 8 || !digits_only(d, 8))
        return false;
    if (strlen(t) != 6 || !digits_only(t, 6))
        return false;

    lv->year   = take_int(d, 4);
    lv->month  = take_int(d + 4, 2);
    lv->day    = take_int(d + 6, 2);
    lv->hour   = take_int(t, 2);
    lv->minute = take_int(t + 2, 2);
    lv->second = take_int(t + 4, 2);
    return true;
}

bool sv_parse_sentence(const char *s, size_t len, SvMsg *out)
{
    char store[512];
    char *f[24];

    if (!s || !out || len < 7 || s[0] != '$')
        return false;

    /* Trim trailing CR/LF and any whitespace. */
    while (len && (s[len - 1] == '\r' || s[len - 1] == '\n' || s[len - 1] == ' '))
        len--;
    if (len >= sizeof store)
        return false;

    memset(out, 0, sizeof *out);

    /* Check the checksum when one is present, but do not reject on it — see
     * the note on SvMsg.checksum_ok. Ocean's data sentences do not always
     * carry one; the status broadcast always does. */
    const char *star = memchr(s, '*', len);
    if (star) {
        size_t rem = (size_t)(s + len - star);
        if (rem < 3 || !isxdigit((unsigned char)star[1]) ||
            !isxdigit((unsigned char)star[2]))
            return false;
        unsigned want = (unsigned)strtoul((char[3]){ star[1], star[2], 0 },
                                          NULL, 16);
        out->checksum_present = true;
        out->checksum_ok = (sv_nmea_checksum(s, len) == want);
        len = (size_t)(star - s);          /* drop checksum before splitting */
    }

    int n = split_csv(s, len, f, 24, store, sizeof store);
    if (n < 1)
        return false;

    const char *id = f[0];

    /* ---- $PVBB status ------------------------------------------------ */
    if (strcmp(id, "$PVBB") == 0) {
        if (n < 8)
            return false;

        SvStatus st;
        memset(&st, 0, sizeof st);
        snprintf(st.hw_id,  sizeof st.hw_id,  "%s", f[1]);
        snprintf(st.serial, sizeof st.serial, "%s", f[2]);
        st.lat = atof(f[3]);
        st.lon = atof(f[4]);
        st.has_fix = !(st.lat >= 999.0 || st.lon >= 999.0);
        st.battery_hours = atof(f[5]);
        snprintf(st.last_file, sizeof st.last_file, "%s", f[6]);
        st.ready_to_deploy = (f[7][0] == '1');

        out->kind = SV_MSG_STATUS;
        out->status = st;
        return true;
    }

    /* ---- $PVSVP / $PVSV1 / $PVSV2 / $PVCT2 data ---------------------- */
    bool is_ct = (strcmp(id, "$PVCT2") == 0);
    bool is_optics = is_ct || (strcmp(id, "$PVSV2") == 0);
    bool is_plain = (strcmp(id, "$PVSVP") == 0) || (strcmp(id, "$PVSV1") == 0);

    if (!is_ct && !is_optics && !is_plain)
        return false;

    /* Field layout, 1-based as in the guide:
     *   1 date  2 time  3 primary  4 units  5 pressure  6 units
     *   7 temp  8 units
     * then either  9 volts 10 units 11 index                   (plain)
     *      or      9 opt1 10 units 11 opt2 12 units 13 volts
     *             14 units 15 index                            (optics) */
    int need = is_optics ? 16 : 12;
    if (n < need)
        return false;

    SvLive lv;
    memset(&lv, 0, sizeof lv);
    if (!parse_datetime(f[1], f[2], &lv))
        return false;

    lv.primary = atof(f[3]);
    lv.primary_is_cond = is_ct;
    lv.pressure = atof(f[5]);
    lv.temp = atof(f[7]);

    if (is_optics) {
        lv.optics1 = atof(f[9]);
        lv.optics2 = atof(f[11]);
        lv.has_optics = true;
        lv.volts = atof(f[13]);
        lv.log_index = (uint32_t)strtoul(f[15], NULL, 10);
    } else {
        lv.volts = atof(f[9]);
        lv.log_index = (uint32_t)strtoul(f[11], NULL, 10);
    }

    out->kind = SV_MSG_DATA;
    out->live = lv;
    return true;
}

/* ------------------------------------------------------------------ */
/* Command construction                                                */
/* ------------------------------------------------------------------ */

size_t sv_cmd_read(char *buf, size_t cap, int code)
{
    int n = snprintf(buf, cap, "#%03d\r\n", code);
    return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

size_t sv_cmd_write_int(char *buf, size_t cap, int code, long value)
{
    int n = snprintf(buf, cap, "#%03d;%ld\r\n", code, value);
    return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

size_t sv_cmd_write_f(char *buf, size_t cap, int code, double value, int dp)
{
    int n = snprintf(buf, cap, "#%03d;%.*f\r\n", code, dp, value);
    return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

size_t sv_cmd_write_str(char *buf, size_t cap, int code, const char *value)
{
    int n = snprintf(buf, cap, "#%03d;%s\r\n", code, value ? value : "");
    return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

/* ------------------------------------------------------------------ */
/* Response parsing                                                    */
/* ------------------------------------------------------------------ */

bool sv_strip_response(const char *rx, size_t len, const char *sent,
                       char *out, size_t cap)
{
    if (!rx || !out || cap == 0)
        return false;

    out[0] = '\0';

    /*
     * The response is complete once the prompt has arrived — but the prompt
     * is a '>' at the start of a line, not any '>' at all. A directory
     * listing is full of "<DIR>" entries, and treating the '>' in those as
     * the prompt truncates the listing at the first subdirectory.
     */
    const char *prompt = NULL;
    for (size_t i = 0; i < len; i++) {
        if (rx[i] != '>')
            continue;
        if (i == 0 || rx[i - 1] == '\n' || rx[i - 1] == '\r') {
            prompt = rx + i;
            break;
        }
    }
    if (!prompt)
        return false;
    size_t body_len = (size_t)(prompt - rx);

    /* Drop the echoed command if it is there. Compare only the printable
     * part of `sent` — the instrument echoes the characters, and its line
     * endings are not always the ones we sent. */
    if (sent) {
        size_t sl = 0;
        while (sent[sl] && sent[sl] != '\r' && sent[sl] != '\n')
            sl++;
        if (sl > 0 && body_len >= sl && memcmp(rx, sent, sl) == 0) {
            rx += sl;
            body_len -= sl;
        }
    }

    /* Trim surrounding whitespace and line endings. */
    while (body_len && (*rx == '\r' || *rx == '\n' || *rx == ' ')) {
        rx++;
        body_len--;
    }
    while (body_len && (rx[body_len - 1] == '\r' || rx[body_len - 1] == '\n' ||
                        rx[body_len - 1] == ' '))
        body_len--;

    if (body_len >= cap)
        body_len = cap - 1;
    memcpy(out, rx, body_len);
    out[body_len] = '\0';
    return true;
}

bool sv_parse_dir_line(const char *line, SvDirEntry *e)
{
    if (!line || !e)
        return false;

    /* "2020/06/20  07:35:26  <DIR>              20"
     * "2020/06/20  07:35:26              911    VL_46103_160620073527.bin" */
    while (*line == ' ' || *line == '\t')
        line++;

    if (strlen(line) < 19)
        return false;
    if (!digits_only(line, 4) || line[4] != '/' ||
        !digits_only(line + 5, 2) || line[7] != '/' ||
        !digits_only(line + 8, 2))
        return false;

    memset(e, 0, sizeof *e);
    e->year  = take_int(line, 4);
    e->month = take_int(line + 5, 2);
    e->day   = take_int(line + 8, 2);

    const char *p = line + 10;
    while (*p == ' ' || *p == '\t')
        p++;
    if (strlen(p) < 8 || !digits_only(p, 2) || p[2] != ':' ||
        !digits_only(p + 3, 2) || p[5] != ':' || !digits_only(p + 6, 2))
        return false;

    e->hour   = take_int(p, 2);
    e->minute = take_int(p + 3, 2);
    e->second = take_int(p + 6, 2);
    p += 8;

    while (*p == ' ' || *p == '\t')
        p++;

    if (strncmp(p, "<DIR>", 5) == 0) {
        e->is_dir = true;
        p += 5;
        while (*p == ' ' || *p == '\t')
            p++;
    } else {
        char *end = NULL;
        e->size = strtol(p, &end, 10);
        if (end == p)
            return false;
        p = end;
        while (*p == ' ' || *p == '\t')
            p++;
    }

    /* Remainder is the name. A directory line with no name is the "." entry
     * the instrument prints for the directory itself — skip it. */
    size_t nl = strlen(p);
    while (nl && (p[nl - 1] == '\r' || p[nl - 1] == '\n' || p[nl - 1] == ' '))
        nl--;
    if (nl == 0)
        return false;
    if (nl >= sizeof e->name)
        nl = sizeof e->name - 1;
    memcpy(e->name, p, nl);
    e->name[nl] = '\0';
    return true;
}

bool sv_last_file_path(const SvStatus *st, char *dir, size_t dir_cap,
                       char *file, size_t file_cap)
{
    if (!st || !dir || !file)
        return false;

    const char *t = st->last_file;             /* YYMMDDhhmmss */
    if (strlen(t) != 12 || !digits_only(t, 12))
        return false;

    int yy = take_int(t, 2);
    int century = (yy >= 80) ? 19 : 20;        /* GPS epoch, not year 80 */

    if ((size_t)snprintf(dir, dir_cap, "\\%d%02d%02d\\%c%c",
                         century, yy, take_int(t + 2, 2),
                         t[4], t[5]) >= dir_cap)
        return false;
    if ((size_t)snprintf(file, file_cap, "VL_%s_%s.bin", st->serial, t)
        >= file_cap)
        return false;
    return true;
}
