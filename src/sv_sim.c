#include "sv_sim.h"
#include "sv_proto.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define OUT_CAP   (1 << 20)          /* 1 MB: a whole synthetic file fits */
#define LINE_CAP  256

/* The simulated instrument's own settings, in the same units as the wire. */
typedef struct {
    int    mode;                     /* 0 continuous, 1 smart profile */
    int    direction;
    double trigger_depth;
    double depth_increment;
    double trigger_step;
    int    require_fix;
    int    auto_power;
    int    bt_sleep;
    char   site[SV_SITE_LEN];
} SimSettings;

/*
 * A cast already on the card.
 *
 * A stored cast was recorded at the time and place the instrument was then,
 * not where it is now — so each one carries its own timestamp and position,
 * laid out along the survey line behind the vessel. Getting this wrong in the
 * emulator would have made every downloaded cast plot on top of the boat.
 */
typedef struct {
    char   name[40];
    int    hh, mm, ss;
    double lat, lon;
    double max_depth;
} SimLogged;

#define SIM_LOGGED 4
#define SIM_SPACING_M 320.0        /* between casts on the line */

struct SvSim {
    unsigned char out[OUT_CAP];
    size_t out_head, out_tail;

    char   line[LINE_CAP];
    size_t line_n;

    bool   interrupted;              /* '#' received, at the '>' prompt   */
    unsigned long last_status_ms;
    unsigned long last_data_ms;
    unsigned long boot_ms;

    SimSettings set;
    int    cast_counter;
    double battery_hours;

    /* Simulated vessel: a survey line at 4 kn, gently turning, so successive
     * casts are at different positions and the chart has real geometry to
     * draw. Integrated here rather than derived from a formula so the
     * broadcast position and the position written into a downloaded file are
     * necessarily the same one. */
    double lat, lon, course_deg;
    unsigned long last_move_ms;

    SimLogged logged[SIM_LOGGED];   /* oldest first */
};

/* ------------------------------------------------------------------ */
/* Output ring                                                         */
/* ------------------------------------------------------------------ */

static void emit(SvSim *s, const void *data, size_t len)
{
    const unsigned char *p = data;
    for (size_t i = 0; i < len; i++) {
        size_t next = (s->out_head + 1) % OUT_CAP;
        if (next == s->out_tail)
            return;                  /* full: drop, exactly like a UART */
        s->out[s->out_head] = p[i];
        s->out_head = next;
    }
}

static void emits(SvSim *s, const char *str)
{
    emit(s, str, strlen(str));
}

static void emitf(SvSim *s, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > 0)
        emit(s, buf, (size_t)n < sizeof buf ? (size_t)n : sizeof buf - 1);
}

/* ------------------------------------------------------------------ */
/* Synthetic .bin file                                                 */
/* ------------------------------------------------------------------ */

static void bin_u8(unsigned char **p, unsigned v)  { *(*p)++ = (unsigned char)v; }
static void bin_u16(unsigned char **p, unsigned v)
{
    bin_u8(p, v & 0xFF); bin_u8(p, (v >> 8) & 0xFF);
}
static void bin_u32(unsigned char **p, uint32_t v)
{
    bin_u8(p, v & 0xFF);         bin_u8(p, (v >> 8) & 0xFF);
    bin_u8(p, (v >> 16) & 0xFF); bin_u8(p, (v >> 24) & 0xFF);
}
static void bin_f32(unsigned char **p, float f)
{
    union { float f; uint32_t u; } u;
    u.f = f;
    bin_u32(p, u.u);
}
static void bin_pad(unsigned char **p, size_t n) { while (n--) bin_u8(p, 0); }
static void bin_str(unsigned char **p, const char *s, size_t field)
{
    size_t i = 0;
    for (; s[i] && i < field; i++) bin_u8(p, (unsigned char)s[i]);
    bin_pad(p, field - i);
}
static void bin_bcd(unsigned char **p, int v)
{
    bin_u8(p, (unsigned)(((v / 10) << 4) | (v % 10)));
}

