#include "sv_app.h"
#include "sv_binfile.h"
#include "sv_profile.h"
#include "sv_ocean.h"
#include "sv_vigo.h"
#include "sv_sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <unistd.h>

#define RX_CAP        (1 << 20)
#define STEP_TIMEOUT   4000        /* ms for one command to answer       */
#define DL_IDLE_MS      900        /* silence that ends a basic download */
#define DL_TIMEOUT   120000
#define VIGO_PORT      8090

/*
 * One step of a job: send a command, wait for the prompt, route the reply.
 * `dest` says what to do with the payload.
 */
typedef enum {
    DST_DISCARD = 0,
    DST_SERIAL, DST_FIRMWARE,
    DST_MODE, DST_DIRECTION, DST_TRIGGER, DST_INCR, DST_STEP,
    DST_FIXREQ, DST_POWER, DST_BTSLEEP, DST_SITE,
    DST_DIRLIST,
    DST_FILEDATA
} SvDest;

typedef struct {
    char   cmd[160];
    SvDest dest;
} SvStep;

#define MAX_STEPS 20

struct SvApp {
    SvState st;

    PlatSerial *port;
    SvSim      *sim;
    bool        simulate;

    PlatUdp    *udp;

    /* receive assembly */
    char    *rx;
    size_t   rx_n;
    uint64_t rx_last_ms;

    /* current job */
    SvStep   step[MAX_STEPS];
    int      n_step, at_step;
    uint64_t step_started;
    SvConfig pending;              /* config being written              */

    /* download */
    FILE    *dl_file;
    char     dl_path[SV_MAX_PATH];
    bool     dl_echo_done;

    /* casts */
    SvCast   cast[SV_MAX_CASTS];
    int      n_cast;
    int      sel;

    /* log ring */
    char     log[SV_LOG_LINES][SV_LOG_WIDTH];
    int      log_n, log_head;
};

/* ------------------------------------------------------------------ */
/* Logging                                                             */
/* ------------------------------------------------------------------ */

static void logf_(SvApp *a, const char *fmt, ...)
{
    char line[SV_LOG_WIDTH];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);

    snprintf(a->log[a->log_head], SV_LOG_WIDTH, "%s", line);
    a->log_head = (a->log_head + 1) % SV_LOG_LINES;
    if (a->log_n < SV_LOG_LINES)
        a->log_n++;
}

int sv_app_log_count(const SvApp *a) { return a->log_n; }

const char *sv_app_log_line(const SvApp *a, int i)
{
    if (i < 0 || i >= a->log_n)
        return "";
    int first = (a->log_head - a->log_n + SV_LOG_LINES) % SV_LOG_LINES;
    return a->log[(first + i) % SV_LOG_LINES];
}

static void set_error(SvApp *a, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->st.error, sizeof a->st.error, fmt, ap);
    va_end(ap);
    logf_(a, "error: %s", a->st.error);
}

/* ------------------------------------------------------------------ */
/* Link plumbing — the only place that knows sim from real             */
/* ------------------------------------------------------------------ */

static int link_read(SvApp *a, void *buf, size_t cap)
{
    if (a->simulate)
        return a->sim ? sv_sim_read(a->sim, buf, cap) : -1;
    return a->port ? plat_serial_read(a->port, buf, cap) : -1;
}

static int link_write(SvApp *a, const void *buf, size_t len)
{
    if (a->simulate)
        return a->sim ? sv_sim_write(a->sim, buf, len) : -1;
    return a->port ? plat_serial_write(a->port, buf, len) : -1;
}

static bool link_open(const SvApp *a)
{
    return a->simulate ? (a->sim != NULL) : (a->port != NULL);
}

/* ------------------------------------------------------------------ */
/* Job construction                                                    */
/* ------------------------------------------------------------------ */

static void job_reset(SvApp *a)
{
    a->n_step = a->at_step = 0;
    a->st.job = SV_JOB_NONE;
    a->st.job_step = a->st.job_steps = 0;
    a->st.job_label[0] = '\0';
}

static void step_add(SvApp *a, SvDest dest, const char *fmt, ...)
{
    if (a->n_step >= MAX_STEPS)
        return;

    SvStep *s = &a->step[a->n_step++];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s->cmd, sizeof s->cmd, fmt, ap);
    va_end(ap);
    s->dest = dest;
}

