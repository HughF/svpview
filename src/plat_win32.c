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
 * plat_win32.c — Windows implementation of plat.h.
 *
 * POSIX lives in plat_posix.c; the two are never compiled together. Both
 * present the same contract to the program above: every call returns
 * immediately, because the whole application is polled once per frame and
 * anything that blocks here freezes the window.
 *
 * Built with mingw-w64 — see the `windows` target in the Makefile. Nothing
 * here needs MSVC.
 */
#if defined(_WIN32)

/* Windows 7: OnLinkPrefixLength in IP_ADAPTER_UNICAST_ADDRESS, and the
 * SIO_UDP_CONNRESET ioctl, are both gated on the target version. */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN

/* winsock2.h before windows.h: the reverse pulls in the 1.1 winsock.h and
 * the two collide on every socket type. */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <setupapi.h>
#include <devguid.h>
#include <mstcpip.h>
#include <mswsock.h>          /* SIO_UDP_CONNRESET; see plat_udp_open() */

#include "plat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- time ---------------------------------------------------------- */

uint64_t plat_now_ms(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;

    if (freq.QuadPart == 0 && !QueryPerformanceFrequency(&freq))
        return (uint64_t)GetTickCount64();

    QueryPerformanceCounter(&now);
    return (uint64_t)((now.QuadPart * 1000) / freq.QuadPart);
}

/* ---- serial -------------------------------------------------------- */

struct PlatSerial { HANDLE h; };

static bool port_seen(const PlatPortInfo *out, int n, const char *com)
{
    for (int i = 0; i < n; i++)
        if (strstr(out[i].path, com) && strlen(strstr(out[i].path, com)) ==
            strlen(com))
            return true;
    return false;
}

static void port_add(PlatPortInfo *out, int *n, const char *com,
                     const char *label)
{
    /*
     * COM10 and above need the \\.\ prefix — CreateFile on a bare "COM10"
     * fails, because only COM1..COM9 exist in the DOS device namespace. The
     * prefix is harmless on the low numbers, so every port gets it.
     */
    snprintf(out[*n].path, sizeof out[*n].path, "\\\\.\\%.200s", com);
    snprintf(out[*n].label, sizeof out[*n].label, "%.120s",
             (label && *label) ? label : com);
    (*n)++;
}

int plat_serial_list(PlatPortInfo *out, int max)
{
    int n = 0;

    /*
     * SetupAPI, for the name the operator actually needs: the friendly name
     * distinguishes "Standard Serial over Bluetooth link (COM7)" from
     * "USB Serial Port (COM3)", which is the difference between the
     * Valeport Bluetooth key and the comms cable — and the instrument will
     * not give a GPS fix while the cable is in.
     */
    HDEVINFO di = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, NULL, NULL,
                                       DIGCF_PRESENT);
    if (di != INVALID_HANDLE_VALUE) {
        SP_DEVINFO_DATA dev;
        dev.cbSize = sizeof dev;

        for (DWORD i = 0; n < max && SetupDiEnumDeviceInfo(di, i, &dev); i++) {
            char com[64] = { 0 };

            HKEY key = SetupDiOpenDevRegKey(di, &dev, DICS_FLAG_GLOBAL, 0,
                                            DIREG_DEV, KEY_READ);
            if (key == INVALID_HANDLE_VALUE)
                continue;

            DWORD type = 0, cb = sizeof com - 1;
            LONG r = RegQueryValueExA(key, "PortName", NULL, &type,
                                      (LPBYTE)com, &cb);
            RegCloseKey(key);

            if (r != ERROR_SUCCESS || type != REG_SZ || com[0] == '\0')
                continue;
            if (strncmp(com, "COM", 3) != 0)
                continue;              /* LPT and friends live here too */

            char label[192] = { 0 };
            DWORD lb = sizeof label - 1;
            if (!SetupDiGetDeviceRegistryPropertyA(di, &dev, SPDRP_FRIENDLYNAME,
                                                   NULL, (PBYTE)label, lb, NULL))
                SetupDiGetDeviceRegistryPropertyA(di, &dev, SPDRP_DEVICEDESC,
                                                  NULL, (PBYTE)label, lb, NULL);

            port_add(out, &n, com, label[0] ? label : com);
        }
        SetupDiDestroyDeviceInfoList(di);
    }

    /*
     * Anything SetupAPI missed. A few virtual-port drivers register a COM
     * number without a device node, and a port that is not listed cannot be
     * chosen at all — so the registry's own map is read as a backstop.
     */
    HKEY map;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ,
                      &map) == ERROR_SUCCESS) {
        for (DWORD i = 0; n < max; i++) {
            char name[256], com[64];
            DWORD nb = sizeof name, cb = sizeof com, type = 0;

            if (RegEnumValueA(map, i, name, &nb, NULL, &type,
                              (LPBYTE)com, &cb) != ERROR_SUCCESS)
                break;
            if (type != REG_SZ || strncmp(com, "COM", 3) != 0)
                continue;
            if (port_seen(out, n, com))
                continue;

            port_add(out, &n, com, com);
        }
        RegCloseKey(map);
    }

    return n;
}

