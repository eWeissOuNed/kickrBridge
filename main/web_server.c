#include "web_server.h"
#include "trainer_manager.h"
#include "app_config.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "WEB";

static esp_err_t root(httpd_req_t *req) {
    static const char page[] =
        "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>body{font-family:system-ui;background:#101216;color:#eee;margin:2rem}"
        ".card{max-width:520px;background:#1b1f26;border-radius:18px;padding:24px}"
        "h1{margin-top:0}button{font-size:18px;padding:12px 18px;margin:4px}</style>"
        "<div class='card'><h1>KICKR Bridge</h1><p id='s'>Loading…</p>"
        "<button onclick=cmd('/api/gear/down')>− Gear</button>"
        "<button onclick=cmd('/api/startpause')>Start / Pause</button>"
        "<button onclick=cmd('/api/gear/up')>+ Gear</button></div>"
        "<script>async function refresh(){let r=await fetch('/api/status');let j=await r.json();"
        "s.textContent=`Trainer: ${j.connected?'connected':'not connected'} | Gear ${j.gear} | ${j.power} W`;};"
        "async function cmd(u){await fetch(u,{method:'POST'});refresh();}setInterval(refresh,1000);refresh();</script>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status(httpd_req_t *req) {
    trainer_status_t s = trainer_get_status();
    char out[256];
    snprintf(out, sizeof(out),
             "{\"connected\":%s,\"gear\":%d,\"running\":%s,\"paused\":%s,\"power\":%d,\"cadence\":%d,\"speed\":%.1f,\"targetPower\":%d}",
             s.connected?"true":"false", s.gear, s.running?"true":"false", s.paused?"true":"false",
             s.power_w, s.cadence_rpm, s.speed_kmh, s.target_power_w);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, out);
}
static esp_err_t up(httpd_req_t *r){trainer_gear_up();return httpd_resp_sendstr(r,"OK");}
static esp_err_t down(httpd_req_t *r){trainer_gear_down();return httpd_resp_sendstr(r,"OK");}
static esp_err_t startpause(httpd_req_t *r){trainer_toggle_start_pause();return httpd_resp_sendstr(r,"OK");}

void web_server_start(void) {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = KICKR_HTTP_PORT;
    httpd_handle_t h = NULL;
    ESP_ERROR_CHECK(httpd_start(&h, &cfg));
    httpd_uri_t u0={.uri="/",.method=HTTP_GET,.handler=root};
    httpd_uri_t u1={.uri="/api/status",.method=HTTP_GET,.handler=status};
    httpd_uri_t u2={.uri="/api/gear/up",.method=HTTP_POST,.handler=up};
    httpd_uri_t u3={.uri="/api/gear/down",.method=HTTP_POST,.handler=down};
    httpd_uri_t u4={.uri="/api/startpause",.method=HTTP_POST,.handler=startpause};
    ESP_ERROR_CHECK(httpd_register_uri_handler(h,&u0));
    ESP_ERROR_CHECK(httpd_register_uri_handler(h,&u1));
    ESP_ERROR_CHECK(httpd_register_uri_handler(h,&u2));
    ESP_ERROR_CHECK(httpd_register_uri_handler(h,&u3));
    ESP_ERROR_CHECK(httpd_register_uri_handler(h,&u4));
    ESP_LOGI(TAG, "HTTP server started on port %d", KICKR_HTTP_PORT);
}
