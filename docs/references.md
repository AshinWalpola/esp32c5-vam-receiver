# References

## Receiver / sniffer firmware (Phase 1 - what to flash to view V2X now)
- V2X2MAP (fork with Windows installer, BLE, Android app, Python bridge, no Ethernet needed):
  https://github.com/pit711/V2X2MAP
- Original firmware it's forked from (opentrafficmap, needs W5500/ENC28J60 Ethernet for onboard MQTT):
  https://codeberg.org/opentrafficmap/its-g5-receiver-firmware
- Live public map running this: https://opentrafficmap.org

## Background / the register hack
- Your uploaded talk: "C-ITS: Using an ESP32 to track traffic lights, trams
  and cars" - Michael Ehrenreich & Peter Potzi (opentrafficmap), TU Graz.
  Source of the `phy_11p_set(1, 0)` / `phy_change_channel(5900, 1, 0, 0)`
  undocumented libphy.a calls.
- ESP32-C5 product page: https://www.espressif.com/en/products/socs/esp32-c5
- ESP-IDF ESP32-C5 docs: https://docs.espressif.com/projects/esp-idf/en/stable/esp32c5/
- Waveshare ESP32-C5-WIFI6-KIT docs: https://docs.waveshare.com/ESP32-C5-WIFI6-KIT

## Earlier / different approach (not what this project uses)
- esp-vanet (ESP-NOW mesh at 2.4 GHz, mock CAM/DENM, OBD2/CAN input):
  https://github.com/jacksalici/esp-vanet
  Referenced by you as prior art; this project intentionally drops the
  ESP-NOW/OBD approach in favor of real 802.11p on 5.9 GHz.

## Toolchain
- ESP-IDF install docs: https://docs.espressif.com/projects/esp-idf/en/stable/esp32c5/get-started/
- Minimum ESP-IDF for ESP32-C5 mass-production silicon (v1.0): v5.5.2

## Standards (for Phase 3, if you go for standard-conformant messages)
- ETSI EN 302 637-2 (CAM)
- ETSI EN 302 637-3 (DENM)
- ETSI EN 302 636-4-1 (GeoNetworking)
- ETSI EN 302 636-5-1 (BTP)
- IEEE 1609.2 (security/signing)
