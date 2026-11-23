# esp32c5-vam-receiver

An ITS-G5 (802.11p, 5.9 GHz) receiver and CAM/DENM transmitter scaffold on the
ESP32-C5, extended to **receive and decode VAMs** (Vulnerable Road User
Awareness Messages), including signed (ETSI TS 103 097) packets.

## Origins

- **Initial idea: OpenTrafficMap.** The approach of turning a cheap ESP32-C5 into a
  5.9 GHz V2X radio, and the receiver firmware and MQTT map pipeline, come from the
  [OpenTrafficMap](https://opentrafficmap.org) project (Michael Ehrenreich and
  Peter Potzi): the talk "C-ITS: Using an ESP32 to track traffic lights, trams and
  cars" and the
  [its-g5-receiver-firmware](https://codeberg.org/opentrafficmap/its-g5-receiver-firmware).
  The `phy_11p_set` / `phy_change_channel` OCB mode used here is documented there.
- **V2X2MAP fork** by [pit711](https://github.com/pit711/V2X2MAP): Windows
  installer, BLE, Android app and the Python bridge that this repo's receiver
  setup builds on.
- **This repo's extension: VAM reception.** The V2X2MAP bridge is run from source
  (`start-v2x2map-signed.bat`) so that signed packets are unwrapped and
  **signed CAMs and VAMs are decoded** (bike and pedestrian awareness messages
  from the MicrOBU phone app). With `--trust` the signatures are checked against a
  demo certificate chain, and the dashboard shows "signature verified",
  "SIGNATURE INVALID" or "not verified". Decoded messages go to the same MQTT
  broker as before.

The bridge source and the demo chain live in the MicrOBU app project and are not
part of this repo. Adjust the paths in `start-v2x2map-signed.bat` to your checkout.

## Contents

| Path | What |
|---|---|
| `docs/` | Step-by-step guides: flash the ESP32-C5, set up MQTT, view DENM/CAM/VAM, transmit setup, references |
| `obu-cam-transmistter/` | ESP-IDF transmitter: CAM/DENM encoding, GeoNetworking, raw 802.11p frame injection |
| `mqtt/` | Local Mosquitto config |
| `sniffer-viewer/` | Notes on the receiver firmware to clone |
| `config/v2x2map.cfg.example` | Bridge config (copy to `v2x2map.cfg`, set your node id and COM port) |
| `start-v2x2map-signed.bat` | Start the V2X2MAP bridge with signed-message decoding |

Recordings (pcaps), build output and prebuilt binaries are deliberately left out.
Get the receiver firmware and installer from the upstream projects above.

## Hardware

Waveshare ESP32-C5-WROOM-1 dev board (or the ESP32-C5-WIFI6-KIT). Dual-band
Wi-Fi 6 (2.4/5 GHz) RISC-V SoC from Espressif - the only current ESP32 that
can reach the 5.9 GHz ITS-G5 band at all.

## The "register hack"

Documented in the C-ITS ESP32 talk (Ehrenreich/Potzi, opentrafficmap)
The relevant undocumented `libphy.a` calls, found by
reverse engineering the (Apache-2.0-licensed) blobs:

```c
phy_11p_set(1, 0);
phy_change_channel(5900, 1, 0, 0);
```

This pushes the radio into 802.11p OCB (Outside Context of a BSS) mode on the
5900 MHz ITS-G5 control channel, outside normal Wi-Fi channel/regulatory
tables. This is what lets a $10-20 WiFi SoC act as a real V2X radio. Same
mechanism the opentrafficmap receiver and the V2X2MAP fork use.

## Legal note

Transmitting outside the allocated Wi-Fi bands (i.e. actually building the OBU
TX side in Phase 2) is not just "undocumented API," it's operating radio
equipment outside its certified/licensed parameters. Rules vary by country;
in the EU the 5.9 GHz ITS-G5 band has its own licensing conditions. Receiving/
sniffing (Phase 1) is comparatively low-risk. Worth checking your local
telecom regulator before radiating on 5900 MHz. Not legal advice, just a flag.
