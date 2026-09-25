# AGENTS.md

## Project goal
Build a small ESP32-C6 bridge for a Wahoo KICKR CORE 2. The ESP connects to the trainer over BLE FTMS and exposes a local web UI/API over the user's normal WLAN. No SoftAP is used during normal operation.

## Toolchain
- ESP-IDF 5.5 is the reference version
- Target: `esp32c6`
- Bluetooth host: ESP-NimBLE
- C, FreeRTOS, esp_http_server, NVS, mDNS

## Core architecture
- `wifi_manager.*`: station-mode Wi-Fi, credentials in NVS, mDNS `kickr.local`
- `trainer_manager.*`: NimBLE central + FTMS telemetry/control
- `button_manager.*`: physical GPIO controls
- `console_cli.*`: serial setup and diagnostics
- `web_server.*`: local web UI and REST endpoints

## FTMS UUIDs
- Fitness Machine Service: `0x1826`
- Fitness Machine Feature: `0x2ACC`
- Indoor Bike Data: `0x2AD2`
- Supported Resistance Level Range: `0x2AD6`
- Supported Power Range: `0x2AD8`
- Fitness Machine Control Point: `0x2AD9`

## FTMS Control Point opcodes used
- Request Control: `0x00`
- Set Target Resistance: `0x04`
- Set Target Power: `0x05`
- Start / Resume: `0x07`
- Stop / Pause: `0x08`

## Known real KICKR CORE 2 observations
A real trainer was observed to report:
- Fitness Machine Feature machine bits: `0x00004003`
- target-setting bits: `0x0000600C`
- Supported Power Range: `0..2000 W`, step `1 W`
- Supported Resistance raw bytes: `00 00 64 00 01 00`

The real trainer sometimes returns FTMS Control Point result `0x04 Operation Failed` for Set Target Resistance when the flywheel is nearly stationary. Once the rider accelerates slightly, resistance commands start succeeding. Keep the selected gear and retry resistance after speed reaches a small threshold instead of treating this as a fatal BLE failure.

## Design rules
- Never print stored Wi-Fi passwords
- Keep BLE callbacks short
- Avoid blocking NimBLE callbacks
- Keep UI/API logic out of `trainer_manager.c`
- `trainer_get_status()` is the shared snapshot API
- All trainer-control entry points should be safe to call from buttons, HTTP, and serial CLI
- Do not remove the low-speed queued-resistance behavior without testing on the real trainer
- Prefer standards-based FTMS behavior before Wahoo-specific protocol extensions

## Current prototype limitations
- Only one FTMS trainer is remembered: the most recently discovered matching device
- Control Point has a simple one-command-at-a-time guard; a proper command queue is a good next improvement
- Gear-to-resistance mapping is intentionally provisional
- HTTP UI is minimal and currently polls `/api/status`
- No workout/HIIT engine is implemented on ESP yet
- No WebSocket streaming yet

## Useful serial workflow
```
wifi set <ssid> <password>
wifi connect
trainer scan
trainer connect
trainer status
start
gear 5
erg 200
```
