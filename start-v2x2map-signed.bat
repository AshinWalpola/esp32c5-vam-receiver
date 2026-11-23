@echo off
rem V2X2MAP bridge from source (microbu-esp32c5 tools/v2x2map-0.3.0), in place of its-g5-bridge.exe.
rem Same serial protocol, COM port, node id and MQTT broker as the exe (config/v2x2map.cfg next to
rem the bridge script). Differences: it unwraps signed (TS 103 097) packets, so signed CAMs and VAMs
rem are decoded, and --trust checks their signatures against the MicrOBU demo chain; the dashboard
rem then says "signature verified", "SIGNATURE INVALID" or "not verified".
rem Dashboard: http://127.0.0.1:8080  -  recordings: bridge\recordings, or pass a path to /api/record.
cd /d "%USERPROFILE%\AndroidStudioProjects\MicrOBU\microbu-esp32c5\tools\v2x2map-0.3.0\bridge"
py -3.11 its_g5_bridge.py --port COM4 --dashboard-port 8080 --open-browser ^
    --trust "%USERPROFILE%\AndroidStudioProjects\MicrOBU\app\src\main\assets\demo-chain.vcr"
pause
