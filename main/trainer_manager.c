#include "trainer_manager.h"
#include "app_config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "os/os_mbuf.h"

static const char *TAG = "FTMS";

#define UUID_FTMS_SERVICE             0x1826
#define UUID_FITNESS_MACHINE_FEATURE  0x2ACC
#define UUID_INDOOR_BIKE_DATA         0x2AD2
#define UUID_SUPPORTED_RESISTANCE     0x2AD6
#define UUID_SUPPORTED_POWER          0x2AD8
#define UUID_FTMS_CONTROL_POINT       0x2AD9
#define UUID_CCCD                     0x2902

#define FTMS_CP_REQUEST_CONTROL       0x00
#define FTMS_CP_SET_RESISTANCE        0x04
#define FTMS_CP_SET_TARGET_POWER      0x05
#define FTMS_CP_START_RESUME          0x07
#define FTMS_CP_STOP_PAUSE            0x08
#define FTMS_CP_RESPONSE_CODE         0x80

#define FTMS_RESULT_SUCCESS           0x01

#define INVALID_HANDLE                0xffff

static trainer_status_t s_status = {
    .gear = DEFAULT_GEAR,
    .supported_power_max_w = 2000,
};
static SemaphoreHandle_t s_lock;

static uint8_t s_own_addr_type;
static ble_addr_t s_peer_addr;
static bool s_have_peer;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_ftms_start = INVALID_HANDLE;
static uint16_t s_ftms_end = INVALID_HANDLE;
static uint16_t s_indoor_data_handle = INVALID_HANDLE;
static uint16_t s_control_point_handle = INVALID_HANDLE;
static uint16_t s_feature_handle = INVALID_HANDLE;
static uint16_t s_power_range_handle = INVALID_HANDLE;
static uint16_t s_resistance_range_handle = INVALID_HANDLE;
static uint16_t s_indoor_cccd = INVALID_HANDLE;
static uint16_t s_control_cccd = INVALID_HANDLE;
static bool s_scanning;
static bool s_control_busy;
static uint8_t s_pending_opcode;

static int gap_event(struct ble_gap_event *event, void *arg);
static void start_scan_internal(void);
static void begin_ftms_discovery(void);
static void subscribe_characteristics(void);
static void read_capabilities(void);

static void lock(void) { if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { if (s_lock) xSemaphoreGive(s_lock); }

static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static int16_t sle16(const uint8_t *p) { return (int16_t)le16(p); }
static uint32_t le24(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16); }

static const char *result_name(uint8_t code) {
    switch (code) {
        case 0x01: return "Success";
        case 0x02: return "Opcode not supported";
        case 0x03: return "Invalid parameter";
        case 0x04: return "Operation failed";
        case 0x05: return "Control not permitted";
        default: return "Unknown";
    }
}

static bool advertised_ftms(const struct ble_hs_adv_fields *f) {
    for (int i = 0; i < f->num_uuids16; ++i) {
        if (ble_uuid_u16(&f->uuids16[i].u) == UUID_FTMS_SERVICE) return true;
    }
    return false;
}

static void copy_name(const struct ble_hs_adv_fields *f, char *out, size_t n) {
    if (!out || n == 0) return;
    out[0] = 0;
    if (f->name && f->name_len) {
        size_t len = f->name_len < n - 1 ? f->name_len : n - 1;
        memcpy(out, f->name, len);
        out[len] = 0;
    }
}

static bool looks_like_kickr(const struct ble_hs_adv_fields *f) {
    if (advertised_ftms(f)) return true;
    char name[40];
    copy_name(f, name, sizeof(name));
    return strstr(name, "KICKR") != NULL || strstr(name, "Wahoo") != NULL;
}

static int write_done(uint16_t conn_handle, const struct ble_gatt_error *error,
                      struct ble_gatt_attr *attr, void *arg) {
    if (error->status != 0) {
        ESP_LOGW(TAG, "GATT write failed status=%d handle=%u", error->status,
                 attr ? attr->handle : 0);
        s_control_busy = false;
    }
    return 0;
}

