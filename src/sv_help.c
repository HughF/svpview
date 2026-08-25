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
 * sv_help.c — the manual
 *
 * Written for someone standing on a deck with the instrument in one hand, so
 * it says what to do rather than how the program is built. The tooltips give
 * the one-line version of the same thing; this is where the reasons live.
 */
#include "sv_help.h"

#include <stdbool.h>

#define T(s)      { SV_HELP_TEXT,   (s),  NULL }
#define SUB(s)    { SV_HELP_SUB,    (s),  NULL }
#define B(s)      { SV_HELP_BULLET, (s),  NULL }
#define R(k, v)   { SV_HELP_ROW,    (k),  (v)  }
#define N(s)      { SV_HELP_NOTE,   (s),  NULL }

/* ------------------------------------------------------------------ */

static const SvHelpItem it_about[] = {
    T("The program does three jobs: it configures a Valeport SWiFT SVP, CTD "
      "or SWiFTplus, it downloads and plots the casts the instrument has "
      "logged, and it reports each cast's achieved depth to a C-MAX Vigo "
      "winch. It replaces Valeport Ocean and VigoDepthRelay."),
    N("It does not control the winch. The winch is commanded from its own "
      "web interface and its physical panel, exactly as before; this program "
      "only tells it how deep the last cast reached."),
    T("Everything works offline. There is no basemap imagery, no tile server "
      "and no internet dependency anywhere, because a survey vessel usually "
      "has none of them."),
};

static const SvHelpItem it_connect[] = {
    T("Both the USB comms cable and the Valeport Bluetooth key appear to the "
      "computer as serial ports, at 230400 baud, 8N1. Open Settings and press "
      "Connect... to see the ports the machine can offer; if the one you want "
      "is not listed, plug it in and press Rescan."),
    T("On Windows the ports are COM numbers, listed with the name Windows "
      "knows them by — the Bluetooth key reads as \"Standard Serial over "
      "Bluetooth link\" and the cable as a USB serial port. That difference "
      "matters: the instrument switches its GPS off while the comms cable is "
      "connected, so a cast set up over the cable will never be ready to "
      "deploy. On Linux the same ports are /dev/ttyUSB and /dev/rfcomm names, "
      "and the account must be in the dialout group to open one."),
    SUB("What the link state means"),
    R("Not connected", "No port is open. Nothing can be read or written."),
    R("Identifying",   "The port is open and the instrument is being asked "
                       "what it is."),
    R("Run mode",      "The normal state. The instrument is logging and "
                       "broadcasting status, and the program is listening."),
    R("Interrupted",   "The instrument is sitting at its command prompt. It "
                       "logs nothing and broadcasts nothing in this state — "
                       "which is why the position on the Chart page goes "
                       "stale during a download."),
    R("Busy",          "A transfer or a settings write is in progress. The "
                       "controls that would disturb it are disabled until it "
                       "finishes."),
    T("Interrupt puts the instrument at its command prompt so its card and "
      "settings can be read; Run mode sends it back to logging. Always leave "
      "it in run mode before deploying."),
};

static const SvHelpItem it_status[] = {
    T("The strip across the top is the same on every page. Read it left to "
      "right before every cast."),
    R("SWiFT nnnnn",     "The serial number of the instrument that "
                     "answered on the port."),
    R("Link state",      "See above."),
    R("Battery",         "Hours remaining, from the instrument's own "
                         "estimate. It turns red below 12 hours."),
    R("Position",        "Latitude and longitude from the status broadcast, "
                         "or \"no fix\"."),
    R("Deploy flag",     "The instrument's own opinion of whether it is "
                         "ready. See the next section."),
    R("Winch",           "Whether the Vigo winch answered the last probe. "
                         "Unknown until you press Probe winch."),
};

static const SvHelpItem it_deploy[] = {
    T("The instrument broadcasts a status sentence every 10 seconds while it "
      "is in smart profile mode at the surface. \"NOT ready to deploy\" has "
      "four causes, and the page you are on will name the one that applies."),
    R("No broadcast at all",
      "Nothing has been heard yet. Check the instrument is in run mode and "
      "at the surface."),
    R("Comms cable plugged in",
      "The GPS is switched off while the USB cable is connected. Unplug it "
      "and use the Bluetooth key."),
    R("No GPS fix",
      "Move to open sky and wait. A hot fix takes under 30 seconds, a cold "
      "one a few minutes."),
    R("Pressure or clock",
      "Pressure reads above the trigger point, or the GPS clock is stale. "
      "Power cycle the instrument with a valid fix, so it creates a new file "
      "and takes a fresh tare."),
};