PlatSerial *plat_serial_open(const char *path, int baud)
{
    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;

    /* 64 KB each way. The default driver buffer is small enough that a
     * frame's worth of 230400 baud can overrun it while the window is
     * busy repainting. */
    SetupComm(h, 65536, 65536);

    DCB dcb;
    memset(&dcb, 0, sizeof dcb);
    dcb.DCBlength = sizeof dcb;
    if (!GetCommState(h, &dcb)) {
        CloseHandle(h);
        return NULL;
    }

    dcb.BaudRate        = (DWORD)baud;
    dcb.ByteSize        = 8;
    dcb.Parity          = NOPARITY;
    dcb.StopBits        = ONESTOPBIT;
    dcb.fBinary         = TRUE;
    dcb.fParity         = FALSE;
    dcb.fOutxCtsFlow    = FALSE;
    dcb.fOutxDsrFlow    = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX           = FALSE;
    dcb.fInX            = FALSE;
    dcb.fNull           = FALSE;
    dcb.fAbortOnError   = FALSE;

    /* DTR and RTS asserted, matching what opening the port does on Linux:
     * some USB bridges hold the device in reset until they are. */
    dcb.fDtrControl     = DTR_CONTROL_ENABLE;
    dcb.fRtsControl     = RTS_CONTROL_ENABLE;

    if (!SetCommState(h, &dcb)) {
        CloseHandle(h);
        return NULL;
    }

    /*
     * ReadIntervalTimeout = MAXDWORD with both total timeouts zero is the
     * documented way to say "return whatever is buffered, immediately, and
     * never wait" — the same contract as O_NONBLOCK on the POSIX side.
     *
     * Writes are the one place this cannot be promised without overlapped
     * I/O. Everything written here is a command of a few dozen bytes, which
     * a 230400 baud port takes microseconds over, so the timeout is a
     * backstop against a wedged driver rather than a normal path.
     */
    COMMTIMEOUTS to;
    memset(&to, 0, sizeof to);
    to.ReadIntervalTimeout         = MAXDWORD;
    to.ReadTotalTimeoutMultiplier  = 0;
    to.ReadTotalTimeoutConstant    = 0;
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant   = 250;
    if (!SetCommTimeouts(h, &to)) {
        CloseHandle(h);
        return NULL;
    }

    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT);

    PlatSerial *s = calloc(1, sizeof *s);
    if (!s) {
        CloseHandle(h);
        return NULL;
    }
    s->h = h;
    return s;
}

void plat_serial_close(PlatSerial *s)
{
    if (!s)
        return;
    CloseHandle(s->h);
    free(s);
}

