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
  - `docs/VIGO_INTERFACE.md` — Vigo TCP :8092 command API and UDP :8090 depth
    handshake, read from `vigoServer.js` rather than from Vigo's own docs
