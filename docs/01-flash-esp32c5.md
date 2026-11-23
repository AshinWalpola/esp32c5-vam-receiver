# Step 1 - Flash the ESP32-C5

Goal here is *not* your OBU firmware yet. It's flashing a known-good ITS-G5
receiver so you can prove the RF path works on 5.9 GHz before writing any TX
code. Use a second ESP32-C5 board as the "viewer" if you have one; if you only
have the one already running your OBU, you'll need a second board for this
step since one board can't TX and RX at once.

## Option A - Windows one-click installer (fastest)

The V2X2MAP project (fork of opentrafficmap's receiver, adds BLE + a Windows
installer) targets exactly this board.

1. Get a Waveshare ESP32-C5-WROOM-1 dev board (or ESP32-C5-WIFI6-KIT).
2. Download "ITS-G5 Receiver Setup" from
   https://github.com/pit711/V2X2MAP/releases/latest
3. Plug the board in over USB.
4. Run the installer:
   - Select the COM port (auto-detected).
   - Flash (writes bootloader + partition table + app, ~30-60s).
   - It reads the MAC and pre-fills a Node-ID for you.

That's it for this board - no ESP-IDF toolchain needed for this path.

## Option B - Manual build from source (needed later for your own OBU anyway)

You'll need this toolchain for Phase 2 regardless, so setting it up now is not
wasted effort.

```powershell
# 1. Install ESP-IDF v5.5.2 or newer (mass-production ESP32-C5 support
#    landed in 5.5.2; use 5.5.3+/6.x if available)
git clone -b v5.5.3 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
.\install.ps1 esp32c5

# 2. Activate the toolchain (once per shell)
.\export.ps1

# 3. Get the receiver firmware
git clone https://github.com/pit711/V2X2MAP.git
cd V2X2MAP\firmware
idf.py set-target esp32c5
idf.py build
idf.py -p COMx -b 921600 flash monitor
```

Replace `COMx` with whatever port Device Manager shows for the board.

## Notes specific to this board

- No Ethernet module needed for V2X2MAP's fork (unlike the original
  opentrafficmap hardware, which uses SPI Ethernet for direct MQTT from the
  device). V2X2MAP talks over USB-Serial-JTAG or BLE instead, and a PC/phone
  does the MQTT bridging - see `02-mqtt-setup.md`.
- If you instead build the plain `opentrafficmap/its-g5-receiver-firmware`
  (no Ethernet wired up), copy the matching sdkconfig first:
  `cp sdkconfig.proto-w5500 sdkconfig` (or `-enc28j60`) - only relevant if you
  wire up an Ethernet module later for a fixed roadside receiver.
- `idf.py monitor` output is your sanity check: you should see the sniffer
  task start and PHY init logs. If it panics on boot, check flash size in
  `idf.py menuconfig` matches your module (Waveshare boards are commonly
  8/16 MB).

## Sanity check before moving on

Confirm in the serial monitor that the firmware reports it's running on
esp32c5 and has entered sniffer/802.11p mode. If you see normal 2.4 GHz Wi-Fi
station/AP logs instead, you likely built for the wrong target or flashed the
wrong binary.
