# OBU transmit firmware - Phase 2 (in progress: HLN-SV DENM beacon)

Started. See `docs/04-transmit-setup.md` in the project root for build/flash
steps and how to validate this against your own sniffer.

Implements one profile so far: **HLN-SV** (aftermarket stationary recovery
vehicle), causeCode 94 (stationaryVehicle), subCauseCode 0, active while the
hazard-light GPIO is grounded. No location/alacarte containers.

- `main/main.c` - entry point, the `phy_11p_set`/`phy_change_channel(5900,...)`
  register hack, GPIO polling, TX loop
- `main/denm.c` / `.h` - ASN.1 UPER encoding of a minimal DENM
- `main/geonet.c` / `.h` - GeoNetworking Basic/Common/SHB headers + BTP-B
- `main/dot11p.c` / `.h` - 802.11 OCB (QoS Data, broadcast) frame + LLC/SNAP

Known gaps, tracked as TODOs in the source: no real GNSS (lat/long hardcoded
0), no real time source (detectionTime/referenceTime hardcoded 0, decodes as
2004-01-01), fixed (non-rotating) pseudonym MAC, SHB instead of GeoBroadcast
(no multi-hop forwarding), unsecured (no IEEE 1609.2 signing).