static bool send_cp(const uint8_t *data, uint16_t len) {
    if (!data || !len) return false;
    if (!s_status.ftms_ready || s_control_point_handle == INVALID_HANDLE ||
        s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW(TAG, "Control Point not ready");
        return false;
    }
    if (s_control_busy) {
        ESP_LOGW(TAG, "Control Point busy, dropping opcode 0x%02X", data[0]);
        return false;
    }

    ESP_LOGD(TAG, "CP -> opcode=0x%02X len=%u", data[0], len);
    int rc = ble_gattc_write_flat(s_conn_handle, s_control_point_handle,
                                  data, len, write_done, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "Control Point write start failed rc=%d", rc);
        return false;
    }
    s_control_busy = true;
    s_pending_opcode = data[0];
    return true;
}

void trainer_request_control(void) {
    const uint8_t cmd[] = {FTMS_CP_REQUEST_CONTROL};
    send_cp(cmd, sizeof(cmd));
}

static bool send_resistance_raw(int raw) {
    if (raw < 0) raw = 0;
    if (raw > 1000) raw = 1000;
    int16_t v = (int16_t)raw;
    uint8_t cmd[] = {FTMS_CP_SET_RESISTANCE, (uint8_t)(v & 0xff), (uint8_t)((v >> 8) & 0xff)};
    bool ok = send_cp(cmd, sizeof(cmd));
    if (ok) {
        lock();
        s_status.requested_resistance_raw = raw;
        unlock();
    }
    return ok;
}

static int gear_to_resistance_raw(int gear) {
    /* 1..10 => 5.0..50.0 FTMS resistance units (0.1 resolution).
       This is deliberately configurable and can be calibrated per trainer later. */
    return gear * GEAR_RESISTANCE_RAW_STEP;
}

static void maybe_apply_pending_resistance(void) {
    int raw = 0;
    bool pending = false;
    float speed = 0;
    lock();
    pending = s_status.resistance_pending;
    raw = s_status.requested_resistance_raw;
    speed = s_status.speed_kmh;
    unlock();

    if (pending && speed >= MIN_RESISTANCE_APPLY_SPEED_KMH && !s_control_busy) {
        ESP_LOGI(TAG, "Flywheel moving (%.1f km/h), applying queued resistance raw=%d", speed, raw);
        if (send_resistance_raw(raw)) {
            lock(); s_status.resistance_pending = false; unlock();
        }
    }
}

static void parse_indoor_bike_data(const uint8_t *d, uint16_t len) {
    if (!d || len < 2) return;
    const uint16_t flags = le16(d);
    uint16_t p = 2;

    lock();

    /* Bit 0 is 'More Data': instantaneous speed is present when bit 0 == 0. */
    if (!(flags & (1u << 0)) && p + 2 <= len) {
        s_status.speed_kmh = le16(&d[p]) / 100.0f;
        p += 2;
    }
    if ((flags & (1u << 1)) && p + 2 <= len) p += 2; /* average speed */
    if ((flags & (1u << 2)) && p + 2 <= len) {
        s_status.cadence_rpm = (int)(le16(&d[p]) / 2.0f + 0.5f);
        p += 2;
    }
    if ((flags & (1u << 3)) && p + 2 <= len) p += 2; /* average cadence */
    if ((flags & (1u << 4)) && p + 3 <= len) {
        s_status.distance_km = le24(&d[p]) / 1000.0f;
        p += 3;
    }
    if ((flags & (1u << 5)) && p + 2 <= len) {
        s_status.resistance_raw = sle16(&d[p]);
        p += 2;
    }
    if ((flags & (1u << 6)) && p + 2 <= len) {
        s_status.power_w = sle16(&d[p]);
        p += 2;
    }
    if ((flags & (1u << 7)) && p + 2 <= len) p += 2; /* average power */

    float speed = s_status.speed_kmh;
    int power = s_status.power_w;
    int cadence = s_status.cadence_rpm;
    unlock();

    ESP_LOGD(TAG, "bike: %.1f km/h, %d rpm, %d W flags=0x%04X", speed, cadence, power, flags);
    maybe_apply_pending_resistance();
}