static void send_current_step(SvApp *a)
{
    SvStep *s = &a->step[a->at_step];
    a->rx_n = 0;
    a->step_started = plat_now_ms();

    if (link_write(a, s->cmd, strlen(s->cmd)) < 0) {
        set_error(a, "lost the serial link");
        sv_app_disconnect(a);
        return;
    }

    a->st.job_step = a->at_step + 1;
    logf_(a, "> %.*s", (int)strcspn(s->cmd, "\r\n"), s->cmd);
}

static void job_start(SvApp *a, SvJobKind kind, const char *label)
{
    a->st.job = kind;
    a->st.job_steps = a->n_step;
    a->st.job_step = 0;
    a->at_step = 0;
    snprintf(a->st.job_label, sizeof a->st.job_label, "%s", label);
    a->st.link = SV_LINK_BUSY;
    a->st.error[0] = '\0';

    if (a->n_step > 0)
        send_current_step(a);
}

/* ------------------------------------------------------------------ */
/* Reply handling                                                      */
/* ------------------------------------------------------------------ */

static void apply_reply(SvApp *a, SvDest dest, const char *body)
{
    SvConfig *c = &a->st.device;

    switch (dest) {
    case DST_SERIAL:
        snprintf(a->st.serial, sizeof a->st.serial, "%s", body);
        break;
    case DST_FIRMWARE:
        snprintf(a->st.firmware, sizeof a->st.firmware, "%s", body);
        break;
    case DST_MODE:      c->operating_mode = atoi(body); break;
    case DST_DIRECTION: c->direction      = atoi(body); break;
    case DST_TRIGGER:   c->trigger_depth  = atof(body); break;
    case DST_INCR:      c->depth_increment = atof(body); break;
    case DST_STEP:      c->trigger_step   = atof(body); break;
    case DST_FIXREQ:    c->require_fix    = atoi(body); break;
    case DST_POWER:     c->auto_power_min = atoi(body); break;
    case DST_BTSLEEP:   c->bt_sleep_enabled = atoi(body); break;
    case DST_SITE:
        snprintf(c->site, sizeof c->site, "%s", body);
        break;

    case DST_DIRLIST: {
        a->st.n_files = 0;
        const char *p = body;
        while (*p && a->st.n_files < SV_MAX_DIR) {
            const char *nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);

            char line[SV_MAX_LINE];
            if (len >= sizeof line)
                len = sizeof line - 1;
            while (len && (p[len - 1] == '\r' || p[len - 1] == ' '))
                len--;                    /* lines arrive CRLF-terminated */
            memcpy(line, p, len);
            line[len] = '\0';

            SvDirEntry e;
            if (sv_parse_dir_line(line, &e)) {
                SvFileRow *r = &a->st.files[a->st.n_files++];
                snprintf(r->name, sizeof r->name, "%s", e.name);
                r->is_dir = e.is_dir;
                r->size = e.size;
            } else if (strncmp(line, "DIR:", 4) == 0) {
                snprintf(a->st.cwd, sizeof a->st.cwd, "%.*s",
                         (int)sizeof a->st.cwd - 1, line + 4);
            }

            if (!nl)
                break;
            p = nl + 1;
        }
        logf_(a, "%d entries in %s", a->st.n_files, a->st.cwd);
        break;
    }

    default:
        break;
    }
}

/* Add a downloaded or opened cast. Takes ownership of *c. */
static void cast_add(SvApp *a, SvCast *c)
{
    if (a->n_cast == SV_MAX_CASTS) {
        sv_cast_free(&a->cast[SV_MAX_CASTS - 1]);
        a->n_cast--;
    }
    memmove(&a->cast[1], &a->cast[0], (size_t)a->n_cast * sizeof a->cast[0]);
    a->cast[0] = *c;
    a->n_cast++;
    a->sel = 0;

    memset(c, 0, sizeof *c);
}

static void finish_download(SvApp *a)
{
    if (!a->dl_file)
        return;

    /* The prompt the instrument returns to after the transfer is not part
     * of the file. Drop it so the record count comes out exact rather than
     * flagging a phantom truncated record. */
    long end = ftell(a->dl_file);
    if (end > 0) {
        fflush(a->dl_file);
        if (fseek(a->dl_file, end - 1, SEEK_SET) == 0) {
            int last = fgetc(a->dl_file);
            if (last == '>') {
                fclose(a->dl_file);
                a->dl_file = NULL;
                if (truncate(a->dl_path, end - 1) == 0)
                    a->st.dl_bytes--;
            }
        }
    }
    if (a->dl_file)
        fclose(a->dl_file);
    a->dl_file = NULL;

    logf_(a, "downloaded %s, %zu bytes", a->st.dl_name, a->st.dl_bytes);

    const char *err = sv_app_open_file(a, a->dl_path);
    if (err)
        set_error(a, "%s: %s", a->st.dl_name, err);
}

