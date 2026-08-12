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
 * sv_proto.h — Valeport SWiFT serial protocol codec
 *
 * Pure functions over byte buffers. Nothing here opens a port.
 * Reference: docs/SWIFT_PROTOCOL.md
 */
#ifndef SV_PROTO_H
#define SV_PROTO_H

#include "sv_types.h"
#include <stddef.h>

/* ---- NMEA-style broadcasts ---------------------------------------- */

/* XOR of everything between '$' and '*'. */
unsigned sv_nmea_checksum(const char *s, size_t len);

/* Parse one sentence into *out. Returns false for anything unrecognised or
 * structurally malformed; *out is zeroed either way, so a false return never
 * leaves stale data behind. A bad checksum is reported in out->checksum_ok
 * rather than rejected — see the note on SvMsg.
 * `len` bytes at `s`; a trailing CR/LF is tolerated, a NUL is not required. */
bool sv_parse_sentence(const char *s, size_t len, SvMsg *out);

/* ---- Command construction ------------------------------------------ */

/* All of these write a NUL-terminated string including the trailing CRLF and
 * return the number of bytes written (excluding the NUL), or 0 if the buffer
 * was too small. */
size_t sv_cmd_read(char *buf, size_t cap, int code);
size_t sv_cmd_write_int(char *buf, size_t cap, int code, long value);
size_t sv_cmd_write_f(char *buf, size_t cap, int code, double value, int dp);
size_t sv_cmd_write_str(char *buf, size_t cap, int code, const char *value);

/* Codes used by more than one module. */
#define SV_CMD_SERIAL        3
#define SV_CMD_TARE         10
#define SV_CMD_FIRMWARE     14
#define SV_CMD_RUN          28
#define SV_CMD_BATT_PCT    140
#define SV_CMD_BATT_V      141
#define SV_CMD_BATT_HOURS  144
#define SV_CMD_DIR_LIST    400
#define SV_CMD_EXTRACT     402
#define SV_CMD_FREE        403
#define SV_CMD_DELETE      404
#define SV_CMD_CHDIR       406
#define SV_CMD_SET_TIME    407
#define SV_CMD_GET_TIME    408
#define SV_CMD_SET_SITE    410
#define SV_CMD_GET_SITE    411
#define SV_CMD_SET_MODE     41
#define SV_CMD_GET_MODE     42
#define SV_CMD_SET_TRIGGER  24
#define SV_CMD_GET_TRIGGER  25
#define SV_CMD_SET_INCR     20
#define SV_CMD_GET_INCR     21
#define SV_CMD_SET_STEP    132
#define SV_CMD_GET_STEP    133
#define SV_CMD_SET_FIXREQ   52
#define SV_CMD_GET_FIXREQ   53
#define SV_CMD_SET_DIR     166
#define SV_CMD_GET_DIR     167
#define SV_CMD_SET_POWER    15
#define SV_CMD_GET_POWER    16
#define SV_CMD_SET_BTSLEEP 160
#define SV_CMD_GET_BTSLEEP 161
#define SV_CMD_SLEEP       162

/* ---- Response parsing ---------------------------------------------- */

/*
 * The instrument echoes the command, then the reply, then '>'. Strip the
 * echo and the prompt from a received block, leaving the payload.
 *
 * Writes at most cap-1 bytes plus a NUL into out. Returns true if a complete
 * response (terminated by '>') was present.
 */
bool sv_strip_response(const char *rx, size_t len, const char *sent,
                       char *out, size_t cap);

/* One entry from a #400 directory listing. */
typedef struct {
    char name[SV_MAX_NAME];
    bool is_dir;
    long size;
    int  year, month, day, hour, minute, second;
} SvDirEntry;

/* Parse a single listing line. Returns false for headers, prompts and blanks,
 * so the caller can feed it every line without pre-filtering. */
bool sv_parse_dir_line(const char *line, SvDirEntry *e);

/* Rebuild the filename and containing directory of the last recorded file
 * from a status message, without listing anything.
 *   serial "46103", last_file "160823112615"
 *     -> "VL_46103_160823112615.bin" in "\201608\23"
 * Returns false if the status timestamp is not 12 digits. */
bool sv_last_file_path(const SvStatus *st, char *dir, size_t dir_cap,
                       char *file, size_t file_cap);

#endif /* SV_PROTO_H */
