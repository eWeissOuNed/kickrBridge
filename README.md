# KICKR Bridge - ESP32-C6

Initial ESP-IDF firmware for a local-network Wahoo KICKR CORE 2 bridge.

The ESP32-C6 joins the same WLAN as the phone/tablet/computer, talks to the trainer over Bluetooth LE FTMS, and serves a small local control/status page at `http://kickr.local`.

## Current scope

Implemented:

- ESP32-C6 target
- ESP-IDF + ESP-NimBLE BLE central
- Scan for FTMS/KICKR trainers
- Connect to the most recently discovered matching trainer
- FTMS service/characteristic discovery
- Indoor Bike Data notifications
- Fitness Machine Control Point indications
- Fitness Machine Feature readout
- Supported Power Range readout
- Supported Resistance Level Range readout
- FTMS Request Control
- FTMS Start / Resume
- FTMS Stop / Pause
- FTMS Set Target Power (ERG)
- FTMS Set Target Resistance (virtual gears)
- low-speed resistance retry behavior
- power, cadence, speed and distance decoding
- Wi-Fi station mode only
- Wi-Fi credentials configured over serial
- credentials stored in NVS
- mDNS `kickr.local`
- HTTP status/control page
- physical buttons
- serial CLI and ESP-IDF logging

This is intentionally an initial development version. It has not yet been compiled/flashed against your exact ESP32-C6 board in this environment, and the BLE implementation should be validated on the real KICKR before relying on it for hard workouts.

## Recommended toolchain

ESP-IDF 5.5 is the reference version.

```bash
idf.py --version
idf.py set-target esp32c6
idf.py build
idf.py flash monitor
```

Exit the serial monitor with `Ctrl+]`.

## First Wi-Fi setup

At the serial console:

```text
kickr> wifi set MySSID MyPassword
Credentials saved.

kickr> wifi connect
```

The firmware stores the credentials in NVS. The password is never printed by the status command.

After DHCP succeeds:

```text
I WIFI: Connected, IP=192.168.1.73
I WIFI: mDNS ready: http://kickr.local
```

From an iPhone/iPad/Mac on the same WLAN open:

```text
http://kickr.local
```

or use the printed IP directly.

## Trainer workflow

For the first trainer test, close other applications that may be controlling the KICKR.

```text
kickr> trainer scan
kickr> trainer connect
kickr> trainer status
```

Expected log sequence is approximately:

```text
FTMS: Found FTMS trainer 'KICKR ...'
FTMS: Connected
FTMS: FTMS service handles ...
FTMS: Subscribed to Indoor Bike Data notifications
FTMS: Subscribed to FTMS Control Point indications
FTMS: FTMS features: ...
FTMS: Power range: 0..2000 W step 1 W
FTMS: FTMS ready; requesting control
FTMS: CP <- request=0x00 result=Success
```

Then test conservatively:

```text
kickr> start
kickr> erg 100
```

For gear/resistance mode:

```text
kickr> gear 3
kickr> gear up
kickr> gear down
```

## Known KICKR behavior

During testing of the Python reference implementation, the real CORE 2 sometimes returned FTMS result `Operation Failed (0x04)` for Set Target Resistance when the flywheel was nearly stationary.

This firmware therefore keeps the requested gear and delays/retries the resistance command until speed reaches approximately 4 km/h.

The threshold is configured in `main/app_config.h`:

```c
#define MIN_RESISTANCE_APPLY_SPEED_KMH 4.0f
```

The initial gear mapping is deliberately simple:

```c
#define GEAR_RESISTANCE_RAW_STEP 50
```

Gear 1 therefore maps to raw FTMS resistance 50, gear 10 to 500. This should be calibrated on the real trainer later.

## Serial commands

```text
help

wifi set <ssid> <password>
wifi connect
wifi disconnect
wifi status
wifi forget

trainer scan
trainer connect
trainer disconnect
trainer status

gear 1
gear up
gear down

erg 100
erg 200

start
stop

log debug
log info
log warn

reboot
```


## Buttons

Default pins are in `main/app_config.h`:

```text
GPIO4  Gear Up
GPIO5  Gear Down
GPIO6  Start/Pause
GPIO7  Mode (reserved)
```

Each switch is connected between GPIO and GND. Internal pull-ups are enabled.

Current behavior:

```text
Gear Up short press       +1 gear
Gear Down short press     -1 gear
Start short press         Start / Pause / Resume
Start long press          Stop
Mode short press          reserved
```

Verify these GPIO assignments against the exact ESP32-C6 development board before wiring hardware.

## HTTP API

Current endpoints:

```text
GET  /api/status
POST /api/gear/up
POST /api/gear/down
POST /api/startpause
```

The built-in page is intentionally minimal. The intended next stage is a mobile-first dark UI with WebSocket/SSE telemetry, power/speed gauges, live chart, ERG and HIIT controls.

## Project structure

```text
.
├── AGENTS.md
├── TODO.md
├── README.md
├── CMakeLists.txt
├── sdkconfig.defaults
└── main
    ├── app_main.c
    ├── app_config.h
    ├── button_manager.c/.h
    ├── console_cli.c/.h
    ├── trainer_manager.c/.h
    ├── web_server.c/.h
    ├── wifi_manager.c/.h
    ├── CMakeLists.txt
    └── idf_component.yml
```

`AGENTS.md` is intended as context for a coding agent continuing the project.

## First things to verify on hardware

See `TODO.md`. In particular:

1. Build against ESP-IDF 5.5
2. Confirm NimBLE scan and connection
3. Confirm FTMS CCCD discovery/subscription
4. Confirm power/cadence/speed parsing
5. Test Request Control
6. Test ERG at a low target such as 100 W
7. Validate gear mapping and low-speed retry behavior

