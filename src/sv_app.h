/*
 * sv_app.h — application state and instrument session
 *
 * Single-threaded. Every link is non-blocking and polled once per frame by
 * sv_app_poll(), so there is no shared mutable state and no lock anywhere in
 * the program. That is a deliberate change from the threaded sketch in the
 * original architecture note: at 230400 baud a frame's worth of serial data
 * is under 400 bytes, so threads would buy nothing and cost a class of race
 * conditions this program cannot afford.
 */
#ifndef SV_APP_H
#define SV_APP_H

#include "sv_types.h"
#include "sv_proto.h"
#include "sv_export.h"
#include "plat.h"

#define SV_MAX_CASTS   24
#define SV_LIVE_CAP    900         /* 15 minutes at 1 Hz */
#define SV_LOG_LINES   400
#define SV_LOG_WIDTH   200
#define SV_MAX_DIR     128

typedef enum {
    SV_LINK_CLOSED = 0,
    SV_LINK_IDENTIFYING,
    SV_LINK_INTERRUPTED,           /* at the '>' prompt                 */
    SV_LINK_RUN,                   /* streaming; broadcasts arriving    */
    SV_LINK_BUSY                   /* a job is running                  */
} SvLinkState;

typedef enum {
    SV_JOB_NONE = 0,
    SV_JOB_IDENTIFY,
    SV_JOB_READ_CONFIG,
    SV_JOB_WRITE_CONFIG,
    SV_JOB_LIST_DIR,
    SV_JOB_DOWNLOAD,
    SV_JOB_RUN_MODE
} SvJobKind;

typedef enum {
    SV_WINCH_UNKNOWN = 0,
    SV_WINCH_REACHABLE,
    SV_WINCH_SILENT
} SvWinchState;

typedef struct {
    char   name[SV_MAX_NAME];
    bool   is_dir;
    long   size;
} SvFileRow;

typedef struct SvApp SvApp;

/* ------------------------------------------------------------------ */
/* State the UI reads. Owned by the app; never written by the UI.      */
/* ------------------------------------------------------------------ */

typedef struct {
    SvLinkState link;
    SvJobKind   job;
    int         job_step, job_steps;
    char        job_label[64];

    char        port[256];
    char        error[160];         /* last failure, "" when clear      */

    char        serial[32];
    char        firmware[64];

    SvStatus    status;
    bool        status_valid;
    uint64_t    status_ms;          /* when it arrived                  */
    int         checksum_bad;       /* running count                    */

    SvConfig    device;             /* as last read from the instrument */
    bool        device_valid;

    SvLive      live[SV_LIVE_CAP];
    int         live_n;

    SvFileRow   files[SV_MAX_DIR];
    int         n_files;
    char        cwd[SV_MAX_PATH];

    size_t      dl_bytes;
    char        dl_name[SV_MAX_NAME];

    char        net_if[64];         /* adapter name, "" = default route */
    char        net_ip[46];
    char        net_bcast[46];

    SvWinchState winch;
    uint64_t    winch_ack_ms;
    int         winch_seq;
    char        winch_last[128];
    double      winch_last_depth;
} SvState;

/* ------------------------------------------------------------------ */

SvApp *sv_app_create(bool simulate);
void   sv_app_destroy(SvApp *a);

/* Once per frame. Cheap when idle. */
void   sv_app_poll(SvApp *a);

const SvState *sv_app_state(const SvApp *a);

/* Casts held in memory. Index 0 is the most recently added. */
int          sv_app_cast_count(const SvApp *a);
const SvCast *sv_app_cast(const SvApp *a, int i);
void         sv_app_select_cast(SvApp *a, int i);
int          sv_app_selected(const SvApp *a);

/* Log, newest last. */
int         sv_app_log_count(const SvApp *a);
const char *sv_app_log_line(const SvApp *a, int i);

/* ---- operator actions --------------------------------------------- */

bool sv_app_connect(SvApp *a, const char *port);
void sv_app_disconnect(SvApp *a);

void sv_app_read_config(SvApp *a);
void sv_app_write_config(SvApp *a, const SvConfig *cfg);
void sv_app_run_mode(SvApp *a);
void sv_app_interrupt(SvApp *a);

void sv_app_list_dir(SvApp *a, const char *path);
void sv_app_download(SvApp *a, const char *filename);

/* Load a .bin already on disk. Returns NULL or a reason. */
const char *sv_app_open_file(SvApp *a, const char *path);

const char *sv_app_export(SvApp *a, int cast, SvExportFormat f,
                          const char *path);

/*
 * Choose the adapter the winch traffic goes out of. Pass NULL/"" to fall
 * back to the default route. Reopens the socket, so it can be called at any
 * time. Returns NULL or a reason.
 */
const char *sv_app_set_interface(SvApp *a, const PlatNetIf *nif);

/* Winch. report_depth uses the selected cast's maximum depth. */
void sv_app_probe_winch(SvApp *a);
const char *sv_app_report_depth(SvApp *a);

/* Processing, applied to the selected cast in place. */
void sv_app_process(SvApp *a, bool downcast_only, bool despike,
                    double bin_m, int thin_to);

#endif /* SV_APP_H */
