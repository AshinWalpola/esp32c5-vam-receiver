# Step 4 - Build, flash, and validate the HLN-SV DENM transmitter

## Radio-config fix (2026-07-15) - read this first

Symptom: nothing on air. Proof it was a radio-config problem, not a
frame-format problem: the 2026-07-15 sniffer capture (`recordings/
its5_20260715_131448.pcap`) contains 13,584 ITS-G5 frames on 5.9 GHz but
**zero** from our source MAC `02:00:00:00:00:01`. A sniffer records a
station's frames even if the payload is malformed, so the frame was never
leaving the radio - the DENM/GeoNet/802.11 encoding was never the issue.

Three stacking causes, all in `main.c`'s Wi-Fi init, now fixed:

1. **Plain STA, no promiscuous, power-save on.** ESP-IDF only actually emits
   raw frames when the MAC is in promiscuous mode or associated to an AP, and
   default STA power-save sleeps the radio between beacons and drops outbound
   frames. The *working sniffer* runs promiscuous - the OBU didn't. Fixed:
   `esp_wifi_set_ps(WIFI_PS_NONE)` + `esp_wifi_set_promiscuous(true)` after
   `esp_wifi_start()`.
2. **Never entered 5 GHz band mode.** The stuck `esp_wifi_get_channel()
   primary=1` was the dual-band C5 still in 2.4 GHz band mode, so
   `esp_wifi_set_channel(<5G channel>)` failed silently and the HMAC TX path
   keyed the 2.4 GHz PHY on ch1 - inaudible to a 5.9 GHz sniffer. Fixed:
   `esp_wifi_set_band_mode(WIFI_BAND_MODE_5G_ONLY)` before start. Channel prime
   changed from 140 (5700 MHz) to **177 (5885 MHz)** - see cause 3.
