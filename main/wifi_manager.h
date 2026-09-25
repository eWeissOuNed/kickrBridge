#pragma once
#include <stdbool.h>
#include <stddef.h>

void wifi_manager_init(void);
bool wifi_manager_set_credentials(const char *ssid, const char *password);
bool wifi_manager_forget_credentials(void);
bool wifi_manager_connect(void);
void wifi_manager_disconnect(void);
bool wifi_manager_has_credentials(void);
bool wifi_manager_is_connected(void);
void wifi_manager_get_status(char *buf, size_t len);