/*
 * A believable 25 m down cast: a warm mixed layer, a thermocline, then a
 * cold isothermal layer, sampled at 32 Hz on the way down and back up. The
 * up cast is included so the profile split in sv_profile has something real
 * to cut.
 */
static void sim_send_file(SvSim *s, const SimLogged *lg)
{
    static unsigned char file[600000];
    unsigned char *p = file;

    const char *logger = "SWIFT LOGGER SIM 1.0";
    for (const char *q = logger; *q; q++) bin_u8(&p, (unsigned char)*q);
    bin_u8(&p, '\r'); bin_u8(&p, '\n');

    unsigned char *hdr = p;
    bin_u16(&p, 0);                  /* header size, patched below */
    bin_u8(&p, 1);                   /* family: SWiFT */
    bin_u8(&p, 1);                   /* type: SV */
    bin_u8(&p, 32);                  /* tick rate */

    bin_bcd(&p, 20); bin_bcd(&p, 26);
    bin_bcd(&p, 8);  bin_bcd(&p, 11);
    bin_bcd(&p, lg->hh); bin_bcd(&p, lg->mm); bin_bcd(&p, lg->ss);

    /* Where this cast was recorded, which is not where the vessel is now. The
     * header carries a 32-bit float, so it is the finer of the app's two
     * position sources. */
    bin_f32(&p, (float)lg->lat);
    bin_f32(&p, (float)lg->lon);
    bin_f32(&p, 46236.0f);
    bin_str(&p, "0650735A9 Jun 29 2018 12:09", 35);
    bin_pad(&p, 3);
    bin_u8(&p, 32);                  /* sample rate */
    bin_u8(&p, 1);                   /* operating mode */
    bin_f32(&p, 10.204f);            /* pressure tare */
    bin_pad(&p, 8);                  /* SV cal */
    bin_pad(&p, 12);                 /* pressure cal */
    bin_pad(&p, 12);                 /* temperature cal */
    bin_f32(&p, 61.6f);
    bin_f32(&p, 3.71f);
    bin_u8(&p, 1);                   /* tare subtracted */
    bin_pad(&p, 36);                 /* user cal blocks */
    bin_str(&p, s->set.site, 100);
    bin_u8(&p, 0);                   /* optics: none */
    bin_pad(&p, 48);
    bin_u8(&p, 0x03);                /* ETX */

    size_t hdr_size = (size_t)(p - hdr);
    hdr[0] = (unsigned char)(hdr_size & 0xFF);
    hdr[1] = (unsigned char)((hdr_size >> 8) & 0xFF);

    /* Descend at 1.5 m/s, then recover at 1.0 m/s. */
    const double rate = 32.0;
    const double max_depth = lg->max_depth;
    uint32_t tick = 0;

    for (int phase = 0; phase < 2; phase++) {
        double speed = phase ? -1.0 : 1.5;
        double d = phase ? max_depth : 0.3;

        while ((phase == 0 && d < max_depth) || (phase == 1 && d > 0.4)) {
            double t = 15.5;
            if (d > 8.0 && d < 14.0)  t = 15.5 - (d - 8.0) * 1.15;    /* thermocline */
            else if (d >= 14.0)       t = 8.6;
            t += 0.01 * sin(d * 3.0);

            double sal = 34.9 + 0.02 * d / max_depth;
            double pressure = d * 1.007;

            /* Chen-Millero, inline: the emulator has to produce sound speed
             * the way the instrument does, not borrow the app's inverse. */
            double sv = 1449.2 + 4.6 * t - 0.055 * t * t + 0.00029 * t * t * t
                      + (1.34 - 0.010 * t) * (sal - 35.0) + 0.016 * d;

            bin_u32(&p, tick);
            bin_f32(&p, (float)sv);
            bin_f32(&p, (float)pressure);
            bin_f32(&p, (float)t);

            tick += 1;
            d += speed / rate;

            if ((size_t)(p - file) > sizeof file - 64)
                break;
        }
    }

    emit(s, file, (size_t)(p - file));
}

