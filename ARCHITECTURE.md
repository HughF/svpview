# svpview — Architecture

**Status:** design, no implementation yet
**Target:** C99 + SDL2, cross-platform (Linux / Windows / macOS)
**Purpose:** replace Valeport Ocean for Valeport SWiFT profilers, with native C-MAX Vigo winch control

---

## 1. What this is

`svpview` downloads, displays, processes and exports data from Valeport SWiFT
SVP / SWiFT CTD / SWiFTplus profilers, and drives a Vigo profiling winch from
the same window. It replaces two pieces of third-party software in the survey
chain:

- **Valeport Ocean** — instrument configuration, download, plotting, export
- **VigoDepthRelay** — the .NET shim that watches a download folder and
  broadcasts the achieved cast depth to the winch

Collapsing both into one program removes the folder-watching round trip: the
cast depth is known the instant the file is parsed, in the same process that
commanded the cast.

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

**Winch (new — Ocean cannot do this)**
- Connect to Vigo, live winch state, run cast to depth, abort, recover
- Automatic depth report to the winch on download (replaces VigoDepthRelay)
- Cast → download → export → next cast, unattended

---

## 4. Modules

| File | Layer | Responsibility |
|---|---|---|
| `sv_main.c` | ui | SDL init, event pump, frame loop, teardown |
| `sv_app.c` | app | Application state struct, queue pumping, autosave |
| `sv_session.c` | app | Instrument FSM: interrupt → configure → run → download |
| `sv_cast.c` | app | Cast FSM, winch coordination, cast log |
| `sv_ui.c` | ui | Nuklear context, layout shell, page routing |
| `sv_pages_*.c` | ui | One file per page: connect, live, profile, config, files, winch |
| `sv_plot.c` | ui | Profile/time plot renderer (SDL primitives, no Nuklear) |
| `sv_theme.c` | ui | Palette, light/dark, HiDPI scale |
| `sv_proto.c` | core | `#NNN` command codec; `$PVBB`/`$PVSVP`/`$PVSV1/2`/`$PVCT2` parsers |
| `sv_binfile.c` | core | `.bin` header (3 firmware variants) + sample decode |
| `sv_vpd.c` | core | `.vpd` / `.vp2` INI-style read/write |
| `sv_profile.c` | core | Cast data model and processing |
| `sv_ocean.c` | core | UNESCO 83 depth, PSS-78 salinity, EOS-80 density, Chen-Millero SV |
| `sv_export.c` | core | Export writers |
| `sv_vigo.c` | core | Vigo TCP command/response/event codec, UDP depth message codec |
| `sv_log.c` | core | Rotating text log, one line per protocol exchange |
| `plat_serial.c` | plat | Open/enumerate/read/write/close a serial port |
| `plat_net.c` | plat | TCP client, UDP socket, broadcast |
| `plat_path.c` | plat | Config/data dirs, path joining, atomic file replace |
| `plat_time.c` | plat | Monotonic ms, UTC conversion |

Naming and layout follow `cm2view` and `sfview` so the three programs stay
readable as a set.

---

## 5. Threading and data flow

One UI thread. One I/O thread per link. No shared mutable state — everything
crosses on a queue.

```
  serial thread ──► rx ring ──►┐
                               ├──► app thread (pumps queues, owns state)
  vigo tcp thread ──► evt q ──►┤        │
                               │        └──► state snapshot (double-buffered)
  udp thread ────────► q ─────►┘                    │
        ▲                                           ▼
        └──────────── cmd queues ◄──────────── UI thread (render, 60 Hz)
```

- Queues are fixed-capacity SPSC rings, allocated once at startup. A full queue
  drops the oldest telemetry frame and increments a counter shown in the status
  bar — it never blocks and never grows.
- The UI thread renders from a snapshot published by the app thread. A frame
  can never observe a half-updated cast.
