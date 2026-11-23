# Sniffer / viewer - Phase 1

Not code you write - this folder is just a landing spot. Actual firmware
lives in whichever repo you clone per `docs/01-flash-esp32c5.md`:

```powershell
git clone https://github.com/pit711/V2X2MAP.git
```

Clone it either here (`sniffer-viewer\V2X2MAP\`) or wherever's convenient -
just keep the path handy for the flash/MQTT/view docs, which assume you know
where it landed.

Once `docs/01-flash-esp32c5.md` through `03-view-v2x-denm.md` are done and
you've confirmed you can see real DENM traffic, this board and this folder
have done their job - move on to `obu-firmware/` for the transmit side.
