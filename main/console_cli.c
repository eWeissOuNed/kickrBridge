#include "console_cli.h"
#include "wifi_manager.h"
#include "trainer_manager.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "CLI";

static void help(void) {
    puts("\nCommands:\n"
         "  help\n"
         "  wifi set <ssid> <password>\n"
         "  wifi connect\n"
         "  wifi disconnect\n"
         "  wifi status\n"
         "  wifi forget\n"
         "  trainer scan|connect|disconnect|status\n"
         "  gear <1-10>|up|down\n"
         "  erg <watts>\n"
         "  start\n"
         "  stop\n"
         "  log info|debug|verbose|warn|error\n"
         "  reboot\n");
}

static void execute(char *line) {
    char *argv[8] = {0}; int argc=0;
    for (char *t=strtok(line," \t\r\n"); t && argc<8; t=strtok(NULL," \t\r\n")) argv[argc++]=t;
    if (!argc) return;
    if (!strcmp(argv[0],"help")) { help(); return; }
    if (!strcmp(argv[0],"wifi")) {
        if (argc>=4 && !strcmp(argv[1],"set")) {
            puts(wifi_manager_set_credentials(argv[2],argv[3]) ? "Credentials saved." : "Failed to save credentials.");
        } else if (argc>=2 && !strcmp(argv[1],"connect")) wifi_manager_connect();
        else if (argc>=2 && !strcmp(argv[1],"disconnect")) wifi_manager_disconnect();
        else if (argc>=2 && !strcmp(argv[1],"forget")) wifi_manager_forget_credentials();
        else if (argc>=2 && !strcmp(argv[1],"status")) { char b[256]; wifi_manager_get_status(b,sizeof(b)); puts(b); }
        else puts("Usage: wifi set|connect|disconnect|status|forget");
        return;
    }
    if (!strcmp(argv[0],"trainer")) {
        if (argc>=2 && !strcmp(argv[1],"scan")) trainer_manager_scan();
        else if (argc>=2 && !strcmp(argv[1],"connect")) trainer_manager_connect();
        else if (argc>=2 && !strcmp(argv[1],"disconnect")) trainer_manager_disconnect();
        else if (argc>=2 && !strcmp(argv[1],"status")) { trainer_status_t s=trainer_get_status(); printf("connected=%d gear=%d running=%d paused=%d power=%dW cadence=%drpm speed=%.1fkm/h target=%dW\n",s.connected,s.gear,s.running,s.paused,s.power_w,s.cadence_rpm,s.speed_kmh,s.target_power_w); }
        else puts("Usage: trainer scan|connect|disconnect|status");
        return;
    }
    if (!strcmp(argv[0],"gear")) {
        if (argc<2) return;
        if (!strcmp(argv[1],"up")) trainer_gear_up();
        else if (!strcmp(argv[1],"down")) trainer_gear_down();
        else trainer_set_gear(atoi(argv[1]));
        return;
    }
    if (!strcmp(argv[0],"erg") && argc>=2) { trainer_set_erg_power(atoi(argv[1])); return; }
    if (!strcmp(argv[0],"start")) { trainer_toggle_start_pause(); return; }
    if (!strcmp(argv[0],"stop")) { trainer_stop(); return; }
    if (!strcmp(argv[0],"log") && argc>=2) {
        esp_log_level_t l=ESP_LOG_INFO;
        if(!strcmp(argv[1],"debug"))l=ESP_LOG_DEBUG; else if(!strcmp(argv[1],"verbose"))l=ESP_LOG_VERBOSE;
        else if(!strcmp(argv[1],"warn"))l=ESP_LOG_WARN; else if(!strcmp(argv[1],"error"))l=ESP_LOG_ERROR;
        esp_log_level_set("*",l); printf("Log level set to %s\n",argv[1]); return;
    }
    if (!strcmp(argv[0],"reboot")) { esp_restart(); }
    puts("Unknown command. Type 'help'.");
}

static void task(void *arg) {
    char line[192];
    puts("\nKICKR Bridge serial console\nType 'help' for commands.");
    for (;;) {
        printf("kickr> "); fflush(stdout);
        if (fgets(line,sizeof(line),stdin)) execute(line);
        else vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void console_cli_start(void) {
    ESP_LOGI(TAG, "Starting serial CLI");
    xTaskCreate(task, "console_cli", 4096, NULL, 4, NULL);
}