static void parse_control_response(const uint8_t *d, uint16_t len) {
    if (!d || len < 3 || d[0] != FTMS_CP_RESPONSE_CODE) return;
    const uint8_t request_opcode = d[1];
    const uint8_t result = d[2];
    s_control_busy = false;

    ESP_LOGI(TAG, "CP <- request=0x%02X result=%s (0x%02X)",
             request_opcode, result_name(result), result);

    if (request_opcode == FTMS_CP_REQUEST_CONTROL && result == FTMS_RESULT_SUCCESS) {
        lock(); s_status.control_granted = true; unlock();
    }

    if (request_opcode == FTMS_CP_SET_RESISTANCE && result == 0x04) {
        float speed;
        lock();
        speed = s_status.speed_kmh;
        if (speed < MIN_RESISTANCE_APPLY_SPEED_KMH) s_status.resistance_pending = true;
        unlock();
        if (speed < MIN_RESISTANCE_APPLY_SPEED_KMH) {
            ESP_LOGW(TAG, "Resistance rejected at %.1f km/h; queued until >= %.1f km/h",
                     speed, MIN_RESISTANCE_APPLY_SPEED_KMH);
        }
    }
}

static void capability_reads_done(void) {
    lock(); s_status.ftms_ready = true; unlock();
    ESP_LOGI(TAG, "FTMS ready; requesting control");
    trainer_request_control();
}

static void read_next_after_feature(void);
static void read_next_after_power(void);

static int read_feature_cb(uint16_t conn, const struct ble_gatt_error *err,
                           struct ble_gatt_attr *attr, void *arg) {
    if (err->status == 0 && attr && OS_MBUF_PKTLEN(attr->om) >= 8) {
        uint8_t b[8]; os_mbuf_copydata(attr->om, 0, 8, b);
        uint32_t machine = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
        uint32_t target  = (uint32_t)b[4] | ((uint32_t)b[5] << 8) | ((uint32_t)b[6] << 16) | ((uint32_t)b[7] << 24);
        ESP_LOGI(TAG, "FTMS features: machine=0x%08lX target=0x%08lX", (unsigned long)machine, (unsigned long)target);
    } else if (err->status != 0) {
        ESP_LOGW(TAG, "Feature read failed status=%d", err->status);
    }
    read_next_after_feature();
    return 0;
}

static int read_power_cb(uint16_t conn, const struct ble_gatt_error *err,
                         struct ble_gatt_attr *attr, void *arg) {
    if (err->status == 0 && attr && OS_MBUF_PKTLEN(attr->om) >= 6) {
        uint8_t b[6]; os_mbuf_copydata(attr->om, 0, 6, b);
        lock();
        s_status.supported_power_min_w = sle16(&b[0]);
        s_status.supported_power_max_w = sle16(&b[2]);
        s_status.supported_power_step_w = le16(&b[4]);
        trainer_status_t st = s_status;
        unlock();
        ESP_LOGI(TAG, "Power range: %d..%d W step %d W",
                 st.supported_power_min_w, st.supported_power_max_w, st.supported_power_step_w);
    } else if (err->status != 0) {
        ESP_LOGW(TAG, "Power range read failed status=%d", err->status);
    }
    read_next_after_power();
    return 0;
}

