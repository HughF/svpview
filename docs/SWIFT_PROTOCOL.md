# Valeport SWiFT — protocol reference

Distilled from *SWiFT Integration Guide*, MANUAL-68251662-19 issue 2.1,
October 2021 (`~/Downloads/SWiFT Integration Guide.pdf`). Section numbers below
refer to that document. This file is the implementation reference for
`sv_proto.c` and `sv_binfile.c`.

---

## 1. Link

Both connection routes present as an **FTDI USB serial port**:

- Direct USB cable to the instrument
- Valeport Bluetooth key — pre-paired to one instrument, auto-connects within
  5–10 m, scans every 10 s

**No Bluetooth stack is required.** The key is a COM port. It will not work
with a generic dongle or built-in Bluetooth, and the instrument holds one BT
connection at a time (key *or* phone app, not both).

Port settings: **230400 8N1**. Commands terminated `<CR><LF>`.

GPS is disabled while the USB comms cable is plugged in (§2.1) — which also
means the deploy flag reads 0 over USB. Expected, not a fault.

---

## 2. Command form

`#NNN` reads, `#NNN;<value>` writes. The instrument echoes the command
characters, then the reply, then the `>` prompt.

`#` at any time interrupts run mode. `#028` returns to run mode. The
instrument self-releases from interrupted state after 5 minutes idle.

### General
| Code | Op |
|---|---|
| `#003` | read serial number |
| `#000;<pw>` | password (`RETAW` = Valeport level, `ID` returns instrument ID) |
| `#010` | read pressure tare |
| `#014` | read firmware version |
| `#028` | enter run mode |
| `#140` / `#141` / `#144` | battery % / volts / hours to empty |

