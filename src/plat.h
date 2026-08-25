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
 * plat.h — everything OS-specific, behind one header
 *
 * No file above this line calls an OS API directly. All I/O here is
 * non-blocking: the app polls it once per frame, so nothing in the program
 * can stall the window.
 */
#ifndef PLAT_H
#define PLAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- time ---------------------------------------------------------- */

uint64_t plat_now_ms(void);            /* monotonic, arbitrary origin */

/* ---- serial -------------------------------------------------------- */

typedef struct PlatSerial PlatSerial;

#define PLAT_MAX_PORTS 32

typedef struct {
    char path[256];                    /* what plat_serial_open() wants  */
    char label[128];                   /* what the operator should see   */
} PlatPortInfo;

/* Enumerate candidate serial ports. Returns the count, at most max. */
int  plat_serial_list(PlatPortInfo *out, int max);

/* Open at `baud`, 8N1, non-blocking. NULL on failure. */
PlatSerial *plat_serial_open(const char *path, int baud);
void plat_serial_close(PlatSerial *s);

/* Non-blocking. Return bytes moved, 0 for "nothing right now", or -1 on a
 * hard error (the caller should close and go back to disconnected). */
int  plat_serial_read(PlatSerial *s, void *buf, size_t cap);
int  plat_serial_write(PlatSerial *s, const void *buf, size_t len);

/* ---- network interfaces -------------------------------------------- */

#define PLAT_MAX_IFS 16

typedef struct {
    char name[64];         /* eth0, Ethernet 2, ...                     */
    char ip[46];           /* this adapter's address                    */
    char mask[46];         /* its subnet mask                           */
    char bcast[46];        /* directed broadcast, computed from the mask */
    bool up;               /* link is up and it has an address          */
} PlatNetIf;

/*
 * Enumerate IPv4 broadcast-capable adapters. Returns the count, at most max.
 *
 * A survey PC usually has more than one: the survey LAN and a general
 * network. Broadcasting to 255.255.255.255 leaves the choice of which one to
 * the routing table, which is how a depth report ends up on the wrong wire
 * and the winch never hears it — so the adapter is chosen explicitly.
 */
int plat_net_list_ifs(PlatNetIf *out, int max);

/* ---- UDP ----------------------------------------------------------- */

typedef struct PlatUdp PlatUdp;

/*
 * Broadcast-capable socket bound to an ephemeral port.
 *
 * bind_ip selects the adapter to send from: pass an adapter address to pin
 * the traffic to that interface, or NULL for the default route. NULL on
 * failure.
 */
PlatUdp *plat_udp_open(const char *bind_ip);
void plat_udp_close(PlatUdp *u);

/*
 * Broadcast on `port`. dest_bcast is the directed broadcast address of the
 * chosen adapter; NULL falls back to 255.255.255.255. Returns bytes sent
 * or -1.
 */
int  plat_udp_send_broadcast(PlatUdp *u, const char *dest_bcast, int port,
                             const void *buf, size_t len);

/* Non-blocking receive. Returns bytes read, 0 if nothing waiting, -1 error. */
int  plat_udp_recv(PlatUdp *u, void *buf, size_t cap);

/* ---- filesystem ---------------------------------------------------- */

/* Per-user config directory, created if needed. False if it cannot be made. */
bool plat_config_dir(char *buf, size_t cap);

/* Join with the platform separator. False if it would not fit. */
bool plat_path_join(char *buf, size_t cap, const char *dir, const char *leaf);

/*
 * Cut a closed file to `len` bytes. The instrument returns its command
 * prompt at the end of a transfer and that byte is not part of the file, so
 * the download drops it rather than carry a phantom truncated record.
 *
 * Here because the POSIX call has no Windows equivalent: truncate() against
 * a path, _chsize_s() against an open descriptor.
 */
bool plat_file_truncate(const char *path, long len);

#endif /* PLAT_H */