static int read_resistance_cb(uint16_t conn, const struct ble_gatt_error *err,
                              struct ble_gatt_attr *attr, void *arg) {
    if (err->status == 0 && attr && OS_MBUF_PKTLEN(attr->om) >= 6) {
        uint8_t b[6]; os_mbuf_copydata(attr->om, 0, 6, b);
        lock();
        s_status.supported_resistance_min_raw = sle16(&b[0]);
        s_status.supported_resistance_max_raw = sle16(&b[2]);
        s_status.supported_resistance_step_raw = le16(&b[4]);
        trainer_status_t st = s_status;
        unlock();
        ESP_LOGI(TAG, "Resistance range raw: %d..%d step %d",
                 st.supported_resistance_min_raw, st.supported_resistance_max_raw,
                 st.supported_resistance_step_raw);
    } else if (err->status != 0) {
        ESP_LOGW(TAG, "Resistance range read failed status=%d", err->status);
    }
    capability_reads_done();
    return 0;
}

static void read_next_after_feature(void) {
    if (s_power_range_handle != INVALID_HANDLE) {
        int rc = ble_gattc_read(s_conn_handle, s_power_range_handle, read_power_cb, NULL);
        if (rc == 0) return;
        ESP_LOGW(TAG, "Power range read failed to start rc=%d", rc);
    }
    read_next_after_power();
}

static void read_next_after_power(void) {
    if (s_resistance_range_handle != INVALID_HANDLE) {
        int rc = ble_gattc_read(s_conn_handle, s_resistance_range_handle, read_resistance_cb, NULL);
        if (rc == 0) return;
        ESP_LOGW(TAG, "Resistance range read failed to start rc=%d", rc);
    }
    capability_reads_done();
}

static void read_capabilities(void) {
    if (s_feature_handle != INVALID_HANDLE) {
        int rc = ble_gattc_read(s_conn_handle, s_feature_handle, read_feature_cb, NULL);
        if (rc == 0) return;
        ESP_LOGW(TAG, "Feature read failed to start rc=%d", rc);
    }
    read_next_after_feature();
}

static void subscribe_control_point(void);

static int subscribe_control_cb(uint16_t conn, const struct ble_gatt_error *err,
                                struct ble_gatt_attr *attr, void *arg) {
    if (err->status == 0) {
        ESP_LOGI(TAG, "Subscribed to FTMS Control Point indications");
        read_capabilities();
    } else {
        ESP_LOGE(TAG, "Control Point subscription failed status=%d", err->status);
    }
    return 0;
}

static int subscribe_indoor_cb(uint16_t conn, const struct ble_gatt_error *err,
                               struct ble_gatt_attr *attr, void *arg) {
    if (err->status == 0) {
        ESP_LOGI(TAG, "Subscribed to Indoor Bike Data notifications");
        subscribe_control_point();
    } else {
        ESP_LOGE(TAG, "Indoor Bike Data subscription failed status=%d", err->status);
    }
    return 0;
}

static void subscribe_control_point(void) {
    if (s_control_cccd == INVALID_HANDLE) return;
    const uint8_t cccd[] = {0x02, 0x00};
    int rc = ble_gattc_write_flat(s_conn_handle, s_control_cccd, cccd, sizeof(cccd), subscribe_control_cb, NULL);
    if (rc != 0) ESP_LOGE(TAG, "Control Point CCCD write failed to start rc=%d", rc);
}

static void subscribe_characteristics(void) {
    if (s_indoor_cccd == INVALID_HANDLE || s_control_cccd == INVALID_HANDLE) return;
    const uint8_t cccd[] = {0x01, 0x00};
    int rc = ble_gattc_write_flat(s_conn_handle, s_indoor_cccd, cccd, sizeof(cccd), subscribe_indoor_cb, NULL);
    if (rc != 0) ESP_LOGE(TAG, "Indoor Bike Data CCCD write failed to start rc=%d", rc);
}

