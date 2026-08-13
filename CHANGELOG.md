# Changelog

All notable changes to svpview are recorded here. This file is the source of
truth for release notes.

## [Unreleased]

### Fixed — the chart drew outside itself (2026-08-13)
- The track ran off the plot, across the toolbar and out of the window. Nuklear's
  stroke and fill commands are not bounded by the rect the coordinates were
  computed from, and everything on a chart is positioned by where it is in the
  world, not by where the plot happens to be — so panning, zooming in, or simply
  following a vessel that has moved put the track wherever the arithmetic said.
  The data layers (track, cast markers and their labels, the live position) now
  draw under their own scissor, intersected with the clip already in force so it
  can only narrow. Chart furniture — graticule labels, scale bar, north arrow,
  cursor readout — is drawn after the restore, as it belongs in the gutters.
- Track segments with both ends off the same edge of the plot are now rejected
  before drawing. Zoomed in, most of a track is not merely invisible but
  thousands of screen-widths away, and vertices at that magnitude lose the
  precision that places the end that *is* visible.
- The summary read "4 casts plotted" over a chart showing two. It now says
  "2 of 4 casts in view" whenever any positioned cast is outside the plot.

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
- Window title carries the version and, once connected, the instrument and
  port — so a screenshot identifies the build and two profilers open at once
  can be told apart from the taskbar.
- About dialog (nav rail): version, build stamp, SDL versions at runtime and
  at compile time, UI scale, the connected instrument, and the protocol
  reference. Single OK button, since Apply and Cancel have nothing to act on
  in a dialog that changes nothing.
- The log page follows the newest line, but stops following the moment the
  operator scrolls up and resumes when they scroll back to the bottom —
  having the view yanked back mid-read makes the page useless during a
  download.

### Added — Chart page (2026-08-12)
- A plan view of where the casts were taken, from data the program already had
  and nothing displayed: the `.bin` header carries a 32-bit float latitude and
  longitude, and `$PVBB` carries the live fix.
- `sv_geo` — WGS84 metres per degree, distance and bearing, a local tangent
  plane, sexagesimal graticule steps, and degrees-and-decimal-minutes
  formatting. 106 assertions. The plane is used because the job is casts tens
  of kilometres apart at most, where it beats a spherical formula on a mean
  radius by about 0.3%; distance and bearing fall back to the spherical forms
  past a third of a degree.
- `sv_chart` — graticule on whole minutes and seconds rather than round metres,
  cast markers with time and depth, the track from the status broadcasts, scale
  bar, north arrow, and a cursor readout carrying range and bearing from the
  current position. Drag pans, the wheel zooms about the cursor, a click
  selects the cast under it and the Profile page follows.
- **No basemap imagery**, and no new dependency: tiles would need a tile
  server, an image decoder, a cache and an internet connection the boat does
  not have, to show what a positioning plot does not need.
- Two things are stated rather than blended. Casts logged without a GPS fix are
  counted in the panel instead of quietly missing from the plot. And the live
  position carries its age, drawn hollow once it stops being refreshed — the
  instrument broadcasts nothing at the command prompt, which is exactly when
  the operator is downloading, so that marker is routinely minutes stale.
- The track keeps only fixes more than 15 m apart, because `$PVBB` gives
  position to four decimal places (~11 m of latitude) and a stationary
  instrument crossing a quantisation cell would otherwise fill the track with
  jitter that looks like movement.

### Fixed — the profile plot could abort the program
- Overlaying four casts crossed Nuklear's 16-bit vertex index limit (65535
  vertices, about 16000 line segments) and the assertion inside
  `nk_draw_list_alloc_vertices` killed the process. Found by opening four casts
  from the simulator's card with all four traces on — an ordinary thing to do,
  one cast away from the operator.
- Fixed at both ends. `NK_UINT_DRAW_INDEX` moves the indices to 32 bits, which
  also needed a fix to the vendored `nuklear_sdl_renderer.h`: it hard-codes the
  index size as 2 bytes in its `SDL_RenderGeometryRaw` call, so with 32-bit
  indices SDL reads garbage. That one is an upstream bug and is marked as such
  in the file. And `sv_plot` now decimates each trace to the pixel grid, so a
  frame's vertex count is bounded by the size of the plot rather than by how
  many samples a cast holds — a 1200-sample cast drew 1200 segments where a few
  hundred are visually identical.
