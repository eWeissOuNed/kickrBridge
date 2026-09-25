# TODO

## First hardware validation
- Build with ESP-IDF 5.5 and ESP32-C6 target
- Verify NimBLE initialization and scan
- Confirm KICKR advertisement contains FTMS UUID or KICKR name
- Connect and verify service/characteristic discovery
- Verify CCCD discovery and subscription
- Verify Indoor Bike Data parser against Python reference behavior
- Verify Request Control response
- Test Start/Resume and Stop/Pause
- Test ERG at 100 W first
- Test gear/resistance while moving and while nearly stopped

## Near-term firmware improvements
- Replace simple Control Point busy flag with a FreeRTOS command queue
- Auto-reconnect to trainer after disconnect
- Persist preferred trainer identity/address
- Better scan list instead of only most recent FTMS device
- Validate resistance-unit interpretation on CORE 2
- Calibrate gear mapping
- Add Fitness Machine Status subscription
- Add heart-rate handling if exposed through trainer data path

## Web UI
- WebSocket/SSE live telemetry
- Modern dark dashboard
- Power and speed gauges
- Live power chart
- Gear controls
- ERG target entry
- HIIT workout screen
- Mobile/iPad responsive layout

## Workouts
- HIIT state machine on ESP
- Configurable warm-up/work/recovery/cool-down
- Countdown before transitions
- Persist workout presets
- Generic interval-plan table later

## Hardware
- Finalize safe ESP32-C6 GPIO assignment for selected board
- Add physical gear up/down buttons
- Add start/pause button
- Optional mode button or rotary encoder
- Status RGB LED
- Later custom USB-C powered PCB