static int dsc_cb(uint16_t conn, const struct ble_gatt_error *err,
                  uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg) {
    if (err->status == 0 && dsc) {
        if (ble_uuid_u16(&dsc->uuid.u) == UUID_CCCD) {
            if (chr_val_handle == s_indoor_data_handle) s_indoor_cccd = dsc->handle;
            else if (chr_val_handle == s_control_point_handle) s_control_cccd = dsc->handle;
        }
        return 0;
    }
    if (err->status == BLE_HS_EDONE) {
        if (s_indoor_cccd != INVALID_HANDLE && s_control_cccd != INVALID_HANDLE) {
            ESP_LOGI(TAG, "FTMS descriptor discovery complete");
            subscribe_characteristics();
        } else {
            ESP_LOGW(TAG, "Missing CCCD: indoor=%u control=%u", s_indoor_cccd, s_control_cccd);
        }
    }
    return 0;
}

static int chr_cb(uint16_t conn, const struct ble_gatt_error *err,
                  const struct ble_gatt_chr *chr, void *arg) {
    if (err->status == 0 && chr) {
        uint16_t uuid = ble_uuid_u16(&chr->uuid.u);
        switch (uuid) {
            case UUID_INDOOR_BIKE_DATA: s_indoor_data_handle = chr->val_handle; break;
            case UUID_FTMS_CONTROL_POINT: s_control_point_handle = chr->val_handle; break;
            case UUID_FITNESS_MACHINE_FEATURE: s_feature_handle = chr->val_handle; break;
            case UUID_SUPPORTED_POWER: s_power_range_handle = chr->val_handle; break;
            case UUID_SUPPORTED_RESISTANCE: s_resistance_range_handle = chr->val_handle; break;
            default: break;
        }
        ESP_LOGD(TAG, "FTMS characteristic uuid=0x%04X def=%u val=%u", uuid, chr->def_handle, chr->val_handle);
        return 0;
    }

    if (err->status == BLE_HS_EDONE) {
        ESP_LOGI(TAG, "FTMS chars: bike=%u control=%u feature=%u powerRange=%u resistanceRange=%u",
                 s_indoor_data_handle, s_control_point_handle, s_feature_handle,
                 s_power_range_handle, s_resistance_range_handle);
        if (s_indoor_data_handle == INVALID_HANDLE || s_control_point_handle == INVALID_HANDLE) {
            ESP_LOGE(TAG, "Trainer lacks required FTMS characteristics");
            return 0;
        }
        int rc = ble_gattc_disc_all_dscs(s_conn_handle, s_ftms_start, s_ftms_end, dsc_cb, NULL);
        if (rc != 0) ESP_LOGE(TAG, "Descriptor discovery failed to start rc=%d", rc);
    }
    return 0;
}

static int svc_cb(uint16_t conn, const struct ble_gatt_error *err,
                  const struct ble_gatt_svc *svc, void *arg) {
    if (err->status == 0 && svc) {
        s_ftms_start = svc->start_handle;
        s_ftms_end = svc->end_handle;
        ESP_LOGI(TAG, "FTMS service handles %u..%u", s_ftms_start, s_ftms_end);
        return 0;
    }
    if (err->status == BLE_HS_EDONE) {
        if (s_ftms_start == INVALID_HANDLE) {
            ESP_LOGE(TAG, "Fitness Machine Service not found");
            return 0;
        }
        int rc = ble_gattc_disc_all_chrs(s_conn_handle, s_ftms_start, s_ftms_end, chr_cb, NULL);
        if (rc != 0) ESP_LOGE(TAG, "Characteristic discovery failed to start rc=%d", rc);
    }
    return 0;
}

static void begin_ftms_discovery(void) {
    s_ftms_start = s_ftms_end = INVALID_HANDLE;
    s_indoor_data_handle = s_control_point_handle = INVALID_HANDLE;
    s_feature_handle = s_power_range_handle = s_resistance_range_handle = INVALID_HANDLE;
    s_indoor_cccd = s_control_cccd = INVALID_HANDLE;
    lock();
    s_status.ftms_ready = false;
    s_status.control_granted = false;
    unlock();

    int rc = ble_gattc_disc_svc_by_uuid(s_conn_handle, BLE_UUID16_DECLARE(UUID_FTMS_SERVICE), svc_cb, NULL);
    if (rc != 0) ESP_LOGE(TAG, "FTMS service discovery failed to start rc=%d", rc);
}

