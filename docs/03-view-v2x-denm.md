# Step 3 - See your OBU's DENM

## Run the bridge

```powershell
cd V2X2MAP\bridge
python -m venv venv
.\venv\Scripts\activate
pip install -r requirements.txt   # check the folder for the actual filename
python its_g5_bridge.py --port COMx --node-id <mac-without-colons> --mqtt-host localhost
```

`COMx` = the port your flashed receiver board shows up on. `--node-id` is
just a label, the installer path auto-fills it from the chip's MAC; doing it
manually you can pick anything memorable, e.g. `sniffer01`.

Dashboard: http://127.0.0.1:8080

## What to expect

Your OBU is a Consider IT unit running the Cohda Wireless stack - a real,
standards-conformant ETSI ITS-G5 implementation (Cohda MK5-class hardware).
That's good news here: no ambiguity about band or framing like there would be
with a mock ESP-NOW setup. It transmits genuine 802.11p OCB frames, almost
certainly on the ITS-G5 control channel (5900 MHz / channel 180), the same
frequency as the `phy_change_channel(5900, ...)` call in the README. The
sniffer should pick it up with zero Cohda-specific configuration - it's just
listening on that channel and decoding standard GeoNetworking/BTP/ITS
headers, which is all Cohda's stack produces.

Expect the dashboard to show a DENM marker/log entry within a few seconds of
the OBU sending one, with RSSI and station ID attached.

If you see nothing:

- **Check the channel.** Cohda units can be configured for other ITS-G5
  channels (service channels 174-184, not just the 180 CCH) depending on
  how Consider IT set it up. If your box has a config UI/CLI, confirm which
  channel it's actually transmitting on and match the sniffer to it.
- **Signed messages.** Cohda's stack normally signs CAM/DENM per IEEE 1609.2 /
  ETSI TS 103097 certificates. The message content itself isn't encrypted, so
  decoding CAM/DENM fields should work fine - you just won't be doing
  certificate/signature validation, which you don't need for viewing traffic.
- **Distance/antenna.** The bare WROOM-1 antenna is short range at 5.9 GHz
  (tens of meters is fine for desk testing); the opentrafficmap team reports
  hundreds of meters to km-range with their PCB antenna outdoors. Not an
  issue for a bench test with both boards a meter apart.

## Alternative: raw PCAP in Wireshark

The Android app and some bridge builds support one-tap PCAP recording
(link type 105 = IEEE 802.11), openable directly in Wireshark if you want to
inspect the raw 802.11 frame + GeoNetworking + BTP + ASN.1 payload rather
than the dashboard's decoded view. Useful once you're validating your own
OBU's frame format in Phase 2.

## Once this works

You've now confirmed: (1) the ESP32-C5 can actually receive on 5.9 GHz, (2)
your MQTT pipeline works end to end, (3) your existing OBU is really
radiating ITS-G5 frames. That's the foundation Phase 2 (writing your own
OBU TX firmware in `obu-firmware/`) builds on.
