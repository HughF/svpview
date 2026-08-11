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
