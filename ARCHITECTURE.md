# svpview — Architecture

**Status:** design, no implementation yet
**Target:** C99 + SDL2, cross-platform (Linux / Windows / macOS)
**Purpose:** replace Valeport Ocean for Valeport SWiFT profilers, and VigoDepthRelay for winch depth reporting

---

## 1. What this is

`svpview` downloads, displays, processes and exports data from Valeport SWiFT
SVP / SWiFT CTD / SWiFTplus profilers, and reports each cast's achieved depth
to a C-MAX Vigo winch. It replaces two pieces of software in the survey chain:

- **Valeport Ocean** — instrument configuration, download, plotting, export
- **VigoDepthRelay** — the .NET shim that watches a download folder and
  broadcasts the achieved cast depth to the winch

Collapsing both into one program removes the folder-watching round trip: the
depth is broadcast the instant the file is parsed, by the process that
downloaded it.

**svpview does not control the winch.** It speaks the profiler UDP protocol on
:8090 — `Q`, `V` and `F` — and nothing else. Casts are commanded from the
winch's own web UI and physical panel, exactly as they are today. See §7.

### The six requirements, as engineering decisions

| Requirement | How it is met |
|---|---|
| 1. SDL2 | SDL2 + SDL2_ttf for window/input/text. Nuklear (single-header, vendored) for widgets — same stack as `cm2view` |
| 2. Cross-platform | All OS-specific code behind `plat_*.h` (serial, sockets, paths, time). No OS calls above that line |
| 3. Ocean feature parity | §3 feature inventory, tracked as an explicit checklist in ROADMAP.md |
| 4. Modern design | One-window, dark-first theme, no modal-dialog maze; live plot always visible; §8 |
| 5. Isn't rubbish | Portable core is UI-free and unit-tested headless; the whole instrument protocol is driven by an explicit FSM, not by sleeps and string matching |
| 6. Doesn't crash | §9 — no allocation in the render loop, bounded buffers everywhere, every parser fuzzed, ASan/UBSan in CI, no untrusted-length trust in the binary file reader |

---

## 2. Layering

The single most important rule: **the core knows nothing about SDL, and the UI
knows nothing about sockets.**

```
┌──────────────────────────────────────────────────────────────┐
│  UI layer            sv_ui.c  sv_plot.c  sv_theme.c          │
│                      sv_pages_*.c   (SDL2 + Nuklear)         │
│  reads: app state snapshot     writes: intent (commands)     │
├──────────────────────────────────────────────────────────────┤
│  App layer           sv_app.c   — owns state, pumps queues   │
│                      sv_session.c — instrument FSM           │
│                      sv_cast.c    — cast/winch FSM           │
├──────────────────────────────────────────────────────────────┤
│  Core (portable, no I/O, no UI — 100% unit-testable)         │
│    sv_proto.c    SWiFT #NNN command codec, $PV* NMEA parser  │
│    sv_binfile.c  .bin header + sample decoder (3 variants)   │
│    sv_vpd.c      .vpd / .vp2 read + write                    │
│    sv_profile.c  cast model, despike, thin, bin, average     │
│    sv_ocean.c    UNESCO/EOS-80: depth, salinity, density, SV │
│    sv_export.c   asvp / svp / vel / csv / vp2 writers        │
│    sv_vigo.c     Vigo TCP command codec + UDP depth codec    │
├──────────────────────────────────────────────────────────────┤
│  Platform          plat_serial.c  plat_net.c  plat_path.c    │
│                    plat_time.c    plat_dir.c                 │
│  termios / Win32 COM / IOKit   ·   BSD sockets / Winsock     │
└──────────────────────────────────────────────────────────────┘
```

Dependencies point downward only. `sv_proto.c` takes a byte buffer and returns
a parsed struct; it never reads a socket. That is what makes the protocol
testable without an instrument on the bench, and it is why this program will
not be rubbish.

