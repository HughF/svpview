/*
 * sv_binfile.h — SWiFT .bin logged-file decoder
 *
 * Takes a whole file in memory as (ptr, len) and never reads outside it.
 * This is the one parser that consumes data straight off a flaky Bluetooth
 * link, so it treats every length in the file as hostile: header_size, the
 * firmware string terminator and the sample count are all cross-checked
 * against the actual buffer length before use.
 *
 * Reference: docs/SWIFT_PROTOCOL.md section 7.
 */
#ifndef SV_BINFILE_H
#define SV_BINFILE_H

#include "sv_types.h"
#include <stddef.h>

typedef enum {
    SV_FW_UNKNOWN = 0,
    SV_FW_0650734,          /* oldest: 16-byte records, no optics       */
    SV_FW_0650735_ABC,      /* adds optics                              */
    SV_FW_0650735_D0        /* adds CTD conductivity + Unicode site info */
} SvFirmwareVariant;

typedef struct {
    SvFirmwareVariant variant;
    size_t  header_size;        /* as declared in the file            */
    size_t  data_offset;        /* where samples actually start       */
    size_t  record_size;        /* 16 or 24                           */
    int     n_records;
    bool    header_size_trusted;/* false if we had to recover it      */
    bool    truncated;          /* trailing partial record discarded  */
} SvBinInfo;

/*
 * Decode into *cast, which is zeroed first and owns cast->s afterwards
 * (free with sv_cast_free()). info may be NULL.
 *
 * Returns NULL on success, or a short human-readable reason on failure. The
 * reason is a string literal, safe to display and never to be freed.
 */
const char *sv_binfile_parse(const void *data, size_t len,
                             SvCast *cast, SvBinInfo *info);

/* Release the sample array. Safe on a zeroed struct and safe twice. */
void sv_cast_free(SvCast *cast);

/* Identify the firmware variant from the version string alone. Exposed for
 * the file browser, which shows it before committing to a download. */
SvFirmwareVariant sv_firmware_variant(const char *version);

#endif /* SV_BINFILE_H */