static void job_finished(SvApp *a)
{
    SvJobKind kind = a->st.job;

    if (kind == SV_JOB_DOWNLOAD)
        finish_download(a);

    if (kind == SV_JOB_READ_CONFIG) {
        a->st.device_valid = true;
        logf_(a, "settings read from instrument");
    }
    if (kind == SV_JOB_WRITE_CONFIG) {
        /* Verify by reading everything straight back. */
        logf_(a, "settings written, verifying");
        job_reset(a);
        sv_app_read_config(a);
        return;
    }

    if (kind == SV_JOB_IDENTIFY) {
        a->st.device_valid = true;
        job_reset(a);
        a->st.link = SV_LINK_RUN;      /* the sequence ends with #028 */
        return;
    }

    job_reset(a);
    a->st.link = (kind == SV_JOB_RUN_MODE) ? SV_LINK_RUN : SV_LINK_INTERRUPTED;
}

static void step_complete(SvApp *a, const char *body)
{
    apply_reply(a, a->step[a->at_step].dest, body);

    a->at_step++;
    if (a->at_step >= a->n_step)
        job_finished(a);
    else
        send_current_step(a);
}

/* ------------------------------------------------------------------ */
/* Receive pump                                                        */
/* ------------------------------------------------------------------ */

static void handle_run_bytes(SvApp *a)
{
    /* Split on newlines and parse each complete sentence. */
    size_t start = 0;
    for (size_t i = 0; i < a->rx_n; i++) {
        if (a->rx[i] != '\n')
            continue;

        size_t len = i - start;
        if (len > 0) {
            SvMsg m;
            if (sv_parse_sentence(a->rx + start, len, &m)) {
                if (m.checksum_present && !m.checksum_ok)
                    a->st.checksum_bad++;

                if (m.kind == SV_MSG_STATUS) {
                    a->st.status = m.status;
                    a->st.status_valid = true;
                    a->st.status_ms = plat_now_ms();
                } else if (m.kind == SV_MSG_DATA) {
                    if (a->st.live_n == SV_LIVE_CAP) {
                        memmove(&a->st.live[0], &a->st.live[1],
                                (SV_LIVE_CAP - 1) * sizeof a->st.live[0]);
                        a->st.live_n--;
                    }
                    a->st.live[a->st.live_n++] = m.live;
                }
            }
        }
        start = i + 1;
    }

    if (start > 0) {
        memmove(a->rx, a->rx + start, a->rx_n - start);
        a->rx_n -= start;
    }
    if (a->rx_n > RX_CAP / 2)
        a->rx_n = 0;               /* nothing parseable: do not grow    */
}

static void handle_busy(SvApp *a)
{
    uint64_t now = plat_now_ms();
    SvStep *s = &a->step[a->at_step];

    if (s->dest == DST_FILEDATA) {
        /*
         * The instrument echoes the command before the file data, and the
         * echo is not part of the file: writing it out shifts the whole
         * header and the reader then rejects a perfectly good download.
         * Skip exactly the echoed command and its line ending, and only
         * decide once enough bytes have arrived to compare against.
         */
        if (!a->dl_echo_done) {
            size_t cmd_len = strcspn(s->cmd, "\r\n");
            if (a->rx_n < cmd_len)
                return;                     /* not enough to tell yet */

            size_t skip = 0;
            if (memcmp(a->rx, s->cmd, cmd_len) == 0) {
                skip = cmd_len;
                while (skip < a->rx_n &&
                       (a->rx[skip] == '\r' || a->rx[skip] == '\n'))
                    skip++;
            }
            if (skip > 0) {
                memmove(a->rx, a->rx + skip, a->rx_n - skip);
                a->rx_n -= skip;
            }
            a->dl_echo_done = true;
        }

        /* Basic extraction (#402) has no terminator and no handshake: the
         * file is complete when the instrument stops talking. */
        if (a->rx_n > 0 && a->dl_file) {
            fwrite(a->rx, 1, a->rx_n, a->dl_file);
            a->st.dl_bytes += a->rx_n;
            a->rx_n = 0;
            a->rx_last_ms = now;
        }
        if (a->st.dl_bytes > 0 && now - a->rx_last_ms > DL_IDLE_MS) {
            step_complete(a, "");
            return;
        }
        if (now - a->step_started > DL_TIMEOUT) {
            set_error(a, "download timed out");
            if (a->dl_file) {
                fclose(a->dl_file);
                a->dl_file = NULL;
                remove(a->dl_path);
            }
            job_reset(a);
            a->st.link = SV_LINK_INTERRUPTED;
        }
        return;
    }

    char body[SV_MAX_LINE];
    if (sv_strip_response(a->rx, a->rx_n, s->cmd, body, sizeof body)) {
        if (body[0])
            logf_(a, "< %s", body);
        step_complete(a, body);
        return;
    }

    if (now - a->step_started > STEP_TIMEOUT) {
        set_error(a, "no reply to %.*s",
                  (int)strcspn(s->cmd, "\r\n"), s->cmd);
        job_reset(a);
        a->st.link = SV_LINK_INTERRUPTED;
    }
}

