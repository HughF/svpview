# svpview

Valeport SWiFT profiler acquisition, display and export, with C-MAX Vigo winch
control. C99 + SDL2, cross-platform.

Replaces **Valeport Ocean** and **VigoDepthRelay** with one program: it
configures the instrument, downloads and plots casts, exports to the survey
formats, commands the winch, and reports the achieved cast depth back to it —
no folder-watching shim in between.

**Status: design only.** No code yet. Start with the documents below.

| Document | What it covers |
|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | Layering, modules, threading, state machines, UI, crash-resistance strategy |
| [ROADMAP.md](ROADMAP.md) | Nine implementation phases with done-criteria |
| [docs/SWIFT_PROTOCOL.md](docs/SWIFT_PROTOCOL.md) | SWiFT command codes, sentences, binary file formats |
| [docs/VIGO_INTERFACE.md](docs/VIGO_INTERFACE.md) | Vigo TCP command API and UDP depth-report handshake |

## Dependencies

SDL2, SDL2_ttf, and a vendored `nuklear.h`. Nothing else.

```
Debian/Ubuntu   sudo apt install libsdl2-dev libsdl2-ttf-dev
Arch            sudo pacman -S sdl2 sdl2_ttf
```

Windows builds are cross-compiled with mingw-w64; macOS builds natively.

## Related projects

- `../Vigo` — the winch controller this talks to
- `../SDL_Viewer/files` — `cm2view`, the C + SDL2 + Nuklear app this follows
- `../sonar-fpga/topside` — `sfview`, same house style
