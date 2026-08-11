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

/* ---- UDP ----------------------------------------------------------- */

typedef struct PlatUdp PlatUdp;

/* Broadcast-capable socket bound to an ephemeral port. NULL on failure. */
PlatUdp *plat_udp_open(void);
void plat_udp_close(PlatUdp *u);

/* Send to the broadcast address on `port`. Returns bytes sent or -1. */
int  plat_udp_send_broadcast(PlatUdp *u, int port, const void *buf, size_t len);

/* Non-blocking receive. Returns bytes read, 0 if nothing waiting, -1 error. */
int  plat_udp_recv(PlatUdp *u, void *buf, size_t cap);

/* ---- filesystem ---------------------------------------------------- */

/* Per-user config directory, created if needed. False if it cannot be made. */
bool plat_config_dir(char *buf, size_t cap);

/* Join with the platform separator. False if it would not fit. */
bool plat_path_join(char *buf, size_t cap, const char *dir, const char *leaf);

#endif /* PLAT_H */