/* ------------------------------------------------------------------ */
/* Winch                                                               */
/* ------------------------------------------------------------------ */

static void poll_winch(SvApp *a)
{
    if (!a->udp)
        return;

    char buf[256];
    int n;
    while ((n = plat_udp_recv(a->udp, buf, sizeof buf - 1)) > 0) {
        buf[n] = '\0';
        switch (sv_vigo_classify(buf, (size_t)n)) {
        case SV_VIGO_REPLY_RESPONDING:
            a->st.winch = SV_WINCH_REACHABLE;
            logf_(a, "winch: responding");
            break;
        case SV_VIGO_REPLY_ACK:
            a->st.winch = SV_WINCH_REACHABLE;
            a->st.winch_ack_ms = plat_now_ms();
            logf_(a, "winch: ACK for %.3f m", a->st.winch_last_depth);
            break;
        case SV_VIGO_REPLY_BTRST:
            logf_(a, "winch: BTRST, spool jogged in");
            break;
        default:
            logf_(a, "winch: unrecognised reply \"%s\"", buf);
            break;
        }
    }
}

void sv_app_probe_winch(SvApp *a)
{
    char msg[128];
    size_t n = sv_vigo_probe(msg, sizeof msg);
    if (!n || !a->udp)
        return;

    if (plat_udp_send_broadcast(a->udp, VIGO_PORT, msg, n) < 0) {
        set_error(a, "cannot broadcast on UDP %d", VIGO_PORT);
        return;
    }
    if (a->st.winch == SV_WINCH_UNKNOWN)
        a->st.winch = SV_WINCH_SILENT;
    logf_(a, "winch probe sent");
}