static const SvHelpItem it_live[] = {
    T("Live shows what the instrument is measuring right now, plotted as it "
      "arrives, with its identity and the last file it recorded underneath."),
    R("Checksum failures",
      "A running count of sentences that arrived corrupt. A handful over a "
      "long session is normal on Bluetooth; a steadily climbing count means "
      "a bad cable, a dying key battery or too much distance."),
};

static const SvHelpItem it_profile[] = {
    T("Profile plots the casts held in memory — downloaded from the "
      "instrument, or opened from a file. Casts are listed on the right, "
      "newest at the bottom; click one to select it."),
    R("Velocity, Temp, Salinity, Density",
      "Which traces are drawn. They share the plot and each carries its own "
      "axis."),
    R("Overlay all",
      "Draw every cast in memory at once, with the selected one picked out. "
      "This is how you see whether the water column has changed through the "
      "day."),
    R("Process...", "Trim, despike, bin and thin the selected cast."),
    R("Export...",  "Write the selected cast out in a survey format."),
    T("The line above the plot gives the selected cast's maximum depth, its "
      "mean sound velocity and the range of velocity and temperature it saw. "
      "The mean velocity is the number a single-value echo sounder wants."),
};

static const SvHelpItem it_chart[] = {
    T("Chart is a plan view of where the casts were taken, the track the "
      "instrument has broadcast, and where it is now. There is deliberately "
      "no basemap: a graticule, a scale bar and a north arrow work at sea "
      "with no data connection."),
    R("Fit",              "Set the view to contain everything there is."),
    R("+ and -",          "Zoom about the middle of the plot."),
    R("Labels",           "Draw the time against each cast marker."),
    R("Follow position",  "Keep the live position centred as the vessel "
                          "moves. Selecting a cast from the list turns it "
                          "off, so the view stays where you put it."),
    SUB("With the mouse"),
    R("Drag",        "Pan."),
    R("Wheel",       "Zoom about the pointer."),
    R("Click a marker", "Select that cast."),
    SUB("What the panel is telling you"),
    T("The distance and bearing are from the current position to the "
      "selected cast — the two numbers that answer \"do I need to dip "
      "again here?\"."),
    T("A hollow position marker means the fix has stopped being refreshed: "
      "the instrument broadcasts nothing at its command prompt, so the "
      "position freezes during a download and the age is shown beside it."),
    N("A cast logged without a GPS fix has no position and cannot be "
      "plotted. The panel says how many are in that state rather than "
      "quietly showing fewer casts than the list holds."),
    T("Positions come from two sources of different precision, which the "
      "page states rather than blends: each cast carries the fix written "
      "into its own file header, while the live marker and the track come "
      "from the status broadcast, which is only given to four decimal "
      "places — about 11 m."),
};

static const SvHelpItem it_files[] = {
    T("Files lists the instrument's memory card. Interrupt is sent for you "
      "if the instrument is logging, so listing a directory during a survey "
      "briefly stops the instrument recording."),
    R("Root",     "Go to the top of the card."),
    R("Refresh",  "List the current directory again."),
    R("Open",     "Enter a directory."),
    R("Download", "Fetch a .bin file and add it to the casts in memory. "
                  "Progress is reported along the bottom of the window."),
    T("A downloaded cast is held in memory only. Export it if you want it "
      "kept."),
};

static const SvHelpItem it_settings[] = {
    T("Settings holds the connection, the instrument's own configuration, "
      "and the winch reporting."),
    R("Read",     "Read the configuration out of the instrument. Nothing is "
                  "shown until you do."),
    R("Edit...",  "Change it. Nothing is written until you press Apply or "
                  "OK."),
    R("Probe winch",
      "Send one probe and wait for the reply, to prove the winch can hear "
      "this machine before a cast depends on it."),
    R("Report selected cast depth",
      "Send the selected cast's achieved depth now. Normally this is not "
      "needed — it goes automatically when a cast is downloaded — but it is "
      "the way to repeat a report the winch missed."),
    R("Select adapter...",
      "Choose which network adapter the report goes out of."),
};