static void connect_selected(void) {
    if (!s_have_peer) {
        ESP_LOGW(TAG, "No trainer selected. Run 'trainer scan' first.");
        return;
    }
    if (s_scanning) {
        ble_gap_disc_cancel();
        s_scanning = false;
    }
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGI(TAG, "Trainer already connected");
        return;
    }
    ESP_LOGI(TAG, "Connecting to selected trainer ...");
    int rc = ble_gap_connect(s_own_addr_type, &s_peer_addr, 10000, NULL, gap_event, NULL);
    if (rc != 0) ESP_LOGE(TAG, "ble_gap_connect failed rc=%d", rc);
}

static int gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_DISC: {
            struct ble_hs_adv_fields f;
            if (ble_hs_adv_parse_fields(&f, event->disc.data, event->disc.length_data) != 0) return 0;
            if (!looks_like_kickr(&f)) return 0;

            char name[32]; copy_name(&f, name, sizeof(name));
            ESP_LOGI(TAG, "Found FTMS trainer '%s' RSSI=%d", name[0] ? name : "<unnamed>", event->disc.rssi);
            s_peer_addr = event->disc.addr;
            s_have_peer = true;
            lock();
            s_status.device_found = true;
            s_status.rssi = event->disc.rssi;
            snprintf(s_status.device_name, sizeof(s_status.device_name), "%s", name[0] ? name : "FTMS trainer");
            unlock();
            return 0;
        }
        case BLE_GAP_EVENT_DISC_COMPLETE:
            s_scanning = false;
            ESP_LOGI(TAG, "BLE scan complete");
            return 0;

        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status != 0) {
                ESP_LOGE(TAG, "Connection failed status=%d", event->connect.status);
                s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
                return 0;
            }
            s_conn_handle = event->connect.conn_handle;
            lock(); s_status.connected = true; unlock();
            ESP_LOGI(TAG, "Connected conn_handle=%u", s_conn_handle);
            begin_ftms_discovery();
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGW(TAG, "Disconnected reason=%d", event->disconnect.reason);
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            s_control_busy = false;
            lock();
            s_status.connected = false;
            s_status.ftms_ready = false;
            s_status.control_granted = false;
            s_status.running = false;
            s_status.paused = false;
            unlock();
            return 0;

        case BLE_GAP_EVENT_NOTIFY_RX: {
            uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
            uint8_t buf[64];
            if (len > sizeof(buf)) len = sizeof(buf);
            os_mbuf_copydata(event->notify_rx.om, 0, len, buf);
            if (event->notify_rx.attr_handle == s_indoor_data_handle) parse_indoor_bike_data(buf, len);
            else if (event->notify_rx.attr_handle == s_control_point_handle) parse_control_response(buf, len);
            return 0;
        }

        case BLE_GAP_EVENT_MTU:
            ESP_LOGD(TAG, "MTU=%d", event->mtu.value);
            return 0;

        default:
            return 0;
    }
}

static void start_scan_internal(void) {
    if (!s_status.ble_ready) {
        ESP_LOGW(TAG, "BLE host not synchronized yet");
        return;
    }
    if (s_scanning) ble_gap_disc_cancel();

    struct ble_gap_disc_params p = {0};
    p.passive = 0;
    p.filter_duplicates = 1;
    p.itvl = 0;
    p.window = 0;
    p.filter_policy = 0;
    p.limited = 0;

    ESP_LOGI(TAG, "Scanning for FTMS trainers for %d s ...", BLE_SCAN_SECONDS);
    int rc = ble_gap_disc(s_own_addr_type, BLE_SCAN_SECONDS * 1000, &p, gap_event, NULL);
    if (rc == 0) s_scanning = true;
    else ESP_LOGE(TAG, "BLE scan failed rc=%d", rc);
}

