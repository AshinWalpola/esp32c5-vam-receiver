# Step 2 - MQTT

Two options. Use your own local broker for development so you're not
depending on, or leaking data to, a public one.

## Option A - Local Mosquitto (recommended for now)

Windows, native install:

1. Install from https://mosquitto.org/download/ (Windows installer).
2. Use the config at `mqtt/mosquitto.conf` in this project (listens on
   1883, no auth - fine for a closed dev network, not for anything exposed).
3. Run it:

```powershell
cd "C:\Program Files\mosquitto"
mosquitto -c "<path to this project>\mqtt\mosquitto.conf" -v
```

`-v` gives verbose logging so you can watch connects/publishes live.

Or via Docker, if you'd rather not install it natively:

```powershell
docker run -it -p 1883:1883 -p 9001:9001 `
  -v "<path to this project>\mqtt\mosquitto.conf:/mosquitto/config/mosquitto.conf" `
  eclipse-mosquitto
```

## Option B - Public test broker (opentrafficmap's)

The V2X2MAP bridge defaults to `mqtts://cits1.opentrafficmap.org:8883`. Fine
for a quick look, but it's someone else's shared infrastructure and your
OBU's DENM output would be visible there too. Use your own broker (Option A)
once you're doing more than a five-minute test.

## Verifying the broker works

With `mosquitto` (the CLI client, comes with the Windows install) in two
terminals:

```powershell
# terminal 1 - subscribe to everything
mosquitto_sub -h localhost -t "#" -v

# terminal 2 - publish a test message
mosquitto_pub -h localhost -t "test/hello" -m "hi"
```

You should see `test/hello hi` show up in terminal 1. If that works, the
broker's ready for the bridge in `03-view-v2x-denm.md`.

## Topic layout (once the bridge is running)

The Python bridge in V2X2MAP publishes decoded frames under topics keyed by
message type and node id, roughly:

```
itsg5/<node-id>/cam
itsg5/<node-id>/denm
itsg5/<node-id>/raw
```

Confirm exact topic names against `bridge/its_g5_bridge.py` once you have the
source checked out - the bridge is the source of truth, not this doc.