/* ------------------------------------------------------------------ */
/* Command handling                                                    */
/* ------------------------------------------------------------------ */

/*
 * Advance the simulated vessel.
 *
 * The metres-per-degree constants are written out here rather than taken from
 * sv_geo, for the same reason the sound speed above is inlined: the emulator
 * must not share arithmetic with the code under test, or a test can pass
 * because both sides made the same mistake.
 */
static void sim_move(SvSim *s, unsigned long now)
{
    if (s->last_move_ms == 0) {
        s->last_move_ms = now;
        return;
    }

    double dt = (double)(now - s->last_move_ms) / 1000.0;
    if (dt <= 0.0)
        return;
    s->last_move_ms = now;

    const double speed_ms = 4.0 * 1852.0 / 3600.0;         /* 4 knots */
    double dist = speed_ms * dt;

    s->course_deg += 0.35 * dt;                            /* a slow turn */
    if (s->course_deg >= 360.0)
        s->course_deg -= 360.0;

    double rad = s->course_deg * 3.14159265358979323846 / 180.0;
    double m_per_deg_lat = 111320.0;
    double m_per_deg_lon = 111320.0 * cos(s->lat * 3.14159265358979323846 / 180.0);

    s->lat += dist * cos(rad) / m_per_deg_lat;
    if (m_per_deg_lon > 1.0)
        s->lon += dist * sin(rad) / m_per_deg_lon;
}

static void status_broadcast(SvSim *s)
{
    char body[160];

    /* Four decimal places, which is what the integration guide's own example
     * sentence carries — about 11 m of latitude. The chart's live track can be
     * no finer than this, and pretending otherwise here would hide a real
     * limitation of the instrument's broadcast. */
    const SimLogged *last = &s->logged[SIM_LOGGED - 1];
    snprintf(body, sizeof body,
             "PVBB,00102532,46236,%.4f,%.4f,%.2f,260811%02d%02d%02d,%d,",
             s->lat, s->lon, s->battery_hours,
             last->hh, last->mm, last->ss, 1);

    emitf(s, "$%s*%02X\r\n", body, sv_nmea_checksum(body, strlen(body)));
}

static void reply_prompt(SvSim *s)
{
    emits(s, ">");
}

/* value part after the ';' of "#NNN;value", or "" */
static const char *arg_of(const char *line)
{
    const char *semi = strchr(line, ';');
    return semi ? semi + 1 : "";
}