- Verified by reproducing the exact case that aborted: four casts, four traces,
  overlaid.

### Changed — simulator
- The simulated vessel is under way: a survey line at 4 knots, gently turning,
  with the position integrated in one place so the broadcast and the position
  written into a downloaded file cannot disagree.
- The card now holds four casts with their own timestamps, positions and
  depths, laid out along the line astern of the vessel. A stored cast was
  recorded where the instrument was *then* — the emulator previously stamped
  every download with the current position, which would have plotted every
  cast on top of the boat.
- `$PVBB` is emitted with the four decimal places the integration guide's own
  example carries, rather than more precision than the instrument gives.

### Fixed — the settings page's scroll offset leaked to every other page
- All six pages share one Nuklear window, and therefore share its scroll
  offset. Scrolling the settings page down and switching to another page left
  that page drawn shifted up — the chart's control row off the top, with no
  scrollbar to bring it back, because the other pages are
  `NK_WINDOW_NO_SCROLLBAR`. Each page now starts at its top. Found while
  taking documentation screenshots, not by a test.

### Added — licence
- **GPL-3.0-or-later**, copyright (C) 2026 Hugh Frater. Full text in `LICENSE`,
  the SPDX identifier and short notice at the top of every source file, and the
  notice in the About box, which is where the GPL's own "How to Apply" section
  asks a GUI program to put it.
- Version 3 or later because nothing svpview links forces otherwise: SDL2 is
  zlib and the vendored Nuklear is MIT or public domain, both GPL-compatible.
- `third_party/README.md` records what is vendored, under which licence, and
  the one local modification carried against upstream (the hard-coded vertex
  index size in `nuklear_sdl_renderer.h`) so a future re-vendor does not
  silently drop it.

### Added — documentation
- `docs/svpview_Design_and_Features.{html,pdf}` — an illustrated tour of the
  application and the reasoning behind it: the six pages with screenshots, the
  design rules and what each costs in code, the protocol findings, the
  oceanography check values, the winch link, the architecture, the test
  discipline, and a plain table of what is proven versus simulator-only.
  Self-contained HTML in the house style; PDF rendered with headless Chrome.
- `ARCHITECTURE.md`'s module table now lists the modules that exist rather than
  the ones originally planned, and says which were consolidated and why.

### Fixed — page layout: rows below a full-height plot were being dropped
- The profile page's summary line was invisible at UI scale 2, and the chart's
  was too. Sizing a plot from the content region's full height leaves the row
  after it beyond the panel's clip — which `nk_window_get_content_region` does
  not account for — and Nuklear *drops* such a row entirely rather than
  clipping it, so it vanishes with no visual clue. Both pages now put their
  summary line above the plot and let the plot fill the rest, so there is no
  row afterwards to lose.
- Chart latitude labels are given a gutter measured from the font rather than a
  constant, which was clipping `50° 25.700' N` at scale 2; cast labels flip to
  the left of their marker rather than running off the right-hand edge.

### Fixed — opening window size on a HiDPI display
- The window opened at the layout's design size of 1280x820 *pixels*, but every
  metric in the layout is multiplied by the UI scale as it is drawn, so on a
  192 dpi panel the interface had only 640x410 of room to work in and the
  window covered a tenth of the screen. The opening size is now the design
  size multiplied by the scale in force — 2560x1640 on this display — clamped
  to 85% of the desktop's *usable* bounds so it cannot open with its action bar
  behind a panel. Converting through the drawable/window-size ratio keeps a
  Retina Mac at 1280x820 points, where the window system applies the factor
  itself and multiplying again would double-count it.
- A minimum window size is set, from the width the status strip needs before
  "NOT ready to deploy" starts being cut off mid-word.
- The window is created hidden and shown once it has been sized, so it no
  longer visibly jumps from the base size to its real one at startup.

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
