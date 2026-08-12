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
 * sv_types.h — data types shared across the core.
 *
 * Nothing here knows about SDL, sockets or files. Fixed-size buffers
 * throughout: no core struct owns a pointer it has to free except SvCast,
 * whose sample array is one allocation.
 */
#ifndef SV_TYPES_H
#define SV_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#define SV_MAX_PATH      512
#define SV_MAX_NAME      128
#define SV_MAX_LINE      1024
#define SV_SITE_LEN      101      /* 100 chars + NUL */

/* ------------------------------------------------------------------ */
/* Instrument                                                          */
/* ------------------------------------------------------------------ */

typedef enum {
    SV_INSTR_UNKNOWN = 0,
    SV_INSTR_SVP,                 /* measures sound speed  (type 1) */
    SV_INSTR_CTD                  /* measures conductivity (type 2) */
} SvInstrType;

typedef enum {
    SV_OPTICS_NONE = 0, SV_OPTICS_CHLOROPHYL, SV_OPTICS_FLUORESCEIN,
    SV_OPTICS_RHODAMINE, SV_OPTICS_CRUDE_OIL, SV_OPTICS_CDOM,
    SV_OPTICS_PHYCOERYTHRIN, SV_OPTICS_PHYCOCYANIN, SV_OPTICS_TURBIDITY
} SvOpticsType;

/* ------------------------------------------------------------------ */
/* One observation                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    double tick;                  /* seconds from file start */
    double primary;               /* sound speed m/s, or conductivity mS/cm */
    double pressure;              /* dBar, tare applied */
    double temp;                  /* degC */
    double optics1, optics2;      /* sensor-dependent units */

    /* Derived by sv_profile_derive() */
    double depth;                 /* m */
    double sv;                    /* m/s  — measured or computed */
    double salinity;              /* PSU */
    double density;               /* kg/m3 */
    double cond;                  /* mS/cm — measured or computed */
} SvSample;

/* ------------------------------------------------------------------ */
/* A cast                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    char        name[SV_MAX_NAME];        /* source filename */
    char        serial[32];
    char        firmware[64];
    char        site[SV_SITE_LEN];

    SvInstrType instr;
    SvOpticsType optics;
    bool        has_optics;

    /* UTC start time, broken out — the instrument clock is GPS-disciplined */
    int         year, month, day, hour, minute, second;

    double      lat, lon;                 /* 999 when there was no fix */
    bool        has_fix;

    double      tare_dbar;
    bool        tare_subtracted;
    double      sample_rate_hz;
    double      battery_v;
    double      battery_hours;

    SvSample   *s;
    int         n;
    int         cap;

    /* Cached extremes, refreshed by sv_profile_derive() */
    double      min_depth, max_depth;
    double      min_sv, max_sv;
    double      min_temp, max_temp;
} SvCast;

/* ------------------------------------------------------------------ */
/* Live data and status                                                */
/* ------------------------------------------------------------------ */

typedef enum {
    SV_MSG_NONE = 0,
    SV_MSG_STATUS,                /* $PVBB */
    SV_MSG_DATA                   /* $PVSVP / $PVSV1 / $PVSV2 / $PVCT2 */
} SvMsgKind;

typedef struct {
    char     hw_id[32];
    char     serial[32];
    double   lat, lon;            /* 999 = no fix */
    bool     has_fix;
    double   battery_hours;
    char     last_file[32];       /* YYMMDDhhmmss as sent */
    bool     ready_to_deploy;
} SvStatus;

typedef struct {
    int      year, month, day, hour, minute, second;
    double   primary;             /* SV (m/s) or conductivity (mS/cm) */
    bool     primary_is_cond;
    double   pressure, temp, volts;
    double   optics1, optics2;
    bool     has_optics;
    uint32_t log_index;
} SvLive;

typedef struct {
    SvMsgKind kind;
    SvStatus  status;
    SvLive    live;

    /* A sentence carrying a checksum that does not verify is still parsed,
     * with this set false, and the app counts it. Rejecting outright was
     * considered and dropped: the algorithm (XOR between '$' and '*') is
     * confirmed by the fact that the deltas between the integration guide's
     * worked examples match exactly, but the guide's example text is not
     * byte-faithful (a constant 0x70 offset says characters were lost in
     * transcription), so we have no byte-exact sample to prove the whole
     * chain. Losing a status broadcast costs the operator the deploy flag
     * and battery reading; every field is independently range-checked
     * anyway. Revisit once a real capture exists. */
    bool      checksum_present;
    bool      checksum_ok;
} SvMsg;

/* ------------------------------------------------------------------ */
/* Instrument configuration (the settings dialog edits one of these)   */
/* ------------------------------------------------------------------ */

typedef struct {
    int    operating_mode;        /* 0 continuous, 1 smart profile */
    int    direction;             /* 0 down cast, 1 up cast */
    double trigger_depth;         /* m   #024 */
    double depth_increment;       /* m   #020 */
    double trigger_step;          /* m   #132 */
    int    require_fix;           /* 0/1 #052 */
    int    auto_power_min;        /* min #015, 9999 = off */
    int    bt_sleep_enabled;      /* 0/1 #160 */
    char   site[SV_SITE_LEN];     /*     #410 */
} SvConfig;

/* Field-by-field equality, used to drive the Apply button's enabled state. */
bool sv_config_equal(const SvConfig *a, const SvConfig *b);

/* Clamp every field to the ranges in the integration guide. Returns true if
 * anything had to be changed. */
bool sv_config_clamp(SvConfig *c);

#endif /* SV_TYPES_H */
