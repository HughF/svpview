# Vigo winch — profiler UDP interface

svpview does **not** control the winch. It talks to Vigo only as a profiler
data source, over UDP :8090 — exactly the role Valeport Connect/Ocean and
VigoDepthRelay play today. The winch is commanded from its own web UI and
physical panel, as now.

Reference: `profilerUDPListener.on('message')` in `../Vigo/vigoServer.js`
(server 2026.08.09), and the captured traffic in
`../Vigo/docs/Swift sample UDP messages.txt`.

---

## 1. The link

| | |
|---|---|
| Port | UDP **8090** |
| Address | **broadcast** — no winch IP is configured anywhere |
| Encoding | ASCII, comma-separated, no terminator required |
| Dispatch | Vigo switches on **character 0 only** — `Q`, `V`, `F` |

Vigo replies to the source address and port of the datagram it received, so the
sending socket must stay open and bound to receive the reply.

---

## 2. The three messages

### `V` — cast complete, here is the depth

```
VP-001,Valeport-Winch-Go,1.434
 │      │                 └ depth in metres — LAST comma-separated field
 │      └ fixed literal
 └ message identifier, VP-<NNN>
```

Vigo replies **`ACK`** (3 bytes).

The depth is taken as `parseFloat` of the **last** CSV field, so the number of
intermediate fields does not matter — but matching the observed format exactly
is the safest thing to do, and costs nothing.

`VP-000` is reserved: `vigoServer.js` synthesises `VP-000,<depth>#` internally
when an operator types a depth by hand. Observed real traffic uses `VP-001` and
`VP-003`; the number appears to identify the cast/message rather than the
instrument. svpview increments it per cast — see the dedupe rule below.

**Conditions under which Vigo silently ignores a `V` message.** All of these
are real and all of them look like "the winch didn't see it":

| Condition | Consequence |
|---|---|
| `castCounter == 0` — no cast performed yet this session | ignored entirely |
| Message byte-identical to the previous one | ignored (dedupe) |
| Cast was aborted | ignored |
| Depth not finite, or ≤ 0 | logged and dropped |
| Depth outside 0.1× – 10× the requested cast depth | logged and dropped |

The dedupe rule is why the message carries an incrementing `VP-<NNN>`: two
consecutive casts to the same depth would otherwise produce identical
datagrams, and the second would be discarded. Ocean's own traffic repeats the
same datagram many times per cast and relies on this dedupe, so repeat
transmission is expected behaviour, not a fault.

On a good `V`, Vigo emits `transfer-complete-event` to its web UI and records
the cast in its dive table for replay at that depth.

### `Q` — probe

Any datagram starting `Q`. Vigo replies **`VIGO responding`** (15 bytes) and
stores the sender's address and port as the profiler source.

Used at startup and periodically to confirm the winch is present on the
network, and to show the operator a live "winch: reachable" indicator instead
of failing silently at the end of the first cast.

### `F` — download failed

Any datagram starting `F`. Sent when the file transfer from the profiler
fails — typically a dropped Bluetooth link with the instrument sitting at the
transfer point.

Vigo checks the proximity switch, jogs the spool **in by 100 mm** to improve
the Bluetooth path, re-checks the switch, and replies **`BTRST`** (5 bytes).
If the proximity switch has tripped it sends nothing and warns its own UI.

This is the one and only case where svpview causes the winch to move, and it
does so through the profiler protocol rather than any control API. The retry
loop is: download fails → send `F` → wait for `BTRST` → retry the download.
Bounded retry count, then stop and tell the operator.

---

## 3. Summary of the exchange

| svpview sends | Vigo replies | Then |
|---|---|---|
| `Q…` | `VIGO responding` | winch marked reachable |
| `VP-NNN,Valeport-Winch-Go,<depth>` | `ACK` | cast logged by the winch |
| `F…` | `BTRST` | spool jogged in 100 mm; retry download |

No reply is not necessarily an error — every ignore condition in §2 produces
silence. svpview logs the datagram it sent and whether a reply arrived, so a
missing `ACK` is visible to the operator rather than assumed.

---

## 4. Out of scope

Vigo also exposes a line-based ASCII **control** API on TCP :8092 (queries plus
`$RUNCAST`, `$ABORT`, `$RECOVER` and asynchronous `$EVT:` broadcasts), and a
Socket.IO interface on :10000 for its web UI. **svpview uses neither.** Noted
here only so the next person does not go looking for a reason it was avoided.
