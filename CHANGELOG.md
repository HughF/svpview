# Changelog

All notable changes to svpview are recorded here. This file is the source of
truth for release notes.

## [Unreleased]

### Added — first working application (2026-08-11)

Builds to a running program with `make`; `make test` runs 289 assertions
across five suites under AddressSanitizer and UndefinedBehaviorSanitizer.

**Core (portable, no SDL, no I/O — all unit-tested)**
- `sv_ocean` — UNESCO 83 pressure-to-depth, Chen & Millero sound speed,
  PSS-78 salinity, EOS-80 density, plus the two numeric inverses a SWiFT SVP
  needs (salinity from measured sound speed, conductivity from salinity).
  Checked against published values: c(35,0,0) = 1449.14 m/s, rho(35,5,0) =
  1027.67547 kg/m3, 9712.653 m at 10000 dBar.
- `sv_proto` — `#NNN` command codec, `$PVBB` status, `$PVSVP`/`$PVSV1`/
  `$PVSV2`/`$PVCT2` data sentences, response framing, directory listings,
  filename reconstruction from the status broadcast.
- `sv_binfile` — all three firmware header variants, SV and CTD, ASCII and
  UTF-16 site info, with every length in the file treated as hostile.
- `sv_profile` — derive, down/up-cast split, despike, largest-triangle
  thinning, depth binning.
- `sv_export` — CSV, Valeport VP2, Kongsberg ASVP, Caris SVP, Hypack VEL,
  written atomically via a temp file and rename.
- `sv_vigo` — `Q`/`V`/`F` message building and reply classification, with
  Vigo's server-side depth rules mirrored so a rejection is explained before
  the datagram goes out.

**Application**
- Single-threaded, non-blocking, polled once per frame — see ARCHITECTURE §5
  for why this replaced the threaded design.
- Instrument session: interrupt, identify, read all settings, return to run
  mode; write only what changed and read back to verify.
- SD card browse and download with progress, then parse, derive and plot.
- `--sim` runs against an emulated SWiFT that speaks the real wire protocol,
  including a synthetic 25 m cast delivered over `#402`, so the whole program
  can be exercised without an instrument.

**Interface** — SDL2 + vendored Nuklear
- Five pages behind a nav rail, always-truthful status strip, action bar.
- Profile plot: four traces on independent scales, multi-cast overlay,
  cursor readout, axis ranges snapped to round numbers.
- Dark and light themes from one role-based palette table.
- Every dialog has Cancel / Apply / OK in that order, right aligned, with the
  body scrolling under a pinned button row, and a title-bar close button;
  the X and Escape both mean Cancel.
- Dialogs are sized to their content rather than to a guessed constant: the
  body is measured as it is drawn and the window height follows, so the
  adapter picker is a row taller when a second adapter appears and no dialog
  hides its own controls behind a scrollbar.
- Deploy-flag failures are explained in the operator's terms, not as a code.
- The log page follows the newest line, but stops following the moment the
  operator scrolls up and resumes when they scroll back to the bottom —
  having the view yanked back mid-read makes the page useless during a
  download.

### Added — network adapter selection
- Settings → Select adapter… lists the machine's **connected** broadcast-capable
  adapters with address, mask and the broadcast that will be used, and binds
  the winch socket to the chosen one. The directed broadcast is computed from
  the address and mask, not taken from the OS, because the Windows path has to
  compute it anyway and a wrong broadcast silently sends the depth report to
  the wrong subnet.
- Verified end to end against a stand-in for `profilerUDPListener`:
  `Q` → `VIGO responding`, `VP-001,…,23.958` → `ACK`, and the *same depth
  again* as `VP-002` → `ACK`, proving the incrementing sequence defeats Vigo's
  identical-message dedupe. All datagrams left the selected adapter's address.

### Fixed during bring-up
- `<DIR>` entries truncated directory listings: the `>` inside them was read
  as the command prompt. The prompt is a `>` at the start of a line.
- The command echo was being written into downloaded files, shifting the
  header so every download was rejected as corrupt.
- An out-of-bounds read in the `.bin` reader when the declared header size
  pointed outside the file — found by the test that corrupts that field.
- The Nuklear glyph range was bare ASCII, printing every dash and degree
  sign as `?`.
- **The Makefile had no header dependency tracking.** Adding a field to a
  struct in `plat.h` rebuilt some objects and not others, which does not fail
  to link — it silently reads the wrong fields. It surfaced as the winch page
  showing the subnet mask where the broadcast address should be. Now built
  with `-MMD -MP` and the generated `.d` files included.
- Dialog buttons sat hard against the window's bottom border.
- Profile x-axis tick labels collided at UI scale 2; labels are now skipped
  where they would overlap, while every gridline is kept.
- Multi-line instrument replies were logged with their embedded CR/LF intact
  and rendered as `?` boxes.

### Changed
- Scope reduced: svpview reports cast depth to the winch over UDP :8090 but
  does **not** control it. Vigo's TCP :8092 control API is documented as
  available and explicitly unused.
- Threading model changed from one I/O thread per link to single-threaded
  non-blocking polling.

### Confirmed
- The `.bin` header-size field counts from itself through the ETX inclusive:
  the field walk yields 246 / 295 / 312 bytes for the three variants, which
  matches the totals the integration guide quotes independently. Asserted in
  `tests/test_binfile.c`.
- The `$PVBB` checksum is a plain XOR between `$` and `*` — the deltas
  between the guide's worked examples reproduce exactly, even though its
  example text is not byte-faithful.

### Still to prove on hardware
Phases 2, 3, 6 and 7 of ROADMAP.md. Everything above is exercised against the
simulator and the unit tests; no real SWiFT and no real winch have been
involved yet.