static const SvHelpItem it_instrument[] = {
    T("These are the instrument's own settings, written into it and kept "
      "when it is powered down. Read them first, so that what is on the "
      "screen is what is in the instrument."),
    R("Operating mode",
      "Smart profile logs a file per descent and is what a winch survey "
      "wants. Continuous logs everything from power-up."),
    R("Profile direction",
      "Which half of the dip is kept. Down cast is the normal choice: the "
      "sensor leads the disturbed water on the way down."),
    R("Trigger depth",
      "How deep the instrument must go before it starts a file."),
    R("Depth increment",
      "The depth change between logged samples."),
    R("Trigger step",
      "How much the pressure must change to keep the file open."),
    N("The trigger step must exceed the local sea and swell. Set it below "
      "the wave height and the instrument decides the descent has stopped "
      "while it is still going down, and the file closes early."),
    R("Require GPS fix",
      "In continuous mode, wait for a fix before logging, so every file "
      "carries a position."),
    R("Bluetooth sleep",
      "Allow the instrument to be woken over Bluetooth."),
    R("Auto power down",
      "Idle minutes before the instrument switches itself off. 9999 "
      "disables it."),
    R("Site information",
      "Free text stored in the instrument and written into the header of "
      "every file it logs. Put the survey or the vessel here."),
};

static const SvHelpItem it_process[] = {
    T("Processing is applied to the selected cast in memory, in place. The "
      "file on the card is untouched — download or open it again to get the "
      "raw profile back."),
    R("Trim",       "Keep the down cast only, discarding the ascent."),
    R("Despike",    "Remove samples more than 3 m/s off the local median."),
    R("Depth bin",  "Average samples into bins of this many metres."),
    R("Thin to",    "Reduce to about this many points, keeping the shape."),
    T("Zero disables a step. Bin and thin exist because some acquisition "
      "systems cap the number of points they will accept in a profile."),
};

static const SvHelpItem it_export[] = {
    T("Export writes the selected cast, as it currently stands, to a file. "
      "The name is proposed from the cast's own timestamp and changes with "
      "the format; edit it freely."),
    R("CSV",             "Every column, for a spreadsheet."),
    R("Valeport .vp2",   "Valeport's own format."),
    R("Kongsberg .asvp", "For Kongsberg acquisition — SIS and friends."),
    R("Caris .svp",      "For Caris HIPS."),
    R("Hypack .vel",     "For Hypack."),
};

static const SvHelpItem it_winch[] = {
    T("When a cast is downloaded, its achieved depth is broadcast to the "
      "Vigo winch on UDP port 8090. Vigo records it against the dive so the "
      "winch can repeat that depth, and answers with an acknowledgement, "
      "which is reported along the bottom of the window."),
    SUB("Choosing the adapter"),
    T("A survey computer usually has more than one network adapter, and "
      "broadcasting to everything leaves the choice of wire to the routing "
      "table — which is how a depth report leaves by the office adapter and "
      "the winch never hears it. Pick the adapter on the survey network "
      "under Settings, Select adapter.... Only connected adapters are "
      "listed."),
    SUB("If the winch does not acknowledge"),
    B("Probe the winch first. No reply means the network, not the report."),
    B("Vigo ignores a depth report if no cast has been performed in its own "
      "session, if the cast was aborted, or if the depth is outside a tenth "
      "to ten times the requested cast depth."),
    B("Vigo ignores a report identical to the one before it, which is why "
      "each carries its own incrementing message number."),
};

static const SvHelpItem it_keys[] = {
    R("Hover",       "Rest the pointer on any control for a one-line "
                     "description of what it does."),
    R("F1",          "Open this page."),
    R("Escape",      "Cancel the dialog that is open."),
    R("Mouse wheel", "Zoom the chart, or scroll a list."),
    R("Drag",        "Pan the chart."),
    SUB("In every dialog"),
    R("Cancel", "Discard the changes and close."),
    R("Apply",  "Commit the changes and stay open."),
    R("OK",     "Commit the changes and close."),
    T("The close button in a dialog's title bar means Cancel, the same as "
      "Escape."),
};

static const SvHelpItem it_trouble[] = {
    R("No serial ports listed",
      "Plug in the cable or the Bluetooth key and press Rescan. On Linux the "
      "user must be in the dialout group to open a serial port."),
    R("Connected, but no status",
      "The instrument is at its command prompt. Press Run mode."),
    R("Checksum failures climbing",
      "A bad cable, a flat Bluetooth key battery, or too much distance."),
    R("Casts missing from the chart",
      "They were logged without a GPS fix. The chart panel counts them."),
    R("Winch silent",
      "Wrong adapter, or the winch is not on this network. Probe it, then "
      "check the adapter."),
    R("Winch silent on Windows only",
      "Windows Firewall. The first broadcast raises a prompt, and a prompt "
      "answered with Cancel — or never shown, on a machine with the firewall "
      "locked down — blocks every depth report from then on, silently. Allow "
      "svpview on the private network, or add an outbound rule for UDP 8090. "
      "This looks exactly like a winch that is not listening."),
    R("Text too small or too large",
      "The interface scales itself to the display. Override it with the "
      "SVPVIEW_SCALE environment variable."),
    T("The Log page holds the conversation with the instrument, sent lines "
      "marked > and received lines <. It is the first thing to look at when "
      "something behaves oddly, and the first thing to quote in a fault "
      "report."),
};

