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
 * sv_export.h — profile export writers
 *
 * Every writer takes a finished path and writes atomically: content goes to
 * "<path>.tmp" and is renamed over the target only once it is complete, so a
 * failure part-way through can never leave a truncated profile where the
 * survey system expects a good one.
 *
 * Returns NULL on success or a short reason (string literal) on failure.
 *
 * Format fidelity:
 *   VP2, CSV        — built from the worked example in the integration
 *                     guide section 12, so these are faithful.
 *   ASVP, SVP, VEL  — built to the published layouts of those formats.
 *                     Not yet diffed against Ocean's own output; see
 *                     ROADMAP.md phase 5.
 */
#ifndef SV_EXPORT_H
#define SV_EXPORT_H

#include "sv_types.h"
#include <stddef.h>

typedef enum {
    SV_EXPORT_CSV = 0,
    SV_EXPORT_VP2,
    SV_EXPORT_ASVP,          /* Kongsberg  .asvp */
    SV_EXPORT_SVP,           /* Caris      .svp  */
    SV_EXPORT_VEL,           /* Hypack     .vel  */
    SV_EXPORT_COUNT
} SvExportFormat;

const char *sv_export_name(SvExportFormat f);   /* "Kongsberg ASVP"  */
const char *sv_export_ext(SvExportFormat f);    /* "asvp"            */

const char *sv_export_write(const SvCast *c, SvExportFormat f,
                            const char *path);

/*
 * Build an output filename from a template into buf.
 *   %s serial   %d date YYYYMMDD   %t time hhmmss   %e extension
 * Unknown escapes are copied through. Returns false if it would not fit.
 */
bool sv_export_filename(char *buf, size_t cap, const char *tmpl,
                        const SvCast *c, SvExportFormat f);

#endif /* SV_EXPORT_H */
