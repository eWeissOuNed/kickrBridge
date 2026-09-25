#pragma once

// Change these pins to suit your ESP32-C6 board.
// Defaults are deliberately conservative placeholders; check your board schematic.
#define BTN_GEAR_UP_GPIO       4
#define BTN_GEAR_DOWN_GPIO     5
#define BTN_START_PAUSE_GPIO   6
#define BTN_MODE_GPIO          7

#define BUTTON_ACTIVE_LEVEL    0
#define BUTTON_DEBOUNCE_MS     35
#define BUTTON_LONG_PRESS_MS   900
#define BUTTON_POLL_MS         10

#define KICKR_MDNS_HOSTNAME    "kickr"
#define KICKR_MDNS_INSTANCE    "KICKR Trainer Bridge"
#define KICKR_HTTP_PORT        80

#define DEFAULT_GEAR           5
#define MIN_GEAR               1
#define MAX_GEAR               10

#define BLE_SCAN_SECONDS                 5
#define MIN_RESISTANCE_APPLY_SPEED_KMH   4.0f
#define GEAR_RESISTANCE_RAW_STEP         50