static void handle_command(SvSim *s, const char *line)
{
    int code = 0;
    if (sscanf(line, "#%d", &code) != 1) {
        reply_prompt(s);
        return;
    }

    const char *arg = arg_of(line);
    bool has_arg = (strchr(line, ';') != NULL);

    /* The instrument echoes what it received. */
    emitf(s, "%s\r\n", line);

    switch (code) {
    case SV_CMD_SERIAL:    emits(s, "46236\r\n"); break;
    case SV_CMD_FIRMWARE:  emits(s, "0650735A9 Jun 29 2018 12:09\r\n"); break;
    case SV_CMD_TARE:      emits(s, "10.204000\r\n"); break;
    case SV_CMD_BATT_PCT:  emits(s, "0.62\r\n"); break;
    case SV_CMD_BATT_V:    emits(s, "3.71\r\n"); break;
    case SV_CMD_BATT_HOURS:
        emitf(s, "%.2f\r\n", s->battery_hours);
        break;

    case SV_CMD_RUN:
        s->interrupted = false;
        emits(s, ">");
        return;                      /* no second prompt */

    case SV_CMD_GET_MODE:    emitf(s, "%d\r\n", s->set.mode); break;
    case SV_CMD_SET_MODE:    if (has_arg) s->set.mode = atoi(arg); break;
    case SV_CMD_GET_DIR:     emitf(s, "%d\r\n", s->set.direction); break;
    case SV_CMD_SET_DIR:     if (has_arg) s->set.direction = atoi(arg); break;
    case SV_CMD_GET_TRIGGER: emitf(s, "%.2f\r\n", s->set.trigger_depth); break;
    case SV_CMD_SET_TRIGGER: if (has_arg) s->set.trigger_depth = atof(arg); break;
    case SV_CMD_GET_INCR:    emitf(s, "%.2f\r\n", s->set.depth_increment); break;
    case SV_CMD_SET_INCR:    if (has_arg) s->set.depth_increment = atof(arg); break;
    case SV_CMD_GET_STEP:    emitf(s, "%.2f\r\n", s->set.trigger_step); break;
    case SV_CMD_SET_STEP:    if (has_arg) s->set.trigger_step = atof(arg); break;
    case SV_CMD_GET_FIXREQ:  emitf(s, "%d\r\n", s->set.require_fix); break;
    case SV_CMD_SET_FIXREQ:  if (has_arg) s->set.require_fix = atoi(arg); break;
    case SV_CMD_GET_POWER:   emitf(s, "%d\r\n", s->set.auto_power); break;
    case SV_CMD_SET_POWER:   if (has_arg) s->set.auto_power = atoi(arg); break;
    case SV_CMD_GET_BTSLEEP: emitf(s, "%d\r\n", s->set.bt_sleep); break;
    case SV_CMD_SET_BTSLEEP: if (has_arg) s->set.bt_sleep = atoi(arg); break;
    case SV_CMD_GET_SITE:    emitf(s, "%s\r\n", s->set.site); break;
    case SV_CMD_SET_SITE:
        if (has_arg)
            snprintf(s->set.site, sizeof s->set.site, "%s", arg);
        break;

    case SV_CMD_GET_TIME:    emits(s, "11;08;20;26;10;54;36\r\n"); break;
    case SV_CMD_SET_TIME:    break;
    case SV_CMD_FREE:
        emits(s, "type 2 memory more than 9043968 bytes of 1914 Mbytes free\r\n");
        break;

    case SV_CMD_DIR_LIST:
        emits(s, "DIR:\\202608\\11\r\n");
        emits(s, "2026/08/11\t09:58:12\t<DIR>\r\n");
        for (int i = 0; i < SIM_LOGGED; i++)
            emitf(s, "2026/08/11\t%02d:%02d:%02d\t%d\t%s\r\n",
                  s->logged[i].hh, s->logged[i].mm, s->logged[i].ss,
                  4096, s->logged[i].name);
        emits(s, "2026/08/11\t11:45:10\t872\taction.log\r\n");
        break;

    case SV_CMD_CHDIR:
        break;

    case SV_CMD_EXTRACT: {
        /* Serve the cast that was asked for, by name. Anything unrecognised
         * gets the newest, which is what the automatic path asks for. */
        const SimLogged *lg = &s->logged[SIM_LOGGED - 1];
        for (int i = 0; i < SIM_LOGGED; i++)
            if (strstr(arg, s->logged[i].name)) {
                lg = &s->logged[i];
                break;
            }
        sim_send_file(s, lg);
        s->cast_counter++;
        break;
    }

    default:
        break;
    }

    reply_prompt(s);
}

/* ------------------------------------------------------------------ */
/* Public interface                                                    */
/* ------------------------------------------------------------------ */

