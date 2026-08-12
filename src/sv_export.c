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
#include "sv_export.h"
#include "sv_profile.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

static const struct {
    const char *name, *ext;
} FORMATS[SV_EXPORT_COUNT] = {
    { "CSV",             "csv"  },
    { "Valeport VP2",    "vp2"  },
    { "Kongsberg ASVP",  "asvp" },
    { "Caris SVP",       "svp"  },
    { "Hypack VEL",      "vel"  },
};

const char *sv_export_name(SvExportFormat f)
{
    return (f >= 0 && f < SV_EXPORT_COUNT) ? FORMATS[f].name : "?";
}

const char *sv_export_ext(SvExportFormat f)
{
    return (f >= 0 && f < SV_EXPORT_COUNT) ? FORMATS[f].ext : "dat";
}

/* Day of year, 1-based. */
static int yday(int y, int m, int d)
{
    static const int cum[12] = { 0, 31, 59, 90, 120, 151,
                                 181, 212, 243, 273, 304, 334 };
    if (m < 1 || m > 12)
        return 1;
    int n = cum[m - 1] + d;
    bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    if (leap && m > 2)
        n++;
    return n;
}

/* ------------------------------------------------------------------ */
/* Writers                                                             */
/* ------------------------------------------------------------------ */

static void write_csv(FILE *f, const SvCast *c)
{
    fprintf(f, "Depth (m),Pressure (dBar),Sound Velocity (m/s),"
               "Temperature (DegC),Salinity (PSU),Density (kg/m3),"
               "Conductivity (mS/cm)");
    if (c->has_optics)
        fprintf(f, ",Optics 1,Optics 2");
    fprintf(f, "\n");

    for (int i = 0; i < c->n; i++) {
        const SvSample *s = &c->s[i];
        fprintf(f, "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f",
                s->depth, s->pressure, s->sv, s->temp,
                s->salinity, s->density, s->cond);
        if (c->has_optics)
            fprintf(f, ",%.3f,%.3f", s->optics1, s->optics2);
        fprintf(f, "\n");
    }
}

static void write_vp2(FILE *f, const SvCast *c)
{
    fprintf(f, "[HEADER]\n");
    fprintf(f, "Instrument=SWIFT %s\n",
            c->instr == SV_INSTR_CTD ? "CTD" : "SVP");
    fprintf(f, "DirectFromInstrument=1\n");
    fprintf(f, "DeviceSeries=SWIFT\n");
    fprintf(f, "DeviceType=%s\n", c->instr == SV_INSTR_CTD ? "CTD" : "SVP");
    fprintf(f, "SerialNumber=%s\n", c->serial);
    fprintf(f, "InstrumentFirmware=%s\n", c->firmware);
    fprintf(f, "OperationalMode=SMART PROFILE\n");
    fprintf(f, "SamplingRateHz=%.0f\n", c->sample_rate_hz);
    fprintf(f, "OpticsReadingsIn=%d\n", c->has_optics ? 2 : 0);
    fprintf(f, "OpticsType=%d\n", (int)c->optics);
    fprintf(f, "SiteInfo=%s\n", c->site);
    if (c->has_fix) {
        fprintf(f, "Latitude=%.10f\n", c->lat);
        fprintf(f, "Longitude=%.10f\n", c->lon);
    } else {
        fprintf(f, "Latitude=\n");
        fprintf(f, "Longitude=\n");
    }
    fprintf(f, "BatteryVoltage=%.2f\n", c->battery_v);
    fprintf(f, "BatteryRemaining=%.1f\n", c->battery_hours);
    fprintf(f, "PressureTareDbar=%.3f\n", c->tare_dbar);
    fprintf(f, "PressureTareSubtracted=%d\n", c->tare_subtracted ? 1 : 0);
    fprintf(f, "DataStartTime=%04d/%02d/%02d %02d:%02d:%02d\n",
            c->year, c->month, c->day, c->hour, c->minute, c->second);
    fprintf(f, "SamplingInterval=%.5f\n",
            c->sample_rate_hz > 0 ? 1.0 / c->sample_rate_hz : 0.03125);
    fprintf(f, "RowCount=%d\n", c->n);
    fprintf(f, "AverageSosMps=%.3f\n", sv_profile_mean_sv(c));

    fprintf(f, "\n[COLUMNS]\n");
    fprintf(f, "Date/Time=Date/Time;;\n");
    fprintf(f, "Depth=Float;m;Calculated\n");
    fprintf(f, "Pressure=Float;dBar;\n");
    fprintf(f, "Sound Velocity=Float;Ms-1;%s\n",
            c->instr == SV_INSTR_CTD ? "Calculated" : "");
    fprintf(f, "Temperature=Float;DegC;\n");
    fprintf(f, "Salinity=Float;PSU;Calculated\n");
    fprintf(f, "Density=Float;kg/M3;Calculated\n");
    fprintf(f, "Conductivity=Float;mS/M;%s\n",
            c->instr == SV_INSTR_CTD ? "" : "Calculated");

    fprintf(f, "\n[DATA]\n");
    fprintf(f, "Date/Time\tDepth\tPressure\tSound Velocity\tTemperature"
               "\tSalinity\tDensity\tConductivity\n");
    fprintf(f, "\tm\tdBar\tMs-1\tDegC\tPSU\tkg/M3\tmS/M\n");

    for (int i = 0; i < c->n; i++) {
        const SvSample *s = &c->s[i];
        double t = s->tick;
        int sec = (int)t;
        int ms = (int)((t - sec) * 1000.0 + 0.5);

        fprintf(f, "%04d/%02d/%02d %02d:%02d:%02d.%03d"
                   "\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\n",
                c->year, c->month, c->day,
                c->hour, c->minute, c->second + sec, ms,
                s->depth, s->pressure, s->sv, s->temp,
                s->salinity, s->density, s->cond);
    }
}