---

## 3. Feature inventory (Ocean parity)

**Connection**
- USB serial (FTDI, 230400 8N1) and Valeport Bluetooth key (also a COM port —
  no Bluetooth stack needed, see docs/SWIFT_PROTOCOL.md §1)
- Port enumeration with SWiFT auto-detect (`#003` serial number probe)
- Reconnect on cable drop without losing app state

**Live status** (from the 10-second `$PVBB` broadcast)
- Serial number, hardware ID, last fix lat/lon, battery hours remaining
- Timestamp of last recorded file
- **Ready-to-deploy flag** with the four diagnosed failure causes surfaced as
  plain-English fixes, not an error code (guide §4.4)

**Configuration** (all of §3 of the integration guide)
- Operating mode: Smart Profile / Continuous; up-cast / down-cast
- Trigger depth, depth increment, trigger step
- Require-GPS-before-logging; auto power-down timeout; Bluetooth sleep mode
- Site info string (ASCII and Unicode), instrument clock set/read, tare read
- Read-back verification after every write — never assume a set took

**Download**
- SD card browse: directory tree navigation (`#400` / `#406`)
- File extract (`#402`), with `#433` acknowledged mode when firmware ≥ D0
- Batch download, resume, delete-after-verify (never delete before verify)
- Local library of downloaded raw `.bin` — the raw file is always kept

**Display**
- Live continuous-mode trace (SV / temperature / pressure vs time)
- Profile plot: SV, temperature, salinity, density vs depth, multi-cast overlay
- Cursor readout, zoom/pan, auto and manual axis ranges

**Processing**
- Down-cast / up-cast split, despike, thin to N points, depth binning
- Derived: depth from pressure, salinity, density, sound velocity (CTD)
- Manual tare override, extend-to-bottom, surface fill

**Export**
- Kongsberg `.asvp`, Caris `.svp`, Hypack `.vel`, QINSy, EIVA, plain CSV
- Valeport `.vp2` and legacy `.vpd`
- Auto-export on download, to a configured folder, with a filename template

**Winch depth reporting** (replaces VigoDepthRelay)
- Broadcast `VP-NNN,Valeport-Winch-Go,<depth>` on UDP :8090 after each download
- `Q` probe at startup and periodically — live "winch reachable" indicator
- `F` on a failed download, so the winch jogs the profiler in 100 mm to recover
  the Bluetooth link, then retry

---

## 4. Modules

The table below is what exists. The original plan split the app layer into
`sv_session.c` / `sv_cast.c` and the UI into one `sv_pages_*.c` per page; both
were consolidated during implementation because the split was costing more
plumbing than it bought. `sv_vpd.c` and `plat_win32.c` are planned and not yet
written.

| File | Layer | Responsibility |
|---|---|---|
| `sv_main.c` | ui | SDL init, event pump, frame loop, teardown |
| `sv_ui.c` | ui | Nuklear context, layout shell, all six pages, all dialogs |
| `sv_plot.c` | ui | Profile and live time-series plots |
| `sv_chart.c` | ui | Cast positions in plan: graticule, track, scale bar |
| `sv_theme.c` | ui | Palette by role, light/dark, HiDPI metrics |
| `sv_app.c` | app | State, instrument job FSM, link pumping, winch reporting |
| `sv_sim.c` | app | Wire-level SWiFT emulator, including a synthetic `.bin` |
| `sv_proto.c` | core | `#NNN` command codec; `$PVBB`/`$PVSVP`/`$PVSV1/2`/`$PVCT2` parsers |
| `sv_binfile.c` | core | `.bin` header (3 firmware variants) + sample decode |
| `sv_profile.c` | core | Cast data model and processing |
| `sv_ocean.c` | core | UNESCO 83 depth, PSS-78 salinity, EOS-80 density, Chen-Millero SV |
| `sv_geo.c` | core | WGS84 per-degree, distance/bearing, tangent plane, graticule steps |
| `sv_export.c` | core | Export writers, atomic via temp file and rename |
| `sv_vigo.c` | core | Profiler UDP message codec: `Q` / `V` / `F` build, reply match |
| `sv_config.c` | core | Settings file read/write |
| `plat_posix.c` | plat | Serial, UDP, adapter enumeration, paths, monotonic time |

