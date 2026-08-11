# Vigo winch — control interface

Taken from the Vigo source (`../Vigo/vigoServer.js`, branch `feature/nmea-output`,
server 2026.08.09), not from `Vigo/ARCHITECTURE.md` — that document lists only
5 of the 14 TCP queries and none of the commands. This file is the reference
for `sv_vigo.c`.

---

## 1. Ports

| Port | Proto | Direction (from svpview) | Purpose |
|---|---|---|---|
| 8092 | TCP | bi-directional | Command / query / event channel |
| 8090 | UDP | outbound broadcast | Profiler depth report (the VigoDepthRelay job) |
| 8091 | UDP | outbound broadcast | NMEA water depth from echo sounder |
| 10000 | HTTP + Socket.IO | — | Operator web UI. **Not used by svpview** |

Ports are bound to the broadcast address, so no winch IP needs configuring.

---

## 2. TCP command channel (:8092)

Line-based ASCII. Requests are `\n`-terminated, comma-separated. On connect the
server greets with `VIGO <version>\r\n`.

Responses: `$RSP:<value>\r\n`, or `$RSP:ERR,<reason>\r\n`.
Unsolicited events: `$EVT:<NAME>[,<value>]\r\n` — these arrive at any time and
must not be mistaken for a command response. Match responses by order on a
single outstanding command; treat anything starting `$EVT:` as async.

### Queries

`$QRYCASTTYPE` `$QRYSAFEFLAG` `$QRYPROFILER` `$QRYSTOPMODE` `$QRYCYCLEFLAG`
`$QRYSPEED` `$QRYTORQUE` `$QRYMETERSOUT` `$QRYDEPTH` `$QRYPOWER`
`$QRYVOLTAGE` `$QRYWATERDEPTH` `$QRYCYCLES` `$QRYDROPRATE`

### Commands

| Command | Args | Error replies |
|---|---|---|
| `$RUNCAST,<depth>` | metres, 0 < d ≤ 500 | `ERR,INVALID_DEPTH`, `ERR,NOT_SAFE` |
| `$ABORT` | — | — |
| `$RECOVER` | — | — |
| `$RECOVERALL` | — | — |
| `$SETPROFILER,<name>` | e.g. `SWIFTSVP` | `ERR,UNKNOWN_PROFILER` |
| `$SETCASTTYPE,<type>` | | `ERR,INVALID` |
| `$SETSTOPMODE,<mode>` | | `ERR,INVALID` |
| `$SETSPEED,<rpm>` | 600–1200 | `ERR,RANGE_600_1200` |
| `$SETTORQUE,<pct>` | 0–max | `ERR,RANGE_0_<max>` |
| `$SETPOWERBUDGET,<W>` | 250–1000 | `ERR,RANGE_250_1000` |

Any unrecognised verb returns `ERR,UNKNOWN_CMD`.

### Events

`$EVT:CASTCOMPLETE` `$EVT:CASTFAIL` `$EVT:CYCLEFLAG,<done\|out\|retract>`
`$EVT:TRANSFERSET` `$EVT:ESTOP` `$EVT:DRIVEFAULT` `$EVT:PROXTRIPPED`

### Cast cycle

```
done ──$RUNCAST──► out ──freefall complete──► retract ──recovery complete──► done
                                                                    $EVT:CASTCOMPLETE
```

`$QRYSAFEFLAG` reflects `safeToCast`, set when the winch has confirmed its data
transfer position. `$RUNCAST` returns `ERR,NOT_SAFE` if it is not set — do not
retry-loop on that, surface it.

**Not available over TCP:** jog in/out, brake, level wind, drive enable, beacon
and sounder tests. Those are Socket.IO events only. See ARCHITECTURE.md §7.

---

## 3. Profiler depth report (UDP :8090)

The message format is fixed by the existing server-side parser; svpview must
match it exactly.

| Sent by svpview | Vigo replies | Meaning |
|---|---|---|
| `Q…` | `VIGO responding` | Probe. Vigo records the sender address/port |
| `V,<csv fields…>,<depth>` | `ACK` | Cast complete; **depth is the last CSV field**, metres |
| `F…` | — | Transfer failure; Vigo jogs the spool in 100 mm and waits for a new message |

Server-side conditions worth knowing, because they cause silent drops:

- Messages are ignored entirely until at least one cast has been performed
  (`castCounter > 0`).
- A message identical to the previous one is ignored — so a repeated depth for
  two consecutive identical casts must differ somewhere in the CSV (include the
  file timestamp).
- Depth must parse finite and > 0.
- Depth is rejected if outside 0.1× to 10× the requested cast depth.
- `V` messages are ignored if the cast was aborted.

On a good `V` message Vigo emits `transfer-complete-event` to its web UI and
records the cast in its dive table for replay at that depth.

---

## 4. Water depth (UDP :8091)

Standard NMEA depth sentences: `$DBT`, `$DPT`, `$DBK`, `$DBS`, `$DBL`.
Only relevant if svpview is also fed an echo-sounder feed; otherwise the survey
system sends these directly and svpview stays out of it.
