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
 * plat_posix.c — Linux and macOS implementation of plat.h.
 *
 * Windows lives in plat_win32.c; the two are never compiled together.
 */
#if !defined(_WIN32)

#include "plat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>

/* ---- time ---------------------------------------------------------- */

uint64_t plat_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

/* ---- serial -------------------------------------------------------- */

struct PlatSerial { int fd; };

static speed_t baud_const(int baud)
{
    switch (baud) {
    case 9600:   return B9600;
    case 19200:  return B19200;
    case 38400:  return B38400;
    case 57600:  return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    default:     return B230400;
    }
}

int plat_serial_list(PlatPortInfo *out, int max)
{
    int n = 0;

    /* /dev/serial/by-id gives stable, human-meaningful names — an FTDI
     * cable there reads as e.g. "usb-FTDI_Swift_SVP_46236-if00-port0", which
     * is exactly what the operator needs to pick the right instrument. */
    const char *by_id = "/dev/serial/by-id";
    DIR *d = opendir(by_id);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && n < max) {
            if (e->d_name[0] == '.')
                continue;

            char link[512];
            snprintf(link, sizeof link, "%s/%s", by_id, e->d_name);

            char real[256];
            ssize_t l = readlink(link, real, sizeof real - 1);
            if (l > 0) {
                real[l] = '\0';
                /* readlink gives "../../ttyUSB0" */
                const char *base = strrchr(real, '/');
                snprintf(out[n].path, sizeof out[n].path,
                         "/dev/%.240s", base ? base + 1 : real);
            } else {
                snprintf(out[n].path, sizeof out[n].path, "%.240s", link);
            }
            snprintf(out[n].label, sizeof out[n].label, "%.120s", e->d_name);
            n++;
        }
        closedir(d);
    }

    /* Fall back to raw device nodes for anything by-id did not cover. */
    static const char *prefixes[] = { "ttyUSB", "ttyACM", "cu.usbserial",
                                      "cu.usbmodem", NULL };
    d = opendir("/dev");
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && n < max) {
            for (int i = 0; prefixes[i]; i++) {
                if (strncmp(e->d_name, prefixes[i], strlen(prefixes[i])) != 0)
                    continue;

                char path[256];
                snprintf(path, sizeof path, "/dev/%.240s", e->d_name);

                bool dup = false;
                for (int j = 0; j < n; j++)
                    if (strcmp(out[j].path, path) == 0)
                        dup = true;
                if (dup)
                    break;

                snprintf(out[n].path, sizeof out[n].path, "%s", path);
                snprintf(out[n].label, sizeof out[n].label, "%.120s", e->d_name);
                n++;
                break;
            }
        }
        closedir(d);
    }

    return n;
}

PlatSerial *plat_serial_open(const char *path, int baud)
{
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
        return NULL;

    struct termios t;
    if (tcgetattr(fd, &t) != 0) {
        close(fd);
        return NULL;
    }

    cfmakeraw(&t);
    t.c_cflag |= (CLOCAL | CREAD);
    t.c_cflag &= ~CRTSCTS;
    t.c_cflag &= ~CSTOPB;                     /* 8N1 */
    t.c_cflag &= ~PARENB;
    t.c_cc[VMIN]  = 0;
    t.c_cc[VTIME] = 0;

    cfsetispeed(&t, baud_const(baud));
    cfsetospeed(&t, baud_const(baud));

    if (tcsetattr(fd, TCSANOW, &t) != 0) {
        close(fd);
        return NULL;
    }
    tcflush(fd, TCIOFLUSH);

    PlatSerial *s = calloc(1, sizeof *s);
    if (!s) {
        close(fd);
        return NULL;
    }
    s->fd = fd;
    return s;
}

void plat_serial_close(PlatSerial *s)
{
    if (!s)
        return;
    close(s->fd);
    free(s);
}

int plat_serial_read(PlatSerial *s, void *buf, size_t cap)
{
    if (!s || cap == 0)
        return -1;

    ssize_t r = read(s->fd, buf, cap);
    if (r < 0)
        return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
               ? 0 : -1;
    /* A zero-byte read on a serial fd means "nothing yet", not EOF —
     * unlike a pipe. Losing the port shows up as an error instead. */
    return (int)r;
}

int plat_serial_write(PlatSerial *s, const void *buf, size_t len)
{
    if (!s)
        return -1;

    ssize_t w = write(s->fd, buf, len);
    if (w < 0)
        return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
               ? 0 : -1;
    return (int)w;
}

/* ---- UDP ----------------------------------------------------------- */

