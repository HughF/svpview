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
 * sv_sim.h — a SWiFT SVP emulated at the wire
 *
 * Deliberately not a bypass: the simulator speaks the same bytes a real
 * instrument does — command echo, reply, '>' prompt, $PVBB broadcasts, and a
 * complete synthetic .bin file over #402 — so running against it exercises
 * the real session, parser and download code rather than a shortcut around
 * them. If it works in --sim it is the link, not the logic, that is left to
 * prove on the bench.
 */
#ifndef SV_SIM_H
#define SV_SIM_H

#include <stddef.h>

typedef struct SvSim SvSim;

SvSim *sv_sim_create(void);
void   sv_sim_destroy(SvSim *s);

/* Host -> instrument. Always consumes everything. */
int sv_sim_write(SvSim *s, const void *buf, size_t len);

/* Instrument -> host. Returns bytes produced, 0 when idle. */
int sv_sim_read(SvSim *s, void *buf, size_t cap);

/* Advance time-driven behaviour (status broadcasts, continuous data). */
void sv_sim_tick(SvSim *s, unsigned long now_ms);

#endif /* SV_SIM_H */