- The download path is the one bulk transfer: it streams straight to a temp
  file on the I/O thread and only hands the UI a progress count.

**Nothing blocking ever runs on the UI thread.** Not a file read, not a
`connect()`, not a 5-second instrument timeout. This is the single rule that
keeps the window responsive, and it is enforced by keeping SDL out of the core
and sockets out of the UI.

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
- **Deploying while interrupted records no profile.** The app refuses to arm a
  cast unless it has confirmed run mode, and says so on the winch page.
- Sleep-mode wake is a plain character, never `#` (that interrupts, and then
  needs `#028` to recover). Encoded once, in one function.
- Destructive commands (`#401` erase card, `#404` delete, `#431` delete tree)
  are behind a typed confirmation and are never issued as part of any automatic
  flow.

---

## 7. Vigo winch integration

**This is the finding that shapes the whole winch design:** `vigoServer.js`
already exposes a complete line-based ASCII control API on **TCP :8092** —
14 queries, 10 commands including `$RUNCAST,<depth>`, `$ABORT`, `$RECOVER`,
plus asynchronous `$EVT:` broadcasts. Full listing in docs/VIGO_INTERFACE.md.

So `svpview` needs **no Socket.IO client**. A plain TCP socket and a line
splitter gets full winch control from C. That removes what would otherwise
have been the largest and least reliable subsystem in the program (an
engine.io/WebSocket implementation in C, against a socket.io 2.1.2 server).

Three links to the winch:

| Link | Direction | Purpose |
|---|---|---|
| TCP :8092 | bi-directional | Commands, queries, `$EVT:` events |
| UDP :8090 | outbound (broadcast) | Depth report after download — the VigoDepthRelay job |
| UDP :8091 | outbound, optional | Echo-sounder water depth passthrough, if svpview is fed NMEA |

The UDP depth report must match what the winch already expects, because the
server-side parser is not changing: `Q…` probe answered with
`VIGO responding`, then `V,<csv>,…,<depth>` answered with `ACK`. Broadcast
address, so no winch IP configuration — same as Connect/Ocean.

Vigo validates the reported depth (rejects ≤0, and anything outside 0.1× to
10× the requested cast depth) so a malformed report is dropped, not acted on.
`sv_vigo.c` formats it correctly and logs exactly what went on the wire.

**Gap:** the TCP API covers cast/abort/recover but not jog, brake, level-wind
or drive-enable — those are Socket.IO-only. If the app is to offer manual jog,
the clean route is a small upstream addition to `vigoServer.js`
(`$JOGIN` / `$JOGOUT` / `$BRAKE`), contributed as a PR — Vigo is publicly
distributed and GPL. Manual jog is therefore **out of scope for v1** and the
winch page shows the physical panel as the place to do it.

### Unattended cast loop

```
  arm ──► $RUNCAST,<depth> ──► $EVT:CYCLEFLAG,out
            │                       │
            │                  $EVT:CASTCOMPLETE
            │                       ▼
            │              wait for $PVBB new-file flag
            │                       ▼
            │              download #402 ──► parse ──► export
            │                       ▼
            └──────── UDP 'V,…,<depth>' to :8090 ──► repeat / stop
```

Every arrow has a timeout and a failure branch that ends in a safe state:
winch idle, instrument in run mode, raw file kept on the SD card.

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
│ WNCH │                                                         │
├──────┴─────────────────────────────────────────────────────────┤
│ ready to deploy ✓   winch idle   last cast 25.4 m 10:54:36     │ action bar
└────────────────────────────────────────────────────────────────┘
```

- **Status strip** is always truthful: connection, battery, fix, deploy flag.
  A red deploy flag names the cause and the fix.
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
4. **Vigo docs drift.** `Vigo/ARCHITECTURE.md` documents 5 TCP queries; the
   code implements 14 queries and 10 commands. docs/VIGO_INTERFACE.md here is
   taken from the code and is the accurate one; worth an upstream doc fix.
