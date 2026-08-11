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
  body scrolling under a pinned button row.
- Deploy-flag failures are explained in the operator's terms, not as a code.

### Fixed during bring-up
- `<DIR>` entries truncated directory listings: the `>` inside them was read
  as the command prompt. The prompt is a `>` at the start of a line.
- The command echo was being written into downloaded files, shifting the
  header so every download was rejected as corrupt.
- An out-of-bounds read in the `.bin` reader when the declared header size
  pointed outside the file — found by the test that corrupts that field.
- The Nuklear glyph range was bare ASCII, printing every dash and degree
  sign as `?`.

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
