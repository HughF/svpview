# Changelog

All notable changes to svpview are recorded here. This file is the source of
truth for release notes.

## [Unreleased]

### Added
- Initial architecture, roadmap and protocol references. No implementation yet.
  - `ARCHITECTURE.md` — layering, module list, threading model, instrument and
    cast state machines, UI design, crash-resistance strategy
  - `ROADMAP.md` — nine phases with done-criteria and explicit deferrals
  - `docs/SWIFT_PROTOCOL.md` — SWiFT command codes, `$PVBB`/`$PVSV*` sentences,
    the three binary header variants, VPD/VP2 layout
  - `docs/VIGO_INTERFACE.md` — the profiler UDP :8090 protocol (`Q`/`V`/`F`),
    the `VP-NNN,Valeport-Winch-Go,<depth>` wire format, and Vigo's five
    silent-drop rules, read from `vigoServer.js` and captured traffic

### Changed
- Scope reduced: svpview reports cast depth to the winch over UDP :8090 but
  does **not** control it. Vigo's TCP :8092 control API is documented as
  available and explicitly unused.