Naming and layout follow `cm2view` and `sfview` so the three programs stay
readable as a set.

---

## 5. Threading and data flow

**Single-threaded, with non-blocking I/O polled once per frame.** This is a
change from the threaded design first sketched here, made during
implementation and kept deliberately:

```
   frame loop
     ├── sv_ui_input_begin / SDL events / sv_ui_input_end
     ├── sv_app_poll()      ── drain serial (non-blocking) ──► parse
     │                      ── drain UDP    (non-blocking) ──► winch replies
     │                      ── advance the job state machine
     ├── sv_ui_frame()      ── read app state directly, emit widgets
     └── render, present
```

The reason: at 230400 baud a 60 Hz frame is under 400 bytes of serial data,
and the one bulk transfer (a file download) streams straight to disk a chunk
at a time. Threads would buy nothing measurable and would cost a class of
race conditions this program cannot afford — the whole point of §9 is that it
does not crash. There is no lock anywhere in the program, and no shared
mutable state, because there is nothing to share.

**Nothing blocking ever runs.** Every serial and socket handle is opened
`O_NONBLOCK`, every read returns immediately, and every instrument command
carries a deadline enforced by the poll rather than by a wait. A dead
instrument costs one timeout, not a frozen window.

The one thing this design would not survive is a link fast enough to
outpace the frame rate — a 10 Mbit instrument, say. It does not exist here,
and the split between `sv_app` and `plat` is where a reader thread would go
if it ever did.

---

## 6. Instrument session FSM

The SWiFT has two states — *run mode* (streaming, logging) and *interrupted*
(command prompt `>`). Everything hard about this instrument comes from moving
between them, so it is an explicit state machine with timeouts, not sleeps.

```
      DISCONNECTED
           │ open port, probe #003
           ▼
      IDENTIFYING ──timeout──► DISCONNECTED
           │ serial number + firmware known
           ▼
      RUN_MODE  ◄──── #028 ────  INTERRUPTED
           │  '#'                    │  #NNN commands
           └────────────────────────►┘
                                     │  5 min no traffic
                                     ▼  (instrument self-releases)
                                 RUN_MODE
```

Rules encoded in `sv_session.c`:

- Every command carries a deadline. Expiry is a state transition, not a hang.
- The instrument auto-leaves `INTERRUPTED` after 5 minutes with no commands;
  the app tracks that timer itself and re-interrupts rather than being
  surprised.
- **Deploying while interrupted records no profile.** The app warns prominently
  whenever the instrument is left interrupted, because the operator is about to
  deploy it from the winch panel and will get an empty cast.
- Sleep-mode wake is a plain character, never `#` (that interrupts, and then
  needs `#028` to recover). Encoded once, in one function.
- Destructive commands (`#401` erase card, `#404` delete, `#431` delete tree)
  are behind a typed confirmation and are never issued as part of any automatic
  flow.

---

## 7. Vigo winch — depth reporting

The winch is not controlled from here. svpview occupies exactly the slot
Ocean/Connect and VigoDepthRelay occupy today: one UDP socket on **:8090**,
broadcast, three message types distinguished by their first character. Full
detail and the ignore rules in docs/VIGO_INTERFACE.md.

| svpview sends | Vigo replies | Purpose |
|---|---|---|
| `Q…` | `VIGO responding` | probe — is the winch on the network |
| `VP-NNN,Valeport-Winch-Go,<depth>` | `ACK` | achieved cast depth, metres |
| `F…` | `BTRST` | download failed; winch jogs in 100 mm, then retry |

