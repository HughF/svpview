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
#define SVPVIEW_VERSION "2026.08.12"
#define SVPVIEW_TAGLINE "Valeport SWiFT profiler acquisition and display"

#endif /* SV_VERSION_H */