static void write_asvp(FILE *f, const SvCast *c)
{
    /* Kongsberg sound velocity profile: one bracketed header line, then
     * "depth speed" pairs in metres and m/s. */
    fprintf(f, "( SoundVelocity  1.0 0 %04d%02d%02d%02d%02d%02d "
               "%.6f %.6f 0 %d 0.0 svpview )\n",
            c->year, c->month, c->day, c->hour, c->minute, c->second,
            c->has_fix ? c->lat : 0.0,
            c->has_fix ? c->lon : 0.0,
            c->n);

    for (int i = 0; i < c->n; i++)
        fprintf(f, "%.2f %.2f\n", c->s[i].depth, c->s[i].sv);
}

static void write_caris_svp(FILE *f, const SvCast *c)
{
    fprintf(f, "[SVP_VERSION_2]\n");
    fprintf(f, "svpview_%s\n", c->serial);
    fprintf(f, "Section %04d-%03d %02d:%02d:%02d %.6f %.6f\n",
            c->year, yday(c->year, c->month, c->day),
            c->hour, c->minute, c->second,
            c->has_fix ? c->lat : 0.0,
            c->has_fix ? c->lon : 0.0);

    for (int i = 0; i < c->n; i++)
        fprintf(f, "%.6f %.6f\n", c->s[i].depth, c->s[i].sv);
}

static void write_hypack_vel(FILE *f, const SvCast *c)
{
    fprintf(f, "FTP NEW 2\n");
    fprintf(f, "VER 1.0\n");
    fprintf(f, "TND %02d:%02d:%02d %02d/%02d/%04d\n",
            c->hour, c->minute, c->second, c->day, c->month, c->year);
    if (c->has_fix)
        fprintf(f, "POS %.6f %.6f\n", c->lat, c->lon);
    fprintf(f, "SVC %d\n", c->n);

    for (int i = 0; i < c->n; i++)
        fprintf(f, "%.2f %.2f\n", c->s[i].depth, c->s[i].sv);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

const char *sv_export_write(const SvCast *c, SvExportFormat fmt,
                            const char *path)
{
    if (!c || !path || !*path)
        return "no output path";
    if (c->n <= 0)
        return "profile has no samples";
    if (fmt < 0 || fmt >= SV_EXPORT_COUNT)
        return "unknown export format";

    char tmp[SV_MAX_PATH];
    if ((size_t)snprintf(tmp, sizeof tmp, "%s.tmp", path) >= sizeof tmp)
        return "path too long";

    FILE *f = fopen(tmp, "wb");
    if (!f)
        return "cannot create output file";

    switch (fmt) {
    case SV_EXPORT_CSV:  write_csv(f, c);        break;
    case SV_EXPORT_VP2:  write_vp2(f, c);        break;
    case SV_EXPORT_ASVP: write_asvp(f, c);       break;
    case SV_EXPORT_SVP:  write_caris_svp(f, c);  break;
    case SV_EXPORT_VEL:  write_hypack_vel(f, c); break;
    default: break;
    }

    /* A write error only shows up reliably at close, so check both. */
    bool bad = ferror(f) != 0;
    if (fclose(f) != 0)
        bad = true;
    if (bad) {
        remove(tmp);
        return "write failed (disk full?)";
    }

    remove(path);                 /* Windows rename() will not overwrite */
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return "cannot replace existing file";
    }
    return NULL;
}

bool sv_export_filename(char *buf, size_t cap, const char *tmpl,
                        const SvCast *c, SvExportFormat fmt)
{
    if (!buf || !cap || !tmpl || !c)
        return false;

    size_t w = 0;
    for (const char *p = tmpl; *p; p++) {
        char sub[64];
        const char *ins = NULL;

        if (*p == '%' && p[1]) {
            switch (*++p) {
            case 's': ins = c->serial; break;
            case 'd':
                snprintf(sub, sizeof sub, "%04d%02d%02d",
                         c->year, c->month, c->day);
                ins = sub;
                break;
            case 't':
                snprintf(sub, sizeof sub, "%02d%02d%02d",
                         c->hour, c->minute, c->second);
                ins = sub;
                break;
            case 'e': ins = sv_export_ext(fmt); break;
            default:
                /* Unknown escape: emit it verbatim, including the %. */
                if (w + 2 >= cap) return false;
                buf[w++] = '%';
                buf[w++] = *p;
                continue;
            }
        }

        if (ins) {
            size_t l = strlen(ins);
            if (w + l >= cap) return false;
            memcpy(buf + w, ins, l);
            w += l;
        } else {
            if (w + 1 >= cap) return false;
            buf[w++] = *p;
        }
    }

    buf[w] = '\0';
    return true;
}
