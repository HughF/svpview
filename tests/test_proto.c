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
 * test_proto.c — SWiFT serial protocol codec.
 *
 * Vectors are taken verbatim from the SWiFT Integration Guide
 * (MANUAL-68251662-19 issue 2.1) sections 6, 7 and 8.
 */
#include "sv_test.h"
#include "../src/sv_proto.h"

#define S(x) x, strlen(x)

TEST_MAIN("sv_proto",
{
    SvMsg m;
    char buf[256];

    /* ---- $PVBB status, guide section 6 ---------------------------- */
    CHECK(sv_parse_sentence(
        S("$PVBB,00102532,56150,50.4264,-3.6814,66.00,210118154647,0,*42"), &m));
    CHECK(m.kind == SV_MSG_STATUS);
    CHECK_STR(m.status.hw_id, "00102532");
    CHECK_STR(m.status.serial, "56150");
    CHECK_NEAR(m.status.lat, 50.4264, 1e-9);
    CHECK_NEAR(m.status.lon, -3.6814, 1e-9);
    CHECK(m.status.has_fix);
    CHECK_NEAR(m.status.battery_hours, 66.00, 1e-9);
    CHECK_STR(m.status.last_file, "210118154647");
    CHECK(!m.status.ready_to_deploy);
    /* The guide's transcription is lossy, so this is expected to be flagged
     * rather than to verify. See the note on SvMsg.checksum_ok. */
    CHECK(m.checksum_present);

    /* Ready flag set. */
    CHECK(sv_parse_sentence(
        S("$PVBB,00102532,56150,50.4264,-3.6814,66.00,210118154647,1,*42"), &m));
    CHECK(m.status.ready_to_deploy);

    /* No fix is signalled as 999. */
    CHECK(sv_parse_sentence(
        S("$PVBB,00102532,56150,999,999,66.00,210118154647,0,*42"), &m));
    CHECK(!m.status.has_fix);

    /* The guide's section 8 example omits the deploy field entirely; it must
     * still parse rather than being thrown away. */
    CHECK(sv_parse_sentence(
        S("$PVBB,00102532,46103,50.4263,-3.6813,79.46,160823112615,*37"), &m));
    CHECK(m.kind == SV_MSG_STATUS);
    CHECK(!m.status.ready_to_deploy);

    /* Checksum algorithm itself: XOR between '$' and '*'. */
    CHECK(sv_nmea_checksum(S("$PVBB,00102532,56150,50.4264,-3.6814,66.00,"
                             "210118154647,0,*42")) == 0x32);

    /* ---- $PVSV1, no optics ---------------------------------------- */
    CHECK(sv_parse_sentence(
        S("$PVSV1,20210728,105436,1506.617,m/s,25.716,dBar,14.868,DegC,"
          "3.71,V,12345"), &m));
    CHECK(m.kind == SV_MSG_DATA);
    CHECK(m.live.year == 2021 && m.live.month == 7 && m.live.day == 28);
    CHECK(m.live.hour == 10 && m.live.minute == 54 && m.live.second == 36);
    CHECK_NEAR(m.live.primary, 1506.617, 1e-9);
    CHECK(!m.live.primary_is_cond);
    CHECK_NEAR(m.live.pressure, 25.716, 1e-9);
    CHECK_NEAR(m.live.temp, 14.868, 1e-9);
    CHECK_NEAR(m.live.volts, 3.71, 1e-9);
    CHECK(m.live.log_index == 12345);
    CHECK(!m.live.has_optics);

    /* ---- $PVSV2, with optics -------------------------------------- */
    CHECK(sv_parse_sentence(
        S("$PVSV2,20210728,105436,1506.617,m/s,25.716,dBar,14.868,DegC,"
          "36.329,ppm,26.374,ppm,3.71,V,999"), &m));
    CHECK(m.live.has_optics);
    CHECK_NEAR(m.live.optics1, 36.329, 1e-9);
    CHECK_NEAR(m.live.optics2, 26.374, 1e-9);
    CHECK_NEAR(m.live.volts, 3.71, 1e-9);
    CHECK(m.live.log_index == 999);

    /* ---- $PVCT2, conductivity in the primary slot ----------------- */
    CHECK(sv_parse_sentence(
        S("$PVCT2,20210728,105436,42.718,mS,25.716,dBar,14.868,DegC,"
          "36.329,ppm,26.374,ppm,3.71,V,999"), &m));
    CHECK(m.live.primary_is_cond);
    CHECK_NEAR(m.live.primary, 42.718, 1e-9);

    /* ---- Rejections ------------------------------------------------ */
    CHECK(!sv_parse_sentence(S("$GPGGA,123519,4807.038,N"), &m));
    CHECK(!sv_parse_sentence(S("rubbish"), &m));
    CHECK(!sv_parse_sentence(S("$PVBB,tooshort"), &m));
    CHECK(!sv_parse_sentence(S(""), &m));
    CHECK(!sv_parse_sentence(S("$"), &m));
    CHECK(!sv_parse_sentence(S("$PVSV1,notadate,105436,1,m/s,2,dBar,3,DegC,"
                               "4,V,5"), &m));
    /* Truncated optics sentence must not be read as a plain one. */
    CHECK(!sv_parse_sentence(
        S("$PVSV2,20210728,105436,1506.6,m/s,25.7,dBar,14.8,DegC,36.3,ppm"), &m));

    /* Trailing CRLF is normal on the wire. */
    CHECK(sv_parse_sentence(
        S("$PVBB,00102532,56150,50.4264,-3.6814,66.00,210118154647,0,*42\r\n"),
        &m));

    /* ---- Command construction -------------------------------------- */
    CHECK(sv_cmd_read(buf, sizeof buf, SV_CMD_SERIAL) == 6);
    CHECK_STR(buf, "#003\r\n");
    sv_cmd_read(buf, sizeof buf, SV_CMD_RUN);
    CHECK_STR(buf, "#028\r\n");
    sv_cmd_write_int(buf, sizeof buf, SV_CMD_SET_POWER, 9999);
    CHECK_STR(buf, "#015;9999\r\n");
    sv_cmd_write_f(buf, sizeof buf, SV_CMD_SET_TRIGGER, 0.5, 2);
    CHECK_STR(buf, "#024;0.50\r\n");
    sv_cmd_write_str(buf, sizeof buf, SV_CMD_EXTRACT, "VL_46103_160823112615.bin");
    CHECK_STR(buf, "#402;VL_46103_160823112615.bin\r\n");
    /* Undersized buffers report failure rather than truncating. */
    CHECK(sv_cmd_read(buf, 3, SV_CMD_SERIAL) == 0);
    CHECK(sv_cmd_write_str(buf, 8, SV_CMD_EXTRACT, "much too long") == 0);

    /* ---- Response stripping ---------------------------------------- */
    char out[128];
    CHECK(sv_strip_response(S("#003\r\n46103\r\n>"), "#003\r\n", out, sizeof out));
    CHECK_STR(out, "46103");
    CHECK(sv_strip_response(S("#016\r\n120\r\n>"), "#016\r\n", out, sizeof out));
    CHECK_STR(out, "120");
    /* Incomplete until the prompt arrives. */
    CHECK(!sv_strip_response(S("#003\r\n46103\r\n"), "#003\r\n", out, sizeof out));
    /* No echo present is tolerated. */
    CHECK(sv_strip_response(S("46103\r\n>"), "#003\r\n", out, sizeof out));
    CHECK_STR(out, "46103");

    /*
     * The prompt is a '>' at the start of a line. A directory listing is
     * full of "<DIR>" entries and their '>' must not end the response —
     * doing so truncates the listing at the first subdirectory.
     */
    CHECK(sv_strip_response(
        S("#400\r\nDIR:\\202006\r\n"
          "2020/06/01\t09:43:30\t<DIR>\t201606\r\n"
          "2020/06/20\t07:35:26\t911\tVL_46103_160620073527.bin\r\n>"),
        "#400\r\n", out, sizeof out));
    CHECK(strstr(out, "VL_46103_160620073527.bin") != NULL);
    CHECK(strstr(out, "201606") != NULL);

    /* A listing that has not yet reached its prompt is incomplete, even
     * though it contains "<DIR>". */
    CHECK(!sv_strip_response(
        S("#400\r\n2020/06/01\t09:43:30\t<DIR>\t201606\r\n"),
        "#400\r\n", out, sizeof out));

    /* ---- Directory listing, guide section 7 ------------------------ */
    SvDirEntry e;
    CHECK(sv_parse_dir_line("2020/06/01\t09:43:30\t<DIR>\t201606", &e));
    CHECK(e.is_dir);
    CHECK_STR(e.name, "201606");
    CHECK(e.year == 2020 && e.month == 6 && e.day == 1);
    CHECK(e.hour == 9 && e.minute == 43 && e.second == 30);

    CHECK(sv_parse_dir_line(
        "2020/06/20   07:35:26        911   VL_46103_160620073527.bin", &e));
    CHECK(!e.is_dir);
    CHECK(e.size == 911);
    CHECK_STR(e.name, "VL_46103_160620073527.bin");

    CHECK(sv_parse_dir_line("2020/06/20  13:55:46   872  action.log", &e));
    CHECK_STR(e.name, "action.log");

    CHECK(!sv_parse_dir_line("DIR:\\202006", &e));
    CHECK(!sv_parse_dir_line(">", &e));
    CHECK(!sv_parse_dir_line("", &e));
    /* A <DIR> line naming nothing is the directory's own entry. */
    CHECK(!sv_parse_dir_line("2020/06/20   07:35:26   <DIR>", &e));

    /* ---- Filename reconstruction, guide section 8 ------------------ */
    SvStatus st;
    memset(&st, 0, sizeof st);
    snprintf(st.serial, sizeof st.serial, "46103");
    snprintf(st.last_file, sizeof st.last_file, "160823112615");

    char dir[64], file[64];
    CHECK(sv_last_file_path(&st, dir, sizeof dir, file, sizeof file));
    CHECK_STR(dir, "\\201608\\23");
    CHECK_STR(file, "VL_46103_160823112615.bin");

    snprintf(st.last_file, sizeof st.last_file, "nonsense");
    CHECK(!sv_last_file_path(&st, dir, sizeof dir, file, sizeof file));
})