Broadcast address, so no winch IP is configured anywhere — same as Ocean.

Three things about this protocol drive the implementation, and all three are
places where a naive version silently does nothing:

1. **Vigo ignores a message identical to the previous one.** Two consecutive
   casts to the same depth produce identical datagrams and the second is
   discarded. Hence the incrementing `VP-NNN` — real captured traffic shows
   `VP-001` and `VP-003`, and Ocean repeats each datagram many times per cast
   relying on that same dedupe.
2. **Vigo ignores everything until it has performed at least one cast**, and
   ignores `V` if the cast was aborted. Silence is therefore not proof of a
   network fault, and svpview must not escalate on it.
3. **Depth is validated server-side** — dropped if non-finite, ≤ 0, or outside
   0.1× to 10× the requested cast depth.

So `sv_vigo.c` logs the exact bytes sent and whether a reply arrived, and the
status bar shows the last `ACK`. That turns all three silent-drop cases into
something an operator can see, which is the whole reason for writing this
rather than keeping VigoDepthRelay.

The `F` path is the one case where svpview causes the winch to move: a failed
Bluetooth download sends `F`, Vigo checks its proximity switch, jogs the spool
in 100 mm to shorten the Bluetooth path, and replies `BTRST`. svpview then
retries the download, bounded, and stops with a clear message rather than
looping.

UDP :8091 (echo-sounder NMEA depth) is not used — the survey system feeds that
directly.

### Post-cast sequence

```
  $PVBB new-file flag / timestamp change
            ▼
  reconstruct filename from serial + timestamp
            ▼
  download #402 ──fail──► send 'F' ──BTRST──► retry (bounded)
            ▼ ok
  parse ──► plot ──► export
            ▼
  broadcast 'VP-NNN,Valeport-Winch-Go,<depth>' ──► expect ACK
```

Every arrow has a timeout and a failure branch that ends somewhere safe: the
raw file stays on the SD card until a download is verified, and the operator is
told what did not happen.

---

## 8. UI design

Single window, single view stack — no dialog maze. Ocean's weakness is that
routine work (download, look, export) takes four windows; here it is one page
and two clicks.

```
┌────────────────────────────────────────────────────────────────┐
│ SWiFT SVP 46236  ● connected   bat 61.6 h   fix 50.426 -3.681  │ status strip
├──────┬─────────────────────────────────────────────────────────┤
│ LIVE │                                                         │
│ PROF │        plot area — always the largest thing             │
│ FILE │        on screen                                        │
│ CONF │                                                         │
├──────┴─────────────────────────────────────────────────────────┤
│ ready to deploy ✓  winch ACK 10:54:36  last cast 25.4 m        │ action bar
└────────────────────────────────────────────────────────────────┘
```

- **Status strip** is always truthful: connection, battery, fix, deploy flag.
  A red deploy flag names the cause and the fix. The action bar shows when the
  winch last acknowledged a depth report — the one piece of winch state that
  matters here.