int plat_serial_read(PlatSerial *s, void *buf, size_t cap)
{
    if (!s || cap == 0)
        return -1;

    /*
     * Clear any framing or overrun error first. The driver latches an error
     * and refuses every subsequent read until it is cleared, so one burst of
     * line noise would otherwise look like the instrument going silent for
     * good — and it also reports the port vanishing, which is what unplugging
     * the Bluetooth key mid-session looks like.
     */
    DWORD errs = 0;
    COMSTAT st;
    if (!ClearCommError(s->h, &errs, &st))
        return -1;

    DWORD got = 0;
    if (!ReadFile(s->h, buf, (DWORD)cap, &got, NULL))
        return -1;
    return (int)got;
}

int plat_serial_write(PlatSerial *s, const void *buf, size_t len)
{
    if (!s)
        return -1;

    DWORD put = 0;
    if (!WriteFile(s->h, buf, (DWORD)len, &put, NULL))
        return -1;
    return (int)put;
}

/* ---- network interfaces -------------------------------------------- */

static void ip_to_text(uint32_t host_order, char *buf, size_t cap)
{
    struct in_addr a;
    a.s_addr = htonl(host_order);
    snprintf(buf, cap, "%s", inet_ntoa(a));
}

int plat_net_list_ifs(PlatNetIf *out, int max)
{
    ULONG flags = GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_ANYCAST |
                  GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG size = 16384;
    IP_ADAPTER_ADDRESSES *list = NULL;
    ULONG rc = 0;

    /* The size is a guess the first time and an answer the second. */
    for (int attempt = 0; attempt < 3; attempt++) {
        free(list);
        list = malloc(size);
        if (!list)
            return 0;

        rc = GetAdaptersAddresses(AF_INET, flags, NULL, list, &size);
        if (rc != ERROR_BUFFER_OVERFLOW)
            break;
    }
    if (rc != NO_ERROR) {
        free(list);
        return 0;
    }

    int n = 0;
    for (IP_ADAPTER_ADDRESSES *a = list; a && n < max; a = a->Next) {
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;
        if (a->OperStatus != IfOperStatusUp)
            continue;              /* not connected: no cable, or disabled */

        for (IP_ADAPTER_UNICAST_ADDRESS *u = a->FirstUnicastAddress;
             u && n < max; u = u->Next) {

            if (!u->Address.lpSockaddr ||
                u->Address.lpSockaddr->sa_family != AF_INET)
                continue;

            const struct sockaddr_in *sa =
                (const struct sockaddr_in *)u->Address.lpSockaddr;
            uint32_t ip = ntohl(sa->sin_addr.s_addr);
            if (ip == 0)
                continue;

            PlatNetIf *e = &out[n];
            memset(e, 0, sizeof *e);

            /*
             * FriendlyName ("Ethernet 2"), not AdapterName, which is the
             * adapter's GUID and means nothing to anyone.
             */
            if (a->FriendlyName)
                WideCharToMultiByte(CP_UTF8, 0, a->FriendlyName, -1,
                                    e->name, (int)sizeof e->name - 1,
                                    NULL, NULL);
            if (!e->name[0])
                snprintf(e->name, sizeof e->name, "%.60s", a->AdapterName);

            /*
             * Windows gives a prefix length where POSIX gives a mask. The
             * broadcast is then the same arithmetic as the other platform,
             * bcast = (ip & mask) | ~mask, and it is computed rather than
             * taken on trust because sending to the wrong subnet's broadcast
             * is exactly how a depth report goes missing.
             */
            unsigned prefix = u->OnLinkPrefixLength;
            if (prefix > 32)
                prefix = 32;
            uint32_t mask = prefix ? (uint32_t)(0xFFFFFFFFu << (32 - prefix))
                                   : 0;

            ip_to_text(ip, e->ip, sizeof e->ip);
            ip_to_text(mask, e->mask, sizeof e->mask);

            if (mask != 0 && mask != 0xFFFFFFFFu)
                ip_to_text((ip & mask) | ~mask, e->bcast, sizeof e->bcast);
            else
                snprintf(e->bcast, sizeof e->bcast, "255.255.255.255");

            e->up = true;
            n++;
        }
    }

    free(list);
    return n;
}