static const SvHelpItem it_cli[] = {
    R("--sim",         "Run against a simulated instrument that speaks the "
                       "real protocol. Everything in the program works, with "
                       "no hardware attached."),
    R("--open FILE",   "Load a .bin logged file at startup."),
    R("--help",        "Command line summary."),
    R("--help-doc",    "Write this manual to standard output as Markdown."),
    R("SVPVIEW_SCALE", "Override the interface scale, e.g. 2 on a HiDPI "
                       "display."),
};

/* ------------------------------------------------------------------ */

#define SEC(t, intro, arr) { (t), (intro), (arr), (int)(sizeof (arr) / sizeof *(arr)) }

static const SvHelpSection SECTIONS[] = {
    SEC("What this program does", NULL, it_about),
    SEC("Connecting to the instrument", NULL, it_connect),
    SEC("The status strip", NULL, it_status),
    SEC("Ready to deploy", NULL, it_deploy),
    SEC("Live", NULL, it_live),
    SEC("Profile", NULL, it_profile),
    SEC("Chart", NULL, it_chart),
    SEC("Files", NULL, it_files),
    SEC("Settings", NULL, it_settings),
    SEC("Instrument settings", NULL, it_instrument),
    SEC("Processing a cast", NULL, it_process),
    SEC("Exporting", NULL, it_export),
    SEC("Reporting depth to the winch", NULL, it_winch),
    SEC("Keyboard and mouse", NULL, it_keys),
    SEC("When something is wrong", NULL, it_trouble),
    SEC("Command line and environment", NULL, it_cli),
};

const SvHelpSection *sv_help_sections(int *n_sections)
{
    if (n_sections)
        *n_sections = (int)(sizeof SECTIONS / sizeof *SECTIONS);
    return SECTIONS;
}

void sv_help_write_markdown(FILE *f, const char *version)
{
    int n = 0;
    const SvHelpSection *s = sv_help_sections(&n);

    fprintf(f, "# svpview — operator's manual\n\n");
    fprintf(f, "Valeport SWiFT profiler acquisition and display, version %s.\n",
            version ? version : "");
    fprintf(f,
        "\nThis file is generated from the help built into the program:\n"
        "`svpview --help-doc > docs/HELP.md`, or `make help-doc`. Edit\n"
        "`src/sv_help.c` and regenerate — do not edit this file.\n\n");

    fprintf(f, "## Contents\n\n");
    for (int i = 0; i < n; i++)
        fprintf(f, "%d. %s\n", i + 1, s[i].title);
    fprintf(f, "\n");

    for (int i = 0; i < n; i++) {
        fprintf(f, "---\n\n## %s\n\n", s[i].title);
        if (s[i].intro)
            fprintf(f, "%s\n\n", s[i].intro);

        /* A run of rows becomes one table, so the Markdown reads like a
         * reference rather than a list of orphaned bold words. */
        bool in_table = false;
        for (int k = 0; k < s[i].n_items; k++) {
            const SvHelpItem *e = &s[i].items[k];
            if (e->kind != SV_HELP_ROW && in_table) {
                fprintf(f, "\n");
                in_table = false;
            }
            switch (e->kind) {
            case SV_HELP_TEXT:
                fprintf(f, "%s\n\n", e->a);
                break;
            case SV_HELP_SUB:
                fprintf(f, "### %s\n\n", e->a);
                break;
            case SV_HELP_BULLET:
                fprintf(f, "- %s\n", e->a);
                if (k + 1 >= s[i].n_items ||
                    s[i].items[k + 1].kind != SV_HELP_BULLET)
                    fprintf(f, "\n");
                break;
            case SV_HELP_ROW:
                if (!in_table) {
                    fprintf(f, "| | |\n|---|---|\n");
                    in_table = true;
                }
                fprintf(f, "| **%s** | %s |\n", e->a, e->b ? e->b : "");
                break;
            case SV_HELP_NOTE:
                fprintf(f, "> **Note.** %s\n\n", e->a);
                break;
            }
        }
        if (in_table)
            fprintf(f, "\n");
    }
}