- **The plot is the app.** Everything else is a side rail.
- Dark theme first, light theme available; both defined as one palette table
  in `sv_theme.c` (`cm2view`'s approach).
- HiDPI via `SDL_RenderSetLogicalSize` — never `SDL_RenderSetScale`, which is
  clobbered on resize. UI scale auto-detected, overridable by env var and by
  `[` / `]` keys, matching `sfview`.
- Every long operation shows progress and a cancel that actually cancels.
- Keyboard-first: every action reachable without the mouse.

---

## 9. Not crashing, on purpose

This is a design constraint, not a testing phase.

**Memory**
- One arena per cast, freed as a unit. No per-sample `malloc`.
- Zero allocation in the render loop. Frame scratch comes from a fixed arena
  reset each frame.
- Ring buffers, string buffers and path buffers are fixed-capacity; every
  write is length-checked. No `strcpy`, no `sprintf` — `snprintf` only.

**Parsers are the attack surface.** The `.bin` header carries a length field
and a 35-byte version string; a truncated or corrupt file downloaded over a
flaky Bluetooth link must be an error message, not a segfault.
- `sv_binfile.c` and `sv_vpd.c` take `(ptr, len)` and validate every field
  against the remaining length before reading it.
- Both get an AFL/libFuzzer harness in `tests/fuzz_*.c` from the first commit
  that can parse anything.
- Corrupt-file corpus committed to `tests/data/`.

**Failure handling**
- Every I/O call's return is checked. No exceptions to this.
- Disconnect is a normal state, not an error path: the app returns to
  `DISCONNECTED` and keeps all downloaded data.
- Autosave of app state and the cast log every 30 s, written to a temp file
  and renamed atomically.
- A watchdog on the app thread logs a stall over 2 s with the current FSM
  state — so a field hang produces evidence, not a shrug.

**Build and CI**
- `-Wall -Wextra -Werror` release; `-fsanitize=address,undefined` on the debug
  and test builds.
- Unit tests are plain C binaries per module (`tests/test_proto.c`, etc.),
  run by `make test` — the `cm2view` pattern.
- GitHub Actions: build + test + fuzz-smoke on Linux, Windows (mingw-w64) and
  macOS on every push.

---

## 10. Cross-platform strategy

- **Build:** one `Makefile` with `uname`-based platform detection; Windows via
  mingw-w64 cross-compile from Linux, which is the same toolchain CI uses.
  MSVC is not a target — nothing here needs it.
- **Serial:** `plat_serial` is ~120 lines per platform. termios (Linux/macOS)
  and `CreateFile`/`SetCommState` (Windows). Enumeration differs per platform
  and is the only genuinely fiddly part: `/dev/serial/by-id`, `SetupDiGetClassDevs`,
  `IOKit`.
- **Sockets:** BSD sockets; Winsock differs only in startup and `closesocket`,
  handled by three macros.
- **Fonts:** SDL2_ttf with a vendored DejaVu Sans — no system font lookup, so
  the program looks identical on all three platforms and cannot fail to start
  because a font is missing.
- **Paths:** UTF-8 everywhere internally; converted to UTF-16 at the Win32
  boundary only.

Dependencies, complete: SDL2, SDL2_ttf, and vendored `nuklear.h`. Nothing else.

---

## 11. Known unknowns

1. **`#433` acknowledged-extraction protocol.** Section 8.2 of the integration
   guide announces the packet/ack protocol and then the figure is blank in the
   PDF — the diagram did not render. The protocol is therefore undocumented in
   the copy we have. v1 implements basic extraction (`#402`); acknowledged mode
   needs either a readable copy of that figure from Valeport or a capture of
   Ocean doing a download.
2. **Firmware variant detection.** Three `.bin` header layouts (0650734,
   0650735 A/B/C, 0650735 D0+) with different lengths and a CTD/SV split.
   The header carries a size field; the reader must select the layout from
   firmware string *and* validate against that size, not trust either alone.
3. **Real instrument access.** Every protocol decision above is from the
   document. A capture session with a real SWiFT is needed before Phase 2 is
   called done — in particular the exact echo behaviour of `#NNN` commands.
4. **`VP-NNN` numbering.** Captured traffic shows `VP-001` and `VP-003`;
   `VP-000` is Vigo's own synthetic manual-depth message. Whether the number is
   a cast counter, a file index or an instrument code is not documented
   anywhere we have. svpview increments it per cast, which satisfies the only
   constraint that actually matters (Vigo's identical-message dedupe), but it
   is worth confirming against a live Ocean capture.
5. **`Q` and `F` payloads.** Vigo dispatches on character 0 alone, so the rest
   of those datagrams is unconstrained and unobserved. svpview mirrors the `V`
   shape for consistency; a capture of Ocean/VigoDepthRelay would confirm.