/* ---- UDP ----------------------------------------------------------- */

struct PlatUdp { SOCKET s; };

/*
 * Winsock has to be started before any socket call and reference-counted per
 * process. There is one socket in this program and it is opened from the
 * frame loop, so the count is kept here and the matching WSACleanup is left
 * to process exit — Windows releases it either way.
 */
static bool winsock_up(void)
{
    static bool started = false;
    WSADATA wsa;

    if (started)
        return true;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return false;
    started = true;
    return true;
}

PlatUdp *plat_udp_open(const char *bind_ip)
{
    if (!winsock_up())
        return NULL;

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
        return NULL;

    BOOL on = TRUE;
    if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char *)&on,
                   sizeof on) != 0) {
        closesocket(s);
        return NULL;
    }

    u_long nonblock = 1;
    ioctlsocket(s, FIONBIO, &nonblock);

    /*
     * Stop a refused datagram from killing the socket.
     *
     * On Windows a UDP socket that provokes an ICMP port-unreachable — which
     * is precisely what happens when the winch is switched off — starts
     * failing every later recvfrom with WSAECONNRESET, permanently. The
     * program would read that as a dead socket and stop listening for the
     * acknowledgement that arrives when the winch comes back. This ioctl is
     * the documented way to switch that behaviour off, and has no POSIX
     * equivalent because POSIX never had the problem.
     */
    BOOL reset = FALSE;
    DWORD ret = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &reset, sizeof reset, NULL, 0, &ret,
             NULL, NULL);

    /* An ephemeral port, so replies come back to us; binding to a specific
     * adapter address is what pins the traffic to that wire. */
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = 0;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind_ip && *bind_ip) {
        struct in_addr got;
        if (inet_pton(AF_INET, bind_ip, &got) == 1)
            a.sin_addr = got;
    }

    if (bind(s, (struct sockaddr *)&a, sizeof a) != 0) {
        closesocket(s);
        return NULL;
    }

    PlatUdp *u = calloc(1, sizeof *u);
    if (!u) {
        closesocket(s);
        return NULL;
    }
    u->s = s;
    return u;
}

void plat_udp_close(PlatUdp *u)
{
    if (!u)
        return;
    closesocket(u->s);
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
    if (dest_bcast && *dest_bcast) {
        struct in_addr got;
        if (inet_pton(AF_INET, dest_bcast, &got) == 1)
            a.sin_addr = got;
    }

    int w = sendto(u->s, (const char *)buf, (int)len, 0,
                   (struct sockaddr *)&a, sizeof a);
    return (w == SOCKET_ERROR) ? -1 : w;
}

int plat_udp_recv(PlatUdp *u, void *buf, size_t cap)
{
    if (!u)
        return -1;

    int r = recvfrom(u->s, (char *)buf, (int)cap, 0, NULL, NULL);
    if (r == SOCKET_ERROR) {
        int e = WSAGetLastError();
        return (e == WSAEWOULDBLOCK || e == WSAECONNRESET) ? 0 : -1;
    }
    return r;
}

/* ---- filesystem ---------------------------------------------------- */

bool plat_config_dir(char *buf, size_t cap)
{
    const char *appdata = getenv("APPDATA");
    if (!appdata || !*appdata)
        return false;

    if ((size_t)snprintf(buf, cap, "%s\\svpview", appdata) >= cap)
        return false;

    if (!CreateDirectoryA(buf, NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS)
        return false;
    return true;
}

bool plat_path_join(char *buf, size_t cap, const char *dir, const char *leaf)
{
    return (size_t)snprintf(buf, cap, "%s\\%s", dir, leaf) < cap;
}

bool plat_file_truncate(const char *path, long len)
{
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER to;
    to.QuadPart = len;

    bool ok = SetFilePointerEx(h, to, NULL, FILE_BEGIN) && SetEndOfFile(h);
    CloseHandle(h);
    return ok;
}

#endif /* _WIN32 */