3. **5900 MHz is out of the C5's spec range.** The datasheet 5 GHz range is
   **5180-5885 MHz**; our target 5900 MHz (ITS-G5 G5-CCH) is 15 MHz above it.
   RX tolerates 15 MHz over (that's why the sniffer works at 5900); TX may be
   PA-calibration-gated at an uncalibrated frequency. Priming the driver to
   channel 177 (5885, the top legal channel) keeps TX on the closest real
   power table before `phy_change_channel(5900,...)` nudges the LO.

### If it STILL shows nothing after these fixes - the 5900 MHz isolation test

Causes 1 and 2 are high-confidence. Cause 3 is the remaining unknown, so
isolate it before touching anything else. Temporarily change **both** ends to
a channel inside the C5's spec range and see if the OBU appears:

- In `main.c`, change the target frequency from `5900` to `5885` in *both*
  `phy_change_channel()` calls (boot + per-TX), matching the channel-177 prime.
- Put your sniffer on 5885 MHz too.
- Ground GPIO4 and watch the dashboard/PCAP for source MAC
  `02:00:00:00:00:01`.

If the OBU now appears at 5885 but not at 5900: TX genuinely can't key the PA
at the out-of-spec 5900 MHz, and no amount of frame-format work will change
that - you'd need a chip whose 5 GHz range covers the ITS band, or to accept
operating one channel down. If it appears at neither, the problem is still in
the TX-enable path (recheck that the three calls above returned ESP_OK in the
boot log), not the frequency.

Also note: **nothing transmits until GPIO4 is grounded** (active-low hazard
input). If you were bench-testing without grounding GPIO4, the TX path was
never even entered - ground it (jumper GPIO4 to GND) before concluding
anything.

---

Profile: **HLN-SV** (aftermarket stationary recovery vehicle) - causeCode 94
(stationaryVehicle), subCauseCode 0, transmits only while the hazard-light
GPIO reads active. No location/alacarte containers, no optional fields.

Code lives in `obu-firmware/main/`: `main.c` (entry, PHY hack, GPIO, TX loop),
`denm.c/.h` (ASN.1 UPER encoding), `geonet.c/.h` (GeoNetworking + BTP-B
wrapping), `dot11p.c/.h` (802.11 OCB frame assembly), `tx_custom.c/.h` (the
actual raw-frame transmit path - see "The TX bypass" below).

## The TX bypass (tx_custom.c)

`esp_wifi_80211_tx()` - ESP-IDF's public raw-frame API - rejects QoS Data
frames outright (`esp_err 258` / "unsupport QoS frame type"), and there's no
supported way to override that from application code (a linker-level symbol
override attempt, `main/wifi_patches.c`, is kept around only for history -
confirmed not to work).

`tx_custom.c` was pulled from
[opentrafficmap/its-g5-receiver-firmware_txenabled](https://codeberg.org/opentrafficmap/its-g5-receiver-firmware_txenabled)
- a TX-enabled fork of the exact receiver firmware (V2X2MAP) already used on
the RX side of this project, same authors, same chip. Instead of trying to
disable the gate, it skips the code path that contains it entirely: it calls
`ic_ebuf_alloc()` and `ieee80211_post_hmac_tx()` - undocumented internal
driver functions - directly, submitting straight to the MAC. `main.c` now
calls `esp_wifi_80211_tx_custom()` instead of `esp_wifi_80211_tx()`, with the
one proven-working parameter set (`WIFI_PHY_MODE_11A`, `WIFI_PHY_RATE_12M`,
`WIFI_BAND_5G`, `WIFI_BW20`) copied from the only call site in the upstream
repo (`mqtt.cpp`, triggered by an incoming MQTT message there - ours is
triggered by the GPIO4 hazard-light logic instead).

**What this unlocks**: `dot11p.c` is back to real QoS Data frames (subtype 8)
since the gate that forced the non-QoS downgrade no longer applies. Frame
size is now 117 bytes (was 115 with non-QoS Data's 2-byte-shorter header) -
expect `sent (117 bytes)` in the log now, not 115.

**What's NOT yet independently verified** - things worth checking as you go:
- Whether `ieee80211_post_hmac_tx`, `ic_ebuf_alloc`, `ic_get_default_sched`,
  `g_osi_funcs_p`, and `g_wifi_global_lock` actually exist as symbols in
  *your* IDF version's `libnet80211.a`/`libpp.a` for esp32c5 - we only
  previously confirmed `ieee80211_raw_frame_sanity_check` exists via `nm`,
  not these. If the build fails to link with `undefined reference`, this is
  the first thing to check - same `nm` approach as the `phy_11p_set` section
  below, just against these symbol names.
- `tx_custom.c`'s internal struct layouts (`x_eb_txdesc_t`, `x_ebuf_t`) are
  reverse-engineered from the closed WiFi driver, pinned only by a `sizeof()`
  assert - that catches a total-size mismatch across IDF versions but not a
  field-order mismatch that happens to keep the same total size. If your IDF
  version differs meaningfully from whatever the upstream repo has pinned
  (check their `esp-idf` git submodule commit vs. `idf.py --version`), this
  could compile and link cleanly but write to the wrong internal offsets.
  Worth keeping in mind as a possible explanation if you get a crash/hang
  right when TX fires rather than a clean error.
- Same "skips ALL sanity checking" risk as the old override attempt: a
  malformed frame from a bug anywhere in `denm.c`/`geonet.c`/`dot11p.c` could
  now behave worse (crash, silent corruption) than a clean rejection.
- `esp_wifi_set_channel(140, ...)` was added in `main.c` right before the
  `phy_11p_set`/`phy_change_channel` pair, copied from upstream's
  `cmd_sniffer.c` (their own comment on it: "not sure if strictly needed").
  Motivation: our own `esp_wifi_get_channel()` diagnostic was reporting a
  stuck `primary=1` regardless of what the PHY hack was told, consistent
  with the driver's channel bookkeeping never being touched by anything it
  tracks. This is a "worth trying," not a confirmed fix - watch whether
  `esp_wifi_get_channel()`'s reported value changes at all now.

## Confidence levels - read this before debugging blind

Updated after pulling the actual specs (EN 302 636-4-1, EN 302 636-5-1) and
the real ASN.1 modules from forge.etsi.org (EN 302 637-3, TS 102 894-2 CDD) -
this isn't guesswork anymore for the parts listed as "verified" below.

- **GeoNetworking Basic/Common/SHB headers, BTP-B header** (`geonet.c`):
  verified field-by-field against EN 302 636-4-1. This caught three real
  bugs in the previous version: wrong header type (was encoded as
  GeoUnicast, HT=2 - now correctly TSB/SINGLE_HOP, HT=5/HST=0), wrong
  payload-length calculation (was including the 24-byte extended header,
  which it shouldn't), and GN_ADDR being an arbitrary byte string instead of
  its actual structure (M-flag + 5-bit station type + reserved + 48-bit
  MID = the same link-layer address used in the 802.11 header).
- **BTP-B header** (`geonet.c`): verified against EN 302 636-5-1 - unchanged
  from before, structure was already correct.
- **802.11 header, LLC/SNAP** (`dot11p.c`): back to real QoS Data (subtype 8,
  26-byte header with a QoS Control field), matching actual ITS-G5 hardware.
  This required abandoning `esp_wifi_80211_tx()` entirely in favor of
  `esp_wifi_80211_tx_custom()` (`tx_custom.c`) - see "The TX bypass" above for
  the full story and the list of things about it that aren't independently
  verified yet for our exact toolchain.
- **DENM ASN.1 UPER payload** (`denm.c`): verified against the real ASN.1
  modules (DENM-PDU-Descriptions.asn, ITS-Container.asn). This caught real
  bugs too: `ManagementContainer`, `SituationContainer`, and the inner
  `CauseCode` SEQUENCE are all declared with a trailing `...` (extensible),
  each of which needs its own leading extension bit that the previous
  version omitted entirely; `SituationContainer`'s optional-presence bits
  were encoded in the wrong position (at the end instead of the start); and
  three field widths were wrong (latitude is 31 bits not 32, the two
  position-confidence fields and orientation are 12 bits not 16, altitude
  value is 20 bits not 24). All fixed now, with the exact ASN.1 type and
  constraint range cited in comments next to each field.
- **Known-missing, by design, not bugs**: `detectionTime`/`referenceTime`/GN
  timestamp are hardcoded to 0 (no RTC/NTP wired up - will decode as
  2004-01-01), and `latitude`/`longitude` are hardcoded to 0 (no GNSS wired
  up). Both are called out with `TODO` comments in the source.
- **Privacy pseudonym**: the source MAC/GN_ADDR MID is a fixed placeholder,
  not rotated. Fine for bench testing; real stacks rotate this every 5-15 min.
- **Unsecured** (no IEEE 1609.2 signing) - matches "no additional
  parameters"/easiest, and your sniffer already handles unsecured frames fine
  (that's how it decodes RSU SPATEM/MAPEM today).
- **Still a deliberate simplification, not a bug**: single-hop broadcast
  (TSB/SINGLE_HOP) instead of GeoBroadcast. Real DENM dissemination
  typically uses GeoBroadcast so RSUs/OBUs can forward it across an area -
  upgrading to that needs a sequence number + circular geo-area fields in
  the GN extended header that this skeleton doesn't build. Fine for a
  single-vehicle beacon; revisit if you need multi-hop forwarding.

## Build

```powershell
. $env:IDF_PATH\export.ps1        # or use the "ESP-IDF PowerShell" Start Menu shortcut instead
cd C:\Users\Ashin\Documents\micrOBU_workspace\v2x-obu-esp32c5\obu-firmware
idf.py set-target esp32c5
idf.py build
```

### If the linker fails on `phy_11p_set` / `phy_change_channel`

These are undocumented, reverse-engineered symbols pulled straight from
`libphy.a` - not a public API, so exact names/signatures can shift between
ESP-IDF versions. If you get `undefined reference`, check what's actually
exported for your IDF version:

```powershell
riscv32-esp-elf-nm $env:IDF_PATH\components\esp_phy\lib\esp32c5\libphy.a | Select-String -Pattern "11p|change_channel"
```

Adjust the `extern` declarations at the top of `main.c` to match whatever you
find.

## Flash

```powershell
idf.py -p COM5 -b 921600 flash monitor
```

You should see in the log:
```
OCB mode requested @ 5900 MHz - HLN-SV DENM beacon armed, waiting on GPIO4
```
Nothing transmits yet - GPIO4 is pulled up (inactive) until you ground it.

## Wire up the hazard-light input

For bench testing: a jumper wire or push button between GPIO4 and GND is
enough (active-low - grounding it = "hazard lights on" = beacon active).
For the real thing later: tap whatever signal your recovery vehicle's hazard
switch drives (through a level shifter / opto-isolator if it's 12V vehicle
wiring - don't feed vehicle voltage directly into a GPIO).

## Event lifecycle (added after cross-checking the Cohda UCA MQTT schema)

The Cohda "Use Case App" schema (`v2x-uca/output/json/denm`) documents a
`termination` key: "present if the DENM is cancelled or negated." The
firmware now implements this properly:

- `actionID` (station ID + sequence number) is assigned once per event, on
  the rising edge of the hazard-light GPIO, and stays constant across every
  repeat of that same event - it does **not** increment every second like an
  earlier version of this code did.
- On the falling edge (hazard lights go off), exactly one DENM is sent with
  `termination = isCancellation`, referencing that same actionID, then
  transmission stops until the next rising edge.

Expect your sniffer to show the same station+sequence number repeating for
the duration of the event, then one final frame with a termination flag.

## Validate against your own sniffer

This is the important part, since a few of the payload bit widths are
best-effort: with the transmitter running (GPIO4 grounded) and your Phase 1
sniffer board powered on nearby, check the V2X2MAP dashboard.

Expect to see:
- A new station appear, sending DENM (and only DENM - this skeleton doesn't
  send CAM).
- `causeCode` decoding to **stationaryVehicle**, `subCauseCode` **0**.
- `stationType` decoding to **passengerCar**.

If instead you get "unknown message type," garbage station type, or the
dashboard just doesn't show anything: capture a PCAP from the dashboard's
record button and open it in Wireshark - compare byte-by-byte against a real
captured DENM (like the one from your Cohda OBU) to see exactly where the
two diverge. That's a much faster debug loop than staring at the C code.

## Validate against the real RSU

Your own sniffer receiving the frame only tells you the RF/PHY side works -
it doesn't tell you whether a standards-strict stack (Cohda) will accept it.
With the transmitter running and both your sniffer and the Cohda-based RSU
mqtt monitor watching:

- If **both** see it, and it decodes as a real DENM (not "Type ?"): done,
  move on to the "once this round-trips cleanly" list below.
- If your sniffer sees it but doesn't classify it, or the RSU doesn't see it
  at all: worth first ruling out the receiver's own reliability (checked
  separately - the receiver has shown signs of hanging independent of the
  transmitter) before concluding anything about frame format.
- If **neither** sees it: still worth re-checking `esp_wifi_get_channel()`'s
  reported value (logged before every TX) - if it's still stuck regardless of
  the new `esp_wifi_set_channel(140, ...)` call, that's a stronger signal
  the PHY genuinely isn't moving to 5900MHz, independent of frame format.

Note the receiver firmware (V2X2MAP) never faces the frame-type problem this
project spent a while on: receiving never calls `esp_wifi_80211_tx()`, so
there was never a gate on that side to work around. The `phy_11p_set`/
`phy_change_channel` PHY setup in `main.c` already mirrors what the receiver
does for getting onto 5.9GHz OCB mode.

## Once this round-trips cleanly

Next reasonable steps, in rough order: wire in real GNSS (replaces the 0/0
lat-long and the GN timestamp), wire in SNTP or GNSS-derived UTC time
(replaces detectionTime/referenceTime), then decide whether you actually
need GeoBroadcast/multi-hop forwarding instead of SHB, then - only if you
need it - look at IEEE 1609.2 signing.
