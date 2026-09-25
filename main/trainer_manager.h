#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool ble_ready;
    bool device_found;
    bool connected;
    bool ftms_ready;
    bool control_granted;
    int8_t rssi;
    char device_name[32];

    int gear;
    bool running;
    bool paused;

    int power_w;
    int cadence_rpm;
    float speed_kmh;
    float distance_km;
    int resistance_raw;

    int target_power_w;
    int requested_resistance_raw;
    bool resistance_pending;

    int supported_power_min_w;
    int supported_power_max_w;
    int supported_power_step_w;
    int supported_resistance_min_raw;
    int supported_resistance_max_raw;
    int supported_resistance_step_raw;
} trainer_status_t;

void trainer_manager_init(void);
void trainer_manager_scan(void);
void trainer_manager_connect(void);
void trainer_manager_disconnect(void);

void trainer_gear_up(void);
void trainer_gear_down(void);
void trainer_set_gear(int gear);

void trainer_toggle_start_pause(void);
void trainer_stop(void);
void trainer_set_erg_power(int watts);
void trainer_request_control(void);

trainer_status_t trainer_get_status(void);
