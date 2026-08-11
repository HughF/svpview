# svpview — implementation roadmap

Nine phases. Each ends with something runnable and tested; no phase depends on
hardware that is not already on the bench, except where marked **HW**.

---

## Phase 0 — Skeleton (0.5 day)

Build system, platform layer stubs, window that opens and closes cleanly.

- `Makefile` with `uname` platform detect, release/debug/asan targets
- `plat_time.c`, `plat_path.c`
- SDL2 + SDL2_ttf + vendored `nuklear.h`, HiDPI logical-size setup
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
- `tests/test_proto.c`, `test_binfile.c`, `test_vpd.c`, `test_ocean.c`
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

## Phase 6 — Vigo integration (1.5 days) **HW**

- `sv_vigo.c` TCP client: commands, queries, `$EVT:` demux, reconnect
- UDP :8090 depth report with the `Q`/`V`/`F` handshake
- Winch page: state, run cast to depth, abort, recover
- Cast log correlating commanded depth against achieved depth

**Done when:** a cast commanded from svpview completes on the winch and the
achieved depth is accepted by Vigo (dive-table entry recorded), with
VigoDepthRelay not running.

---

## Phase 7 — Unattended loop (1 day) **HW**

- Arm → cast → wait → download → export → report → repeat
- Every arrow timed out, every failure ending in a safe state
- Operator can abort at any point

**Done when:** ten consecutive unattended casts complete without intervention,
and an induced failure at each stage (winch abort, BT drop, corrupt file) ends
safely and is logged.

---

## Phase 8 — Hardening and release (1 day)

- Watchdog, autosave, crash-log-on-exit
- Soak test: 24 h continuous mode with periodic download
- README, operator guide, `CHANGELOG.md` promoted to a dated release
- Windows and macOS builds smoke-tested on real machines

---

## Deferred, explicitly

| Item | Why | Route |
|---|---|---|
| Manual jog / brake / level wind | Not exposed on Vigo's TCP API | Upstream PR adding `$JOGIN`/`$JOGOUT`/`$BRAKE` to `vigoServer.js` (GPL, publicly distributed) |
| `#433` acknowledged extraction | Protocol figure blank in the PDF | Ask Valeport, or capture Ocean doing a download |
| AML profiler support | Different instrument family entirely | Only if the fleet needs it; the core is structured to allow a second instrument backend |
| Mobile / phone app | Out of scope | — |