int plat_net_list_ifs(PlatNetIf *out, int max)
{
    struct ifaddrs *head = NULL;
    if (getifaddrs(&head) != 0)
        return 0;

    int n = 0;
    for (struct ifaddrs *ifa = head; ifa && n < max; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
            continue;
        if (!(ifa->ifa_flags & IFF_BROADCAST))
            continue;              /* loopback and point-to-point links */

        /* Connected only: IFF_UP is the administrative state, IFF_RUNNING is
         * the one that means a cable is actually in. */
        if (!(ifa->ifa_flags & IFF_UP) || !(ifa->ifa_flags & IFF_RUNNING))
            continue;

        const struct sockaddr_in *chk = (const struct sockaddr_in *)ifa->ifa_addr;
        if (chk->sin_addr.s_addr == 0)
            continue;              /* up, but no address yet */

        PlatNetIf *e = &out[n];
        memset(e, 0, sizeof *e);
        snprintf(e->name, sizeof e->name, "%s", ifa->ifa_name);

        const struct sockaddr_in *sa = (const struct sockaddr_in *)ifa->ifa_addr;
        inet_ntop(AF_INET, &sa->sin_addr, e->ip, sizeof e->ip);

        /*
         * Derive the directed broadcast from the address and the mask:
         *   bcast = (ip & mask) | ~mask
         *
         * The kernel offers ifa_broadaddr, but it is not guaranteed to be
         * present or correct on every platform and the Windows path has to
         * compute it from the mask regardless, so it is computed here in one
         * place and ifa_broadaddr is only a fallback. Getting this wrong
         * sends the depth report to the wrong subnet's broadcast, where the
         * winch never sees it.
         */
        uint32_t ip = ntohl(sa->sin_addr.s_addr);
        uint32_t mask = 0;

        if (ifa->ifa_netmask) {
            const struct sockaddr_in *m =
                (const struct sockaddr_in *)ifa->ifa_netmask;
            mask = ntohl(m->sin_addr.s_addr);
            inet_ntop(AF_INET, &m->sin_addr, e->mask, sizeof e->mask);
        }

        if (mask != 0 && mask != 0xFFFFFFFFu) {
            uint32_t b = (ip & mask) | (~mask);
            struct in_addr ba;
            ba.s_addr = htonl(b);
            inet_ntop(AF_INET, &ba, e->bcast, sizeof e->bcast);
        } else if (ifa->ifa_broadaddr) {
            const struct sockaddr_in *b =
                (const struct sockaddr_in *)ifa->ifa_broadaddr;
            inet_ntop(AF_INET, &b->sin_addr, e->bcast, sizeof e->bcast);
        }
        if (!e->bcast[0])
            snprintf(e->bcast, sizeof e->bcast, "255.255.255.255");

        e->up = true;              /* filtered above */
        n++;
    }

    freeifaddrs(head);
    return n;
}

struct PlatUdp { int fd; };

PlatUdp *plat_udp_open(const char *bind_ip)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return NULL;

    int on = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof on) != 0) {
        close(fd);
        return NULL;
    }
    fcntl(fd, F_SETFL, O_NONBLOCK);

    /* Bind to an ephemeral port so replies come back to us. Binding to a
     * specific adapter address is what pins the traffic to that wire. */
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = 0;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind_ip && *bind_ip && inet_pton(AF_INET, bind_ip, &a.sin_addr) != 1)
        a.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&a, sizeof a) != 0) {
        close(fd);
        return NULL;
    }

    PlatUdp *u = calloc(1, sizeof *u);
    if (!u) {
        close(fd);
        return NULL;
    }
    u->fd = fd;
    return u;
}

void plat_udp_close(PlatUdp *u)
{
    if (!u)
        return;
    close(u->fd);
    free(u);
}

int plat_udp_send_broadcast(PlatUdp *u, const char *dest_bcast, int port,
                            const void *buf, size_t len)
{
    if (!u)
        return -1;

    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    if (dest_bcast && *dest_bcast)
        inet_pton(AF_INET, dest_bcast, &a.sin_addr);

    ssize_t w = sendto(u->fd, buf, len, 0, (struct sockaddr *)&a, sizeof a);
    return (w < 0) ? -1 : (int)w;
}

int plat_udp_recv(PlatUdp *u, void *buf, size_t cap)
{
    if (!u)
        return -1;

    ssize_t r = recvfrom(u->fd, buf, cap, 0, NULL, NULL);
    if (r < 0)
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
    return (int)r;
}

/* ---- filesystem ---------------------------------------------------- */

bool plat_config_dir(char *buf, size_t cap)
{
    const char *home = getenv("HOME");
    if (!home || !*home)
        return false;

    if ((size_t)snprintf(buf, cap, "%s/.config", home) >= cap)
        return false;
    mkdir(buf, 0755);

    if ((size_t)snprintf(buf, cap, "%s/.config/svpview", home) >= cap)
        return false;
    if (mkdir(buf, 0755) != 0 && errno != EEXIST)
        return false;
    return true;
}

bool plat_path_join(char *buf, size_t cap, const char *dir, const char *leaf)
{
    return (size_t)snprintf(buf, cap, "%s/%s", dir, leaf) < cap;
}

bool plat_file_truncate(const char *path, long len)
{
    return truncate(path, (off_t)len) == 0;
}

#endif /* !_WIN32 */
