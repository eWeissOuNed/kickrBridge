#include "nvs_flash.h"
#include "esp_log.h"
#include "wifi_manager.h"
#include "console_cli.h"
#include "button_manager.h"
#include "trainer_manager.h"
#include "web_server.h"

static const char *TAG = "APP";

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_LOGI(TAG, "KICKR Bridge starting");
    trainer_manager_init();
    wifi_manager_init();
    button_manager_init();
    web_server_start();
    console_cli_start();

    if (wifi_manager_has_credentials()) wifi_manager_connect();
    else ESP_LOGW(TAG, "Wi-Fi not configured. Serial command: wifi set <ssid> <password>");
}