SvSim *sv_sim_create(void)
{
    SvSim *s = calloc(1, sizeof *s);
    if (!s)
        return NULL;

    s->set.mode = 1;
    s->set.trigger_depth = 0.5;
    s->set.depth_increment = 0.1;
    s->set.trigger_step = 2.0;
    s->set.require_fix = 1;
    s->set.auto_power = 120;
    snprintf(s->set.site, sizeof s->set.site, "Simulated site");
    s->battery_hours = 61.6;
    s->lat = 50.4264;                  /* Torbay, as in the guide's example */
    s->lon = -3.6814;
    s->course_deg = 65.0;

    /* Four casts already on the card, laid out along the line the vessel has
     * just run: newest at the start position, the rest trailing astern at
     * SIM_SPACING_M. Times and depths differ so the app has something to tell
     * apart, as a real card would. */
    static const struct { int hh, mm, ss; double depth; } made[SIM_LOGGED] = {
        {  9, 58, 12, 22.4 },
        { 10, 21, 44, 25.9 },
        { 10, 54, 36, 24.0 },
        { 11, 31,  2, 23.1 }
    };

    double back = s->course_deg * 3.14159265358979323846 / 180.0;
    for (int i = 0; i < SIM_LOGGED; i++) {
        double astern = (double)(SIM_LOGGED - 1 - i) * SIM_SPACING_M;
        s->logged[i].hh = made[i].hh;
        s->logged[i].mm = made[i].mm;
        s->logged[i].ss = made[i].ss;
        s->logged[i].max_depth = made[i].depth;
        s->logged[i].lat = s->lat - astern * cos(back) / 111320.0;
        s->logged[i].lon = s->lon - astern * sin(back) /
                           (111320.0 * cos(s->lat * 3.14159265358979323846 / 180.0));
        snprintf(s->logged[i].name, sizeof s->logged[i].name,
                 "VL_46236_260811%02d%02d%02d.bin",
                 made[i].hh, made[i].mm, made[i].ss);
    }
    return s;
}

void sv_sim_destroy(SvSim *s)
{
    free(s);
}

int sv_sim_write(SvSim *s, const void *buf, size_t len)
{
    const char *p = buf;

    for (size_t i = 0; i < len; i++) {
        char ch = p[i];

        /* '#' interrupts from run mode, wherever it arrives. */
        if (ch == '#' && !s->interrupted) {
            s->interrupted = true;
            s->line_n = 0;
        }

        if (ch == '\r' || ch == '\n') {
            if (s->line_n > 0) {
                s->line[s->line_n] = '\0';
                if (s->interrupted)
                    handle_command(s, s->line);
                s->line_n = 0;
            }
            continue;
        }

        if (s->line_n + 1 < LINE_CAP)
            s->line[s->line_n++] = ch;
    }
    return (int)len;
}

int sv_sim_read(SvSim *s, void *buf, size_t cap)
{
    unsigned char *out = buf;
    size_t n = 0;

    while (n < cap && s->out_tail != s->out_head) {
        out[n++] = s->out[s->out_tail];
        s->out_tail = (s->out_tail + 1) % OUT_CAP;
    }
    return (int)n;
}

void sv_sim_tick(SvSim *s, unsigned long now)
{
    if (s->boot_ms == 0)
        s->boot_ms = now;

    /* The vessel keeps moving while the instrument is at the command prompt —
     * that is exactly when the operator is downloading the last cast. */
    sim_move(s, now);

    if (s->interrupted)
        return;                      /* silent at the command prompt */

    if (s->set.mode == 1) {
        /* Smart profile: status broadcast every 10 s, as the real one does. */
        if (now - s->last_status_ms >= 10000) {
            s->last_status_ms = now;
            status_broadcast(s);
            if (s->battery_hours > 1.0)
                s->battery_hours -= 0.01;
        }
    } else {
        /* Continuous: 1 Hz observations. */
        if (now - s->last_data_ms >= 1000) {
            s->last_data_ms = now;

            double t = 15.2 + 0.3 * sin((double)now / 9000.0);
            double p = 0.4 + 0.15 * sin((double)now / 3000.0);
            double sv = 1449.2 + 4.6 * t - 0.055 * t * t
                      + 0.00029 * t * t * t - 0.13;

            emitf(s, "$PVSV1,20260811,105436,%.3f,m/s,%.3f,dBar,%.3f,DegC,"
                     "3.71,V,%lu\r\n",
                  sv, p, t, (now - s->boot_ms) / 1000 * 32);
        }
    }
}
