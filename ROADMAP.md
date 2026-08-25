# svpview — implementation roadmap

> **Status 2026-08-12:** phases 0, 1, 4, 5 and 6 are implemented and the
> program runs; phase 2 and 3 are implemented against the simulator and
> await a real instrument. Phase 4 has since grown a Chart page (cast
> positions in plan) that was not in the original plan. See CHANGELOG.md for
> what actually landed, and docs/svpview_Design_and_Features.html for the
> illustrated tour.

Nine phases. Each ends with something runnable and tested; no phase depends on
hardware that is not already on the bench, except where marked **HW**.

---

## Phase 0 — Skeleton (0.5 day)

Build system, platform layer stubs, window that opens and closes cleanly.

- `Makefile` with `uname` platform detect, release/debug/asan targets
- `plat_time.c`, `plat_path.c`
- SDL2 + vendored `nuklear.h`, HiDPI logical-size setup (SDL2_ttf was
  planned and proved unnecessary — Nuklear bakes its own atlas)
- `make test` harness with one trivial test
- CI: build + test on Linux, mingw-w64, macOS

**Done when:** window opens on Linux and on a mingw-w64 cross-build, ASan clean.

---

## Phase 1 — Protocol core, offline (2 days)

No I/O at all. Pure parsers, driven by test vectors from the integration guide.

- `sv_proto.c`: `#NNN` codec; `$PVBB` with checksum; `$PVSVP` / `$PVSV1` /
  `$PVSV2` / `$PVCT2`
- `sv_binfile.c`: all three header variants, SV and CTD, sample decode
- `sv_vpd.c`: `.vpd` and `.vp2` read/write
- `sv_ocean.c`: UNESCO 83 depth, PSS-78 salinity, EOS-80 density, Chen-Millero SV
- `sv_vigo.c`: `Q` / `V` / `F` message build and reply match — no socket
- `tests/test_proto.c`, `test_binfile.c`, `test_vpd.c`, `test_ocean.c`,
  `test_vigo.c`
- `tests/fuzz_binfile.c`, `fuzz_vpd.c` + corrupt-file corpus

**Done when:** every documented sentence and header variant round-trips, fuzzers
run 10 M execs clean, oceanographic functions match published check values.

---

## Phase 2 — Instrument link (2 days) **HW**

- `plat_serial.c` for Linux first, then Windows and macOS
- Port enumeration and SWiFT auto-detect via `#003`
- `sv_session.c` FSM: interrupt, identify, configure, run mode, with deadlines
- Read and write every config code in docs/SWIFT_PROTOCOL.md §2, with read-back
  verification
- Protocol log: one line per exchange, on disk

**Done when:** a real SWiFT can be identified, fully configured and returned to
run mode, and a pulled cable leaves the app in `DISCONNECTED` with no leak.
Needs a real instrument — command echo behaviour is not fully specified in the
guide.

---

## Phase 3 — Download (1.5 days) **HW**

- `#400` / `#406` directory navigation and browse UI
- `#402` basic extraction, streaming to a temp file with progress and cancel
- Filename reconstruction from `$PVBB` for the automatic path
- Verify-then-keep; delete only on explicit user action
- Batch download and resume

`#433` acknowledged extraction is **deferred** — the protocol figure is blank in
our copy of the guide (ARCHITECTURE.md §11.1).

**Done when:** a full SD card downloads over Bluetooth without a corrupt file,
and an interrupted download resumes.

---

## Phase 4 — Display (2 days)

- `sv_plot.c`: profile plot, multi-cast overlay, cursor readout, zoom/pan
- `sv_chart.c` + `sv_geo.c`: cast positions in plan, graticule, track, scale
  bar, range and bearing — no basemap imagery, by decision
- Live continuous-mode trace
- `sv_theme.c` dark/light palette, HiDPI scaling
- Status strip with deploy-flag diagnosis (§3 of the protocol doc)

**Done when:** a downloaded cast plots correctly against the same cast opened in
Ocean, and the window resizes and rescales without artefacts.

---

## Phase 5 — Processing and export (1.5 days)

- Down/up-cast split, despike, thin, depth bin, extend-to-bottom, surface fill
- Export: `.asvp`, `.svp`, `.vel`, QINSy, EIVA, CSV, `.vp2`
- Auto-export on download with filename template

**Done when:** exported `.asvp` and `.vel` files are byte-compatible with Ocean's
output for the same input cast.

---

## Phase 6 — Vigo depth reporting (0.5 day) **HW**

Small phase — one UDP socket and three message types. `sv_vigo.c` is pure
formatting and reply matching, so most of it is unit-tested in Phase 1.

- `Q` probe at startup and on a timer; "winch reachable" indicator
- `VP-NNN,Valeport-Winch-Go,<depth>` broadcast on :8090, `ACK` matched and
  logged, `VP-NNN` incrementing per cast to defeat Vigo's dedupe
- `F` on download failure, `BTRST` awaited, bounded retry
- Every datagram sent and every reply (or absence) written to the protocol log

**Done when:** a real cast's depth is accepted by Vigo — dive-table entry
recorded, `transfer-complete-event` seen on the winch UI — with VigoDepthRelay
not running. Then repeat the same depth immediately to prove the `VP-NNN`
increment defeats the dedupe.

---

## Phase 7 — Unattended download loop (1 day) **HW**

The operator commands casts from the winch; svpview reacts to them.

- Detect new cast from the `$PVBB` file timestamp, reconstruct the filename
- Download → verify → parse → plot → export → broadcast depth
- Induced-failure handling: BT drop mid-download → `F` → `BTRST` → retry;
  corrupt file → keep on card, report, do not broadcast a bogus depth

**Done when:** ten consecutive operator-commanded casts are downloaded,
exported and reported without intervention, and each induced failure ends
safely and is logged.

---

## Phase 8 — Hardening and release (1 day)

- Watchdog, autosave, crash-log-on-exit
- Soak test: 24 h continuous mode with periodic download
- README, operator guide, `CHANGELOG.md` promoted to a dated release
- ~~Operator guide~~ **done** — `src/sv_help.c` is the manual, drawn by the
  Help page (F1) and written out as `docs/HELP.md` by `--help-doc`; every
  control also carries a tooltip
- ~~Windows build smoke-tested on a real machine~~ **done 2026-08-25** —
  cross-built with mingw-w64, run on Windows 11, serial-port and adapter
  enumeration both confirmed there. The link itself is still untested on
  Windows, as it is everywhere else
- macOS build smoke-tested on a real machine — **not started**, never
  compiled

---

## Deferred, explicitly

| Item | Why | Route |
|---|---|---|
| Winch control of any kind | Out of scope by decision — casts are commanded from the winch's own UI and panel | Vigo's TCP :8092 API exists if this is ever wanted |
| `#433` acknowledged extraction | Protocol figure blank in the PDF | Ask Valeport, or capture Ocean doing a download |
| AML profiler support | Different instrument family entirely | Only if the fleet needs it; the core is structured to allow a second instrument backend |
| Mobile / phone app | Out of scope | — |