const char *sv_app_report_depth(SvApp *a)
{
    if (a->sel < 0 || a->sel >= a->n_cast)
        return "no cast selected";
    if (!a->udp)
        return "no network socket";

    double depth = a->cast[a->sel].max_depth;

    const char *bad = sv_vigo_depth_rejection(depth, 0.0);
    if (bad)
        return bad;

    a->st.winch_seq = sv_vigo_next_seq(a->st.winch_seq);

    char msg[128];
    size_t n = sv_vigo_depth(msg, sizeof msg, a->st.winch_seq, depth);
    if (!n)
        return "message would not fit";

    if (plat_udp_send_broadcast(a->udp, VIGO_PORT, msg, n) < 0)
        return "broadcast failed";

    snprintf(a->st.winch_last, sizeof a->st.winch_last, "%s", msg);
    a->st.winch_last_depth = depth;
    logf_(a, "winch < %s", msg);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

SvApp *sv_app_create(bool simulate)
{
    SvApp *a = calloc(1, sizeof *a);
    if (!a)
        return NULL;

    a->rx = malloc(RX_CAP);
    if (!a->rx) {
        free(a);
        return NULL;
    }

    a->simulate = simulate;
    a->sel = -1;
    a->udp = plat_udp_open();
    if (!a->udp)
        logf_(a, "no UDP socket: winch reporting unavailable");

    logf_(a, "svpview ready%s", simulate ? " (simulated instrument)" : "");
    return a;
}

void sv_app_destroy(SvApp *a)
{
    if (!a)
        return;

    for (int i = 0; i < a->n_cast; i++)
        sv_cast_free(&a->cast[i]);
    if (a->dl_file)
        fclose(a->dl_file);
    if (a->port)
        plat_serial_close(a->port);
    if (a->sim)
        sv_sim_destroy(a->sim);
    if (a->udp)
        plat_udp_close(a->udp);

    free(a->rx);
    free(a);
}

const SvState *sv_app_state(const SvApp *a) { return &a->st; }
int   sv_app_cast_count(const SvApp *a)     { return a->n_cast; }
int   sv_app_selected(const SvApp *a)       { return a->sel; }

const SvCast *sv_app_cast(const SvApp *a, int i)
{
    return (i >= 0 && i < a->n_cast) ? &a->cast[i] : NULL;
}

void sv_app_select_cast(SvApp *a, int i)
{
    if (i >= 0 && i < a->n_cast)
        a->sel = i;
}

/* ------------------------------------------------------------------ */
/* Poll                                                                */
/* ------------------------------------------------------------------ */

void sv_app_poll(SvApp *a)
{
    poll_winch(a);

    if (!link_open(a))
        return;

    if (a->simulate)
        sv_sim_tick(a->sim, (unsigned long)plat_now_ms());

    /* Drain the link into the assembly buffer. */
    for (;;) {
        if (a->rx_n >= RX_CAP)
            break;

        int n = link_read(a, a->rx + a->rx_n, RX_CAP - a->rx_n);
        if (n < 0) {
            set_error(a, "serial link lost");
            sv_app_disconnect(a);
            return;
        }
        if (n == 0)
            break;

        a->rx_n += (size_t)n;
        a->rx_last_ms = plat_now_ms();
    }

    if (a->st.link == SV_LINK_BUSY)
        handle_busy(a);
    else
        handle_run_bytes(a);
}

/* ------------------------------------------------------------------ */
/* Actions                                                             */
/* ------------------------------------------------------------------ */

bool sv_app_connect(SvApp *a, const char *port)
{
    sv_app_disconnect(a);
    a->st.error[0] = '\0';

    if (a->simulate) {
        a->sim = sv_sim_create();
        if (!a->sim) {
            set_error(a, "cannot start the simulator");
            return false;
        }
        snprintf(a->st.port, sizeof a->st.port, "simulator");
    } else {
        a->port = plat_serial_open(port, 230400);
        if (!a->port) {
            set_error(a, "cannot open %s", port);
            return false;
        }
        snprintf(a->st.port, sizeof a->st.port, "%s", port);
    }

    logf_(a, "opened %s", a->st.port);

    /*
     * Interrupt, identify, read the settings, then put the instrument back
     * into run mode. Ending anywhere else would leave it interrupted, and an
     * instrument deployed while interrupted records no profile at all
     * (guide section 2) — so the connect sequence has to hand it back in a
     * state it can actually be used in.
     */
    a->n_step = 0;
    step_add(a, DST_DISCARD,   "#\r\n");
    step_add(a, DST_SERIAL,    "#%03d\r\n", SV_CMD_SERIAL);
    step_add(a, DST_FIRMWARE,  "#%03d\r\n", SV_CMD_FIRMWARE);
    step_add(a, DST_MODE,      "#%03d\r\n", SV_CMD_GET_MODE);
    step_add(a, DST_DIRECTION, "#%03d\r\n", SV_CMD_GET_DIR);
    step_add(a, DST_TRIGGER,   "#%03d\r\n", SV_CMD_GET_TRIGGER);
    step_add(a, DST_INCR,      "#%03d\r\n", SV_CMD_GET_INCR);
    step_add(a, DST_STEP,      "#%03d\r\n", SV_CMD_GET_STEP);
    step_add(a, DST_FIXREQ,    "#%03d\r\n", SV_CMD_GET_FIXREQ);
    step_add(a, DST_POWER,     "#%03d\r\n", SV_CMD_GET_POWER);
    step_add(a, DST_BTSLEEP,   "#%03d\r\n", SV_CMD_GET_BTSLEEP);
    step_add(a, DST_SITE,      "#%03d\r\n", SV_CMD_GET_SITE);
    step_add(a, DST_DISCARD,   "#%03d\r\n", SV_CMD_RUN);
    job_start(a, SV_JOB_IDENTIFY, "Identifying");
    a->st.link = SV_LINK_BUSY;
    return true;
}

void sv_app_disconnect(SvApp *a)
{
    if (a->dl_file) {
        fclose(a->dl_file);
        a->dl_file = NULL;
        remove(a->dl_path);
    }
    if (a->port) {
        plat_serial_close(a->port);
        a->port = NULL;
    }
    if (a->sim) {
        sv_sim_destroy(a->sim);
        a->sim = NULL;
    }

    job_reset(a);
    a->st.link = SV_LINK_CLOSED;
    a->st.status_valid = false;
    a->st.device_valid = false;
    a->st.live_n = 0;
    a->st.n_files = 0;
    a->rx_n = 0;
    a->st.serial[0] = a->st.firmware[0] = '\0';
}

void sv_app_interrupt(SvApp *a)
{
    if (!link_open(a) || a->st.link == SV_LINK_BUSY)
        return;

    a->n_step = 0;
    step_add(a, DST_DISCARD, "#\r\n");
    job_start(a, SV_JOB_IDENTIFY, "Interrupting");
}

void sv_app_run_mode(SvApp *a)
{
    if (!link_open(a) || a->st.link == SV_LINK_BUSY)
        return;

    a->n_step = 0;
    step_add(a, DST_DISCARD, "#%03d\r\n", SV_CMD_RUN);
    job_start(a, SV_JOB_RUN_MODE, "Entering run mode");
}

void sv_app_read_config(SvApp *a)
{
    if (!link_open(a) || a->st.link == SV_LINK_BUSY)
        return;

    a->n_step = 0;
    step_add(a, DST_MODE,      "#%03d\r\n", SV_CMD_GET_MODE);
    step_add(a, DST_DIRECTION, "#%03d\r\n", SV_CMD_GET_DIR);
    step_add(a, DST_TRIGGER,   "#%03d\r\n", SV_CMD_GET_TRIGGER);
    step_add(a, DST_INCR,      "#%03d\r\n", SV_CMD_GET_INCR);
    step_add(a, DST_STEP,      "#%03d\r\n", SV_CMD_GET_STEP);
    step_add(a, DST_FIXREQ,    "#%03d\r\n", SV_CMD_GET_FIXREQ);
    step_add(a, DST_POWER,     "#%03d\r\n", SV_CMD_GET_POWER);
    step_add(a, DST_BTSLEEP,   "#%03d\r\n", SV_CMD_GET_BTSLEEP);
    step_add(a, DST_SITE,      "#%03d\r\n", SV_CMD_GET_SITE);
    job_start(a, SV_JOB_READ_CONFIG, "Reading settings");
}

void sv_app_write_config(SvApp *a, const SvConfig *cfg)
{
    if (!link_open(a) || a->st.link == SV_LINK_BUSY || !cfg)
        return;

    SvConfig c = *cfg;
    sv_config_clamp(&c);
    a->pending = c;

    /* Only write what actually differs — every write is a flash cycle on
     * the instrument, and a shorter sequence is a shorter window in which
     * something can go wrong. */
    const SvConfig *d = &a->st.device;
    a->n_step = 0;

    if (!a->st.device_valid || c.operating_mode != d->operating_mode)
        step_add(a, DST_DISCARD, "#%03d;%d\r\n", SV_CMD_SET_MODE, c.operating_mode);
    if (!a->st.device_valid || c.direction != d->direction)
        step_add(a, DST_DISCARD, "#%03d;%d\r\n", SV_CMD_SET_DIR, c.direction);
    if (!a->st.device_valid || c.trigger_depth != d->trigger_depth)
        step_add(a, DST_DISCARD, "#%03d;%.2f\r\n", SV_CMD_SET_TRIGGER, c.trigger_depth);
    if (!a->st.device_valid || c.depth_increment != d->depth_increment)
        step_add(a, DST_DISCARD, "#%03d;%.2f\r\n", SV_CMD_SET_INCR, c.depth_increment);
    if (!a->st.device_valid || c.trigger_step != d->trigger_step)
        step_add(a, DST_DISCARD, "#%03d;%.2f\r\n", SV_CMD_SET_STEP, c.trigger_step);
    if (!a->st.device_valid || c.require_fix != d->require_fix)
        step_add(a, DST_DISCARD, "#%03d;%d\r\n", SV_CMD_SET_FIXREQ, c.require_fix);
    if (!a->st.device_valid || c.auto_power_min != d->auto_power_min)
        step_add(a, DST_DISCARD, "#%03d;%d\r\n", SV_CMD_SET_POWER, c.auto_power_min);
    if (!a->st.device_valid || c.bt_sleep_enabled != d->bt_sleep_enabled)
        step_add(a, DST_DISCARD, "#%03d;%d\r\n", SV_CMD_SET_BTSLEEP, c.bt_sleep_enabled);
    if (!a->st.device_valid || strcmp(c.site, d->site) != 0)
        step_add(a, DST_DISCARD, "#%03d;%s\r\n", SV_CMD_SET_SITE, c.site);

    if (a->n_step == 0) {
        logf_(a, "settings unchanged, nothing to write");
        return;
    }
    job_start(a, SV_JOB_WRITE_CONFIG, "Writing settings");
}

void sv_app_list_dir(SvApp *a, const char *path)
{
    if (!link_open(a) || a->st.link == SV_LINK_BUSY)
        return;

    a->n_step = 0;
    if (path && *path)
        step_add(a, DST_DISCARD, "#%03d;%s\r\n", SV_CMD_CHDIR, path);
    step_add(a, DST_DIRLIST, "#%03d\r\n", SV_CMD_DIR_LIST);
    job_start(a, SV_JOB_LIST_DIR, "Listing directory");
}

void sv_app_download(SvApp *a, const char *filename)
{
    if (!link_open(a) || a->st.link == SV_LINK_BUSY || !filename || !*filename)
        return;

    char dir[SV_MAX_PATH];
    if (!plat_config_dir(dir, sizeof dir)) {
        set_error(a, "no writable config directory");
        return;
    }
    if (!plat_path_join(a->dl_path, sizeof a->dl_path, dir, filename)) {
        set_error(a, "download path too long");
        return;
    }

    a->dl_file = fopen(a->dl_path, "wb");
    if (!a->dl_file) {
        set_error(a, "cannot write %s", a->dl_path);
        return;
    }

    a->st.dl_bytes = 0;
    a->dl_echo_done = false;
    snprintf(a->st.dl_name, sizeof a->st.dl_name, "%s", filename);

    a->n_step = 0;
    step_add(a, DST_FILEDATA, "#%03d;%s\r\n", SV_CMD_EXTRACT, filename);
    job_start(a, SV_JOB_DOWNLOAD, "Downloading");
    a->rx_last_ms = plat_now_ms();
}

const char *sv_app_open_file(SvApp *a, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return "cannot open file";

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return "cannot read file";
    }
    long len = ftell(f);
    rewind(f);

    if (len <= 0 || len > 64 * 1024 * 1024) {
        fclose(f);
        return "file is empty or implausibly large";
    }

    unsigned char *buf = malloc((size_t)len);
    if (!buf) {
        fclose(f);
        return "out of memory";
    }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);

    SvCast c;
    const char *err = sv_binfile_parse(buf, got, &c, NULL);
    free(buf);
    if (err)
        return err;

    const char *base = strrchr(path, '/');
    snprintf(c.name, sizeof c.name, "%s", base ? base + 1 : path);

    sv_profile_derive(&c, c.has_fix ? c.lat : SV_DEFAULT_LAT);
    cast_add(a, &c);

    logf_(a, "%s: %d samples, %.2f m", a->cast[0].name,
          a->cast[0].n, a->cast[0].max_depth);
    return NULL;
}

const char *sv_app_export(SvApp *a, int cast, SvExportFormat f,
                          const char *path)
{
    const SvCast *c = sv_app_cast(a, cast);
    if (!c)
        return "no cast selected";

    const char *err = sv_export_write(c, f, path);
    if (err)
        logf_(a, "export failed: %s", err);
    else
        logf_(a, "exported %s as %s", c->name, sv_export_name(f));
    return err;
}

void sv_app_process(SvApp *a, bool downcast_only, bool despike,
                    double bin_m, int thin_to)
{
    if (a->sel < 0 || a->sel >= a->n_cast)
        return;

    SvCast *c = &a->cast[a->sel];
    int before = c->n;

    if (downcast_only)
        sv_profile_keep_downcast(c, 0.2);
    if (despike)
        sv_profile_despike(c, 3.0);
    if (bin_m > 0.0)
        sv_profile_bin(c, bin_m);
    if (thin_to > 1)
        sv_profile_thin(c, thin_to);

    sv_profile_derive(c, c->has_fix ? c->lat : SV_DEFAULT_LAT);
    logf_(a, "processed %s: %d samples from %d", c->name, c->n, before);
}
