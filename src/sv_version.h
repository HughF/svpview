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
 * sv_version.h — one place for the version string.
 *
 * Date-based, matching the rest of the toolchain (Vigo's serverVersion and
 * the PRU firmware both read as YYYY.MM.DD), so a version seen in a
 * screenshot or a log can be lined up against the other pieces of the system
 * without a lookup table.
 */
#ifndef SV_VERSION_H
#define SV_VERSION_H

#define SVPVIEW_NAME    "svpview"
#define SVPVIEW_VERSION "2026.08.25"
#define SVPVIEW_TAGLINE "Valeport SWiFT profiler acquisition and display"

#endif /* SV_VERSION_H */