### File management
| Code | Op |
|---|---|
| `#400` | list current directory |
| `#406;<path>` | change directory (`\` = root; full path required for day dirs) |
| `#402;<file>` | extract file (basic, no handshake) |
| `#433;<file>` | extract with per-packet ack (firmware D0+, ~50 % slower) |
| `#403` | free space |
| `#404;<file>` | **delete file — irreversible** |
| `#401` | **erase whole SD card — irreversible, Valeport-level access** |
| `#431;<dir>` | **delete directory and all contents — irreversible** |
| `#407;dd;mm;cc;yy;hh;mm;ss` / `#408` | set / read clock |
| `#410;<s>` / `#411` | set / read 100-char ASCII site string |
| `#412` / `#413` | write / read site info as Unicode hex |

### Profiling
| Code | Op | Default | Range |
|---|---|---|---|
| `#041` / `#042` | operating mode 0=continuous 1=smart profile | 1 | 0–1 |
| `#024` / `#025` | trigger depth (m) | 0.5 | 0.1–100 |
| `#020` / `#021` | depth increment (m) | 0.1 | 0.1–100 |
| `#132` / `#133` | trigger (stop) step (m) | 2 | 0.5–100 |
| `#052` / `#053` | require GPS fix for continuous mode | 1 | 0–1 |
| `#166` / `#167` | profile direction 0=down cast 1=up cast | 0 | 0–1 |

### Power
| Code | Op | Default |
|---|---|---|
| `#015;<min>` / `#016` | auto power-down minutes; `9999` disables | 120 |
| `#160;<0\|1>` / `#161` | Bluetooth wake-from-sleep enable | 0 |
| `#162` | exit configure and sleep (errors if `#160` is 0) |
| `#164` | read DSR-controlled low-power STOP mode |

Wake from sleep: send **any character except `#`**. If `#` is sent the
instrument is interrupted and needs `#028`.

---

## 3. Status broadcast — `$PVBB`

Broadcast every 10 s in smart profile mode while at the surface. This is the
app's primary source of truth about the instrument.

```
$PVBB,00102532,56150,50.4264,-3.6814,66.00,210118154647,0,*42
       │        │     │       │       │     │            │
       │        │     │       │       │     │            └ ready-to-deploy 0/1
       │        │     │       │       │     └ last file YYMMDDhhmmss
       │        │     │       │       └ battery hours remaining
       │        │     │       └ last longitude (999 = no fix)
       │        │     └ last latitude (999 = no fix)
       │        └ serial number
       └ hardware ID
```

Checksum: XOR of all bytes between `$` and `*`.

The algorithm is confirmed — the deltas between the guide's four worked
examples reproduce exactly — but the guide's example *text* is not
byte-faithful (a constant 0x70 offset says characters were lost in
transcription). svpview therefore parses a sentence whose checksum fails and
flags it, counting the failures in the interface, rather than discarding it:
losing a status broadcast costs the operator the deploy flag and the battery
reading, and every field is range-checked independently anyway. Revisit once
a real capture exists.

### Deploy flag = 0 — the four causes (§4.4)

The app must name the cause, not print "not ready":

1. GPS off — USB comms cable plugged in → unplug and use Bluetooth
2. GPS clock reads year 80 → reset GPS, get a fix under open sky
3. GPS date not later than 1/1/2018 → same fix
4. Pressure reading above the trigger point → power-cycle with a valid fix so a
   new file and tare are created

Most common cause is a stale GPS update. Hot fix < 30 s, cold fix minutes.

---

## 4. Real-time data — continuous mode

Sentence identifier depends on firmware and fitted sensors:

| Sentence | Firmware | Fields |
|---|---|---|
| `$PVSVP` | 0650734 | date, time, SOS, `m/s`, pressure, `dBar`, temp, `DegC`, volts, `V`, log index |
| `$PVSV1` | 0650735, no optics | as above |
| `$PVSV2` | 0650735, optics | …, optics1+units, optics2+units, volts, log index |
| `$PVCT2` | 0650735 CTD, optics | conductivity `mS` in place of SOS, then as `$PVSV2` |

Date `YYYYMMDD`, time `hhmmss`, log index in 32 Hz ticks. The 1 Hz continuous
value is the average of a 32 Hz observation period.

The parser keys off the sentence ID and field count, and must tolerate an
unknown ID rather than mis-parse it.

---

## 5. Smart profile operation

Three parameters define a cast (§4.1):

- **Trigger depth** `#024` — depth near surface where logging starts (down cast)
  or stops (up cast)
- **Depth increment** `#020` — one observation per increment
- **Trigger step** `#132` — upward depth change that ends a down cast; must
  exceed local sea and swell or the file closes early

Tare: pressure is re-tared at every GPS fix, stored in dBar in the file header
with a timestamp, and applied to logged pressure.

No fix before deployment → data still recorded, position written as `999,999`.
GPS and all LEDs shut off below ~2 m.

---

## 6. File structure and naming

`VL_<serial>_<YYMMDDhhmmss>.bin`, UTC (GPS-disciplined clock).

Layout: `\<ccyymm>\<dd>\`. Directories exist only for days with data, and a day
directory must be entered by full path — `#406;\202006\20` — not from the month
directory.

The last filename can be reconstructed from `$PVBB` (serial + last-file
timestamp) without listing anything, which is how the automatic download path
finds the file it just recorded.

---

## 7. Binary file format

Common shape: a CRLF-terminated firmware version string (~50 bytes, **not
fixed length**), then a fixed header whose size is given by a 2-byte field,
then fixed-size sample records, header terminated by `0x03` ETX.

All multi-byte numerics are **little-endian floats** unless noted.

| Variant | Header total | Record | Notes |
|---|---|---|---|
| 0650734 | 110 + 36 + 100 | 16 B | tick, SOS, pressure, temp |
| 0650735 A/B/C | 50 + 145 + 100 + 50 | 16 + 4 + 4 B | adds optics type + 2 optics channels |
| 0650735 D0+ | 50 + 145 + 100 + 50 (SV) / 50 + 162 + 100 + 50 (CTD) | 16 + 4 + 4 B | primary parameter is SOS or conductivity by instrument type; user cal applied |

Header fields of interest: instrument family (1 = SWiFT), instrument type
(1 = SV, 2 = CTD), tick rate, datetime, lat, lon, serial, 35-byte version
string, cal date, sample rate, operating mode, pressure tare, cal coefficient
blocks, battery, tare mode, 100-byte site info, optics sensor type.

Sample record: `tick` (u32, 32 Hz), primary parameter (f32), pressure dBar
(f32), temperature °C (f32), then optics 1 and 2 (f32) if fitted.

From D0 the site info may be **Unicode** — first two bytes `0x01 0x01` mark it;
otherwise ASCII.

**Header size, confirmed.** The guide is ambiguous about what the 2-byte
header-size field counts, but walking the field list above gives 246, 295 and
312 bytes for the three variants — which match the totals the guide quotes
independently (110+36+100; 145+100+50; 162+100+50). So the field counts from
itself through the ETX inclusive, and the data begins at
`end_of_version_string + header_size`. `tests/test_binfile.c` asserts all
three numbers, so a mistake in the field list shows up as a failing test
rather than as silently misread casts.

**Reader rules** (`sv_binfile.c`): find the CRLF-terminated version string by
scanning with a bound, read the header-size field, and only treat it as a
candidate if it lands inside the file *and* the byte before it is the 0x03
ETX. If it does not, search a small window for the ETX, then fall back to
where the field walk ended. Never seek past `len` — the offset comes from
inside the file, so it is checked at the point of use, not where it was
computed.

---

## 8. VPD / VP2 files

INI-shaped text. `.vpd` has `[Application] [Header] [Calibrations] [Columns]
[Data]`; `.vp2` (Connect 1.0.4.0+) has `[HEADER] [COLUMNS] [DATA]` and adds the
instrument code and calibration data to the header.

Keys are **not case-sensitive** and header key order is not guaranteed — parse
into a map, never by position.

`[COLUMNS]` entries are `Name=Type;Units;Calculated`, and `[DATA]` repeats the
column names and units as its first two rows before the data.

Where a turbidity sensor is fitted, the `Turbidity` column carries the
nephelometer reading below 1000 NTU and the OBS value above it.
