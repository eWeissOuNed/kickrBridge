#include "wifi_manager.h"
#include "app_config.h"
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "mdns.h"

static const char *TAG = "WIFI";
static const char *NS = "kickr_cfg";
static bool s_connected;
static bool s_mdns_started;
static char s_ip[16] = "-";

static bool load_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t a = ssid_len, b = pass_len;
    esp_err_t e1 = nvs_get_str(h, "ssid", ssid, &a);
    esp_err_t e2 = nvs_get_str(h, "pass", pass, &b);
    nvs_close(h);
    return e1 == ESP_OK && e2 == ESP_OK && ssid[0] != '\0';
}

static void start_mdns(void) {
    if (s_mdns_started) return;
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(KICKR_MDNS_HOSTNAME));
    ESP_ERROR_CHECK(mdns_instance_name_set(KICKR_MDNS_INSTANCE));
    ESP_ERROR_CHECK(mdns_service_add(KICKR_MDNS_INSTANCE, "_http", "_tcp", KICKR_HTTP_PORT, NULL, 0));
    s_mdns_started = true;
    ESP_LOGI(TAG, "mDNS ready: http://%s.local", KICKR_MDNS_HOSTNAME);
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        strcpy(s_ip, "-");
        ESP_LOGW(TAG, "Disconnected from Wi-Fi");
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&e->ip_info.ip));
        s_connected = true;
        ESP_LOGI(TAG, "Connected, IP=%s", s_ip);
        start_mdns();
    }
}

void wifi_manager_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

bool wifi_manager_set_credentials(const char *ssid, const char *password) {
    if (!ssid || !ssid[0] || strlen(ssid) > 32 || !password || strlen(password) > 63) return false;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = nvs_set_str(h, "ssid", ssid);
    if (e == ESP_OK) e = nvs_set_str(h, "pass", password);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e == ESP_OK) ESP_LOGI(TAG, "Wi-Fi credentials stored for SSID '%s'", ssid);
    return e == ESP_OK;
}

bool wifi_manager_forget_credentials(void) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    nvs_erase_key(h, "ssid");
    nvs_erase_key(h, "pass");
    esp_err_t e = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Stored Wi-Fi credentials erased");
    return e == ESP_OK;
}

bool wifi_manager_has_credentials(void) {
    char s[33] = {0}, p[64] = {0};
    return load_credentials(s, sizeof(s), p, sizeof(p));
}

bool wifi_manager_connect(void) {
    char ssid[33] = {0}, pass[64] = {0};
    if (!load_credentials(ssid, sizeof(ssid), pass, sizeof(pass))) {
        ESP_LOGW(TAG, "No stored Wi-Fi credentials. Use: wifi set <ssid> <password>");
        return false;
    }
    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_LOGI(TAG, "Connecting to '%s' ...", ssid);
    return esp_wifi_connect() == ESP_OK;
}

void wifi_manager_disconnect(void) { esp_wifi_disconnect(); }
bool wifi_manager_is_connected(void) { return s_connected; }

void wifi_manager_get_status(char *buf, size_t len) {
    char ssid[33] = "<not configured>", pass[64] = {0};
    load_credentials(ssid, sizeof(ssid), pass, sizeof(pass));
    snprintf(buf, len,
             "Wi-Fi: %s\nSSID: %s\nPassword: %s\nIP: %s\nmDNS: http://%s.local\n",
             s_connected ? "connected" : "disconnected",
             ssid, pass[0] ? "********" : "<none>", s_ip, KICKR_MDNS_HOSTNAME);
}
