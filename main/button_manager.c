#include "button_manager.h"
#include "app_config.h"
#include "trainer_manager.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BUTTON";

typedef enum { B_UP, B_DOWN, B_START, B_MODE } btn_id_t;
typedef struct {
    btn_id_t id;
    gpio_num_t pin;
    int stable;
    int last_raw;
    TickType_t changed_at;
    TickType_t pressed_at;
    bool long_sent;
} btn_t;

static btn_t btns[] = {
    {B_UP, BTN_GEAR_UP_GPIO, 1, 1, 0, 0, false},
    {B_DOWN, BTN_GEAR_DOWN_GPIO, 1, 1, 0, 0, false},
    {B_START, BTN_START_PAUSE_GPIO, 1, 1, 0, 0, false},
    {B_MODE, BTN_MODE_GPIO, 1, 1, 0, 0, false},
};

static void short_press(btn_id_t id) {
    switch (id) {
        case B_UP: ESP_LOGI(TAG, "Gear up"); trainer_gear_up(); break;
        case B_DOWN: ESP_LOGI(TAG, "Gear down"); trainer_gear_down(); break;
        case B_START: ESP_LOGI(TAG, "Start/Pause"); trainer_toggle_start_pause(); break;
        case B_MODE: ESP_LOGI(TAG, "Mode button pressed (reserved)"); break;
    }
}

static void long_press(btn_id_t id) {
    if (id == B_START) {
        ESP_LOGI(TAG, "Long press Start/Pause -> Stop");
        trainer_stop();
    }
}

static void task(void *arg) {
    const TickType_t poll = pdMS_TO_TICKS(BUTTON_POLL_MS);
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        for (unsigned i = 0; i < sizeof(btns)/sizeof(btns[0]); ++i) {
            btn_t *b = &btns[i];
            int raw = gpio_get_level(b->pin);
            if (raw != b->last_raw) {
                b->last_raw = raw;
                b->changed_at = now;
            }
            if (raw != b->stable && (now - b->changed_at) >= pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS)) {
                b->stable = raw;
                if (b->stable == BUTTON_ACTIVE_LEVEL) {
                    b->pressed_at = now;
                    b->long_sent = false;
                } else {
                    if (!b->long_sent) short_press(b->id);
                }
            }
            if (b->stable == BUTTON_ACTIVE_LEVEL && !b->long_sent &&
                (now - b->pressed_at) >= pdMS_TO_TICKS(BUTTON_LONG_PRESS_MS)) {
                b->long_sent = true;
                long_press(b->id);
            }
        }
        vTaskDelay(poll);
    }
}

void button_manager_init(void) {
    uint64_t mask = 0;
    for (unsigned i = 0; i < sizeof(btns)/sizeof(btns[0]); ++i) mask |= 1ULL << btns[i].pin;
    gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    ESP_LOGI(TAG, "Buttons: UP=GPIO%d DOWN=GPIO%d START=GPIO%d MODE=GPIO%d",
             BTN_GEAR_UP_GPIO, BTN_GEAR_DOWN_GPIO, BTN_START_PAUSE_GPIO, BTN_MODE_GPIO);
    xTaskCreate(task, "buttons", 3072, NULL, 6, NULL);
}