static void on_sync(void) {
    int rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    assert(rc == 0);
    lock(); s_status.ble_ready = true; unlock();
    ESP_LOGI(TAG, "NimBLE synchronized, central ready");
}

static void on_reset(int reason) {
    ESP_LOGE(TAG, "NimBLE reset reason=%d", reason);
    lock(); s_status.ble_ready = false; unlock();
}

static void host_task(void *param) {
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void trainer_manager_init(void) {
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        ESP_LOGE(TAG, "Could not create trainer mutex");
        return;
    }

    int rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed rc=%d", rc);
        return;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "Trainer manager initialized");
}

void trainer_manager_scan(void) { start_scan_internal(); }
void trainer_manager_connect(void) { connect_selected(); }

void trainer_manager_disconnect(void) {
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

void trainer_set_gear(int gear) {
    if (gear < MIN_GEAR) gear = MIN_GEAR;
    if (gear > MAX_GEAR) gear = MAX_GEAR;
    const int raw = gear_to_resistance_raw(gear);

    lock();
    s_status.gear = gear;
    s_status.requested_resistance_raw = raw;
    const float speed = s_status.speed_kmh;
    unlock();

    ESP_LOGI(TAG, "Gear %d selected -> resistance raw=%d", gear, raw);
    if (speed < MIN_RESISTANCE_APPLY_SPEED_KMH) {
        lock(); s_status.resistance_pending = true; unlock();
        ESP_LOGI(TAG, "Waiting for movement before applying gear (speed %.1f km/h)", speed);
        return;
    }
    if (send_resistance_raw(raw)) {
        lock(); s_status.resistance_pending = false; unlock();
    }
}

void trainer_gear_up(void) { trainer_status_t st = trainer_get_status(); trainer_set_gear(st.gear + 1); }
void trainer_gear_down(void) { trainer_status_t st = trainer_get_status(); trainer_set_gear(st.gear - 1); }

void trainer_toggle_start_pause(void) {
    trainer_status_t st = trainer_get_status();
    if (!st.control_granted) {
        trainer_request_control();
        return;
    }
    if (!st.running) {
        if (send_cp((const uint8_t[]){FTMS_CP_START_RESUME}, 1)) {
            lock(); s_status.running = true; s_status.paused = false; unlock();
        }
    } else if (!st.paused) {
        if (send_cp((const uint8_t[]){FTMS_CP_STOP_PAUSE, 0x02}, 2)) {
            lock(); s_status.paused = true; unlock();
        }
    } else {
        if (send_cp((const uint8_t[]){FTMS_CP_START_RESUME}, 1)) {
            lock(); s_status.paused = false; unlock();
        }
    }
}

void trainer_stop(void) {
    if (send_cp((const uint8_t[]){FTMS_CP_STOP_PAUSE, 0x01}, 2)) {
        lock(); s_status.running = false; s_status.paused = false; unlock();
    }
}

void trainer_set_erg_power(int watts) {
    trainer_status_t st = trainer_get_status();
    int minw = st.supported_power_min_w;
    int maxw = st.supported_power_max_w > 0 ? st.supported_power_max_w : 2000;
    if (watts < minw) watts = minw;
    if (watts > maxw) watts = maxw;
    int16_t v = (int16_t)watts;
    uint8_t cmd[] = {FTMS_CP_SET_TARGET_POWER, (uint8_t)(v & 0xff), (uint8_t)((v >> 8) & 0xff)};
    if (send_cp(cmd, sizeof(cmd))) {
        lock(); s_status.target_power_w = watts; unlock();
        ESP_LOGI(TAG, "ERG target %d W", watts);
    }
}

trainer_status_t trainer_get_status(void) {
    trainer_status_t copy;
    lock(); copy = s_status; unlock();
    return copy;
}
