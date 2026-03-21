/*
 * M5Stack Unit CamS3-5MP — WiFi MJPEG Streaming Firmware
 *
 * Endpoints:
 *   /         — Web UI with live stream viewer
 *   /capture  — Single JPEG snapshot
 *   /stream   — MJPEG stream (for Home Assistant)
 *   /status   — JSON device status
 *   /restart  — Reboot the device
 *
 * Home Assistant configuration.yaml:
 *   camera:
 *     - platform: mjpeg
 *       name: "M5 CamS3"
 *       mjpeg_url: "http://m5cam.local/stream"
 *       still_image_url: "http://m5cam.local/capture"
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_task_wdt.h"

#include "config.h"
#include "camera_pins.h"

// ---- Stability defaults (override in config.h) ----
#ifndef WIFI_RECONNECT_INTERVAL_MS
#define WIFI_RECONNECT_INTERVAL_MS 30000   // check WiFi every 30s
#endif
#ifndef CAMERA_HEALTH_INTERVAL_MS
#define CAMERA_HEALTH_INTERVAL_MS  60000   // test-grab every 60s
#endif
#ifndef CAMERA_MAX_FAILURES
#define CAMERA_MAX_FAILURES        5       // consecutive failures before reboot
#endif
#ifndef WDT_TIMEOUT_SEC
#define WDT_TIMEOUT_SEC            30      // watchdog timeout
#endif
#ifndef STREAM_MAX_FRAME_FAILURES
#define STREAM_MAX_FRAME_FAILURES  3       // transient failures before ending stream
#endif

// ---- Forward declarations: HTTP handlers ----
static esp_err_t index_handler(httpd_req_t *req);
static esp_err_t capture_handler(httpd_req_t *req);
static esp_err_t stream_handler(httpd_req_t *req);
static esp_err_t status_handler(httpd_req_t *req);
static esp_err_t restart_handler(httpd_req_t *req);

// ---- Forward declarations: setup helpers ----
static void setup_camera();
static void setup_wifi();
static void setup_http_server();

// SCCB (I2C) register access — from pre-compiled esp32-camera library.
// Used to manually configure PY260 sensor after esp_camera_init(), since the
// pre-compiled mega_ccm driver's set_pixformat() doesn't apply a proper reset
// sequence, leaving the sensor stuck in raw YUV422 mode instead of JPEG.
extern "C" {
    uint8_t SCCB_Read16(uint8_t slv_addr, uint16_t reg);
    int SCCB_Write16(uint8_t slv_addr, uint16_t reg, uint8_t data);
}

// ---- MJPEG stream boundary ----
#define PART_BOUNDARY "frame"
static const char *const STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *const STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *const STREAM_PART =
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// ---- LED helpers ----
static void led_on()  { digitalWrite(LED_GPIO_NUM, HIGH); }
static void led_off() { digitalWrite(LED_GPIO_NUM, LOW); }

// ---- Stability state ----
static unsigned long last_wifi_check = 0;
static unsigned long last_health_check = 0;
static int camera_fail_count = 0;

static void wifi_reconnect() {
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.println("WiFi lost — reconnecting...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 40) {
        delay(500);
        retries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        char ip[16];
        WiFi.localIP().toString().toCharArray(ip, sizeof(ip));
        Serial.printf("WiFi reconnected! IP: %s\n", ip);
        // Restore mDNS after reconnect
        MDNS.end();
        if (MDNS.begin(MDNS_HOSTNAME)) {
            MDNS.addService("http", "tcp", HTTP_PORT);
            Serial.printf("mDNS restored: http://%s.local\n", MDNS_HOSTNAME);
        }
    } else {
        Serial.println("WiFi reconnect failed — will retry next cycle");
    }
}

// ---- HTML page ----
static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>M5 CamS3-5MP</title>
  <style>
    *{box-sizing:border-box;margin:0;padding:0}
    body{font-family:system-ui,sans-serif;background:#1a1a2e;color:#e0e0e0;
         display:flex;flex-direction:column;align-items:center;min-height:100vh;padding:1rem}
    h1{margin:0.5rem 0;font-size:1.4rem;color:#00d4ff}
    .stream-container{position:relative;margin:1rem 0;max-width:100%%;
                      border:2px solid #333;border-radius:8px;overflow:hidden}
    img#stream{width:100%%;max-width:640px;display:block;background:#000}
    .controls{display:flex;gap:0.5rem;flex-wrap:wrap;justify-content:center;margin:1rem 0}
    button{padding:0.6rem 1.2rem;border:none;border-radius:6px;cursor:pointer;
           font-size:0.9rem;font-weight:600;transition:all 0.2s}
    .btn-stream{background:#00d4ff;color:#1a1a2e}
    .btn-stream:hover{background:#00b8d9}
    .btn-capture{background:#ff6b6b;color:#fff}
    .btn-capture:hover{background:#ee5a5a}
    .btn-restart{background:#ff9f43;color:#1a1a2e}
    .btn-restart:hover{background:#e08b30}
    .info{margin-top:1rem;padding:1rem;background:#16213e;border-radius:8px;
          font-size:0.8rem;line-height:1.6;max-width:640px;width:100%%}
    .info code{background:#0f3460;padding:0.15rem 0.4rem;border-radius:3px;font-size:0.85em}
    a{color:#00d4ff}
  </style>
</head>
<body>
  <h1>📷 M5 CamS3-5MP</h1>
  <div class="stream-container">
    <img id="stream" src="/capture" alt="Camera stream">
  </div>
  <div class="controls">
    <button class="btn-stream" onclick="toggleStream()">Start Stream</button>
    <button class="btn-capture" onclick="captureStill()">📸 Snapshot</button>
    <button class="btn-restart" onclick="restartDevice()">🔄 Restart</button>
  </div>
  <div class="info">
    <strong>Endpoints:</strong><br>
    Stream: <code><a href="/stream">/stream</a></code> &nbsp;
    Snapshot: <code><a href="/capture">/capture</a></code> &nbsp;
    Status: <code><a href="/status">/status</a></code> &nbsp;
    Restart: <code><a href="/restart">/restart</a></code><br><br>
    <strong>Home Assistant (configuration.yaml):</strong><br>
    <code>camera:</code><br>
    <code>&nbsp;&nbsp;- platform: mjpeg</code><br>
    <code>&nbsp;&nbsp;&nbsp;&nbsp;name: "M5 CamS3"</code><br>
    <code>&nbsp;&nbsp;&nbsp;&nbsp;mjpeg_url: "http://%s/stream"</code><br>
    <code>&nbsp;&nbsp;&nbsp;&nbsp;still_image_url: "http://%s/capture"</code>
  </div>
</body>
<script>
  let streaming = false;
  function toggleStream() {
    const img = document.getElementById('stream');
    const btn = document.querySelector('.btn-stream');
    if (streaming) {
      img.src = '/capture?' + Date.now();
      btn.textContent = 'Start Stream';
    } else {
      img.src = '/stream';
      btn.textContent = 'Stop Stream';
    }
    streaming = !streaming;
  }
  function captureStill() {
    const img = document.getElementById('stream');
    const btn = document.querySelector('.btn-stream');
    img.src = '/capture?' + Date.now();
    streaming = false;
    btn.textContent = 'Start Stream';
  }
  function restartDevice() {
    if (!confirm('Restart the camera?')) return;
    fetch('/restart').then(() => {
      document.body.innerHTML = '<h1 style="color:#ff9f43;margin:2rem">Restarting…</h1>' +
        '<p style="color:#e0e0e0">Reloading in 10 seconds.</p>';
      setTimeout(() => location.reload(), 10000);
    });
  }
</script>
</html>
)rawliteral";

// ============================================================
//  Camera initialisation
// ============================================================

// Apply a single sensor setting and log whether the driver accepted it.
// Uses a typedef matching the common sensor_t setter signature.
typedef int (*sensor_set_fn_t)(sensor_t *, int);

static bool apply_sensor_setting(sensor_t *s, const char *name,
                                 sensor_set_fn_t fn, int value) {
    if (!fn) {
        Serial.printf("  %-18s SKIPPED (not implemented)\n", name);
        return false;
    }
    int rc = fn(s, value);
    Serial.printf("  %-18s %s (value=%d)\n", name, rc == 0 ? "OK" : "FAILED", value);
    return rc == 0;
}

// PY260 SCCB workaround: the pre-compiled mega_ccm driver doesn't include
// a proper delay in its reset sequence, so the sensor stays in raw YUV422
// mode. We perform a manual reset with correct timing, then re-configure
// pixel format, resolution, and quality via direct SCCB register writes.
static void py260_sccb_configure(sensor_t *sensor) {
    Serial.printf("Sensor PID: 0x%04X\n", sensor->id.PID);

    // Reset with 100ms hold + 1500ms settle (matches M5Stack reference)
    SCCB_Write16(sensor->slv_addr, 0x0102, 0x00);
    delay(100);
    SCCB_Write16(sensor->slv_addr, 0x0102, 0x01);
    delay(1500);

    // PY260 register map: 0x0120=pixel_fmt, 0x0121=resolution, 0x012A=quality
    SCCB_Write16(sensor->slv_addr, 0x0120, 0x01);  // JPEG
    delay(100);

    // Resolution values: 0x01=QVGA, 0x02=VGA, 0x03=HD, 0x04=FHD
    uint8_t res_reg;
    switch (CAMERA_FRAME_SIZE) {
        case FRAMESIZE_QVGA: res_reg = 0x01; break;
        case FRAMESIZE_VGA:  res_reg = 0x02; break;
        case FRAMESIZE_HD:   res_reg = 0x03; break;
        case FRAMESIZE_FHD:  res_reg = 0x04; break;
        default:             res_reg = 0x02; break;  // Default VGA
    }
    SCCB_Write16(sensor->slv_addr, 0x0121, res_reg);
    delay(100);

    // Quality: 0=high, 1=default, 2=low
    SCCB_Write16(sensor->slv_addr, 0x012A, 0x00);  // High quality
    delay(100);

    Serial.println("PY260 configured via SCCB");
}

// Re-enable ISP features after manual reset — without this the sensor's
// image processing pipeline may be left unconfigured, causing red cast and
// excessive noise in low-light scenes.
static void configure_isp(sensor_t *s) {
    Serial.println("Configuring ISP features:");

    // White balance
    apply_sensor_setting(s, "AWB",            s->set_whitebal,      1);
    apply_sensor_setting(s, "AWB gain",       s->set_awb_gain,      1);
    apply_sensor_setting(s, "WB mode (auto)", s->set_wb_mode,       0);

    // Exposure & gain
    apply_sensor_setting(s, "Auto exposure",  s->set_exposure_ctrl, 1);
    apply_sensor_setting(s, "AEC2 (DSP)",     s->set_aec2,         1);
    apply_sensor_setting(s, "Auto gain",      s->set_gain_ctrl,     1);

    // set_gainceiling has a different signature (gainceiling_t enum) — call directly
    if (s->set_gainceiling) {
        int rc = s->set_gainceiling(s, static_cast<gainceiling_t>(2));
        Serial.printf("  %-18s %s (value=%d)\n", "Gain ceiling",
                      rc == 0 ? "OK" : "FAILED", 2);
    } else {
        Serial.printf("  %-18s SKIPPED (not implemented)\n", "Gain ceiling");
    }

    // Pixel / lens correction
    apply_sensor_setting(s, "BPC",            s->set_bpc,           1);
    apply_sensor_setting(s, "WPC",            s->set_wpc,           1);
    apply_sensor_setting(s, "Gamma",          s->set_raw_gma,       1);
    apply_sensor_setting(s, "Lens correction",s->set_lenc,          1);

    // Noise reduction
    apply_sensor_setting(s, "Denoise",        s->set_denoise,       0);
}

static void setup_camera() {
    camera_config_t config = {};
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = Y2_GPIO_NUM;
    config.pin_d1       = Y3_GPIO_NUM;
    config.pin_d2       = Y4_GPIO_NUM;
    config.pin_d3       = Y5_GPIO_NUM;
    config.pin_d4       = Y6_GPIO_NUM;
    config.pin_d5       = Y7_GPIO_NUM;
    config.pin_d6       = Y8_GPIO_NUM;
    config.pin_d7       = Y9_GPIO_NUM;
    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_pclk     = PCLK_GPIO_NUM;
    config.pin_vsync    = VSYNC_GPIO_NUM;
    config.pin_href     = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn     = PWDN_GPIO_NUM;
    config.pin_reset    = RESET_GPIO_NUM;
    config.xclk_freq_hz = CAMERA_XCLK_FREQ;
    config.frame_size   = CAMERA_FRAME_SIZE;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
    config.jpeg_quality = CAMERA_JPEG_QUALITY;
    config.fb_count     = CAMERA_FB_COUNT;

    // Use PSRAM if available, fall back to DRAM
    if (ESP.getPsramSize() > 0) {
        config.fb_location = CAMERA_FB_IN_PSRAM;
        Serial.printf("Using PSRAM for frame buffers (available: %u bytes)\n",
                      ESP.getFreePsram());
    } else {
        config.fb_location = CAMERA_FB_IN_DRAM;
        Serial.println("WARNING: No PSRAM detected, using DRAM");
    }

    Serial.println("Calling esp_camera_init...");
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed: 0x%x\n", err);
        for (;;) {
            led_on(); delay(100); led_off(); delay(100);
        }
    }
    Serial.println("Camera initialized");

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor) {
        py260_sccb_configure(sensor);
        configure_isp(sensor);
    }

    // Wait for sensor to stabilize
    delay(2000);
}

// ============================================================
//  WiFi
// ============================================================
static void setup_wifi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    WiFi.setSleep(false);  // Keep WiFi active for low-latency streaming

    Serial.print("Connecting to WiFi");
    led_on();
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        if (++retries > 40) {  // 20-second timeout
            Serial.println("\nWiFi connection failed! Restarting...");
            ESP.restart();
        }
    }
    led_off();
    char ip[16];
    WiFi.localIP().toString().toCharArray(ip, sizeof(ip));
    Serial.printf("\nConnected! IP: %s\n", ip);

    if (MDNS.begin(MDNS_HOSTNAME)) {
        MDNS.addService("http", "tcp", HTTP_PORT);
        Serial.printf("mDNS: http://%s.local\n", MDNS_HOSTNAME);
    }
}

// ============================================================
//  HTTP Server
// ============================================================
static void setup_http_server() {
    httpd_config_t httpd_config = HTTPD_DEFAULT_CONFIG();
    httpd_config.server_port      = HTTP_PORT;
    httpd_config.max_uri_handlers = 8;
    httpd_config.stack_size       = 16384;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &httpd_config) == ESP_OK) {
        httpd_uri_t index_uri = {
            .uri = "/", .method = HTTP_GET,
            .handler = index_handler, .user_ctx = NULL
        };
        httpd_uri_t capture_uri = {
            .uri = "/capture", .method = HTTP_GET,
            .handler = capture_handler, .user_ctx = NULL
        };
        httpd_uri_t stream_uri = {
            .uri = "/stream", .method = HTTP_GET,
            .handler = stream_handler, .user_ctx = NULL
        };
        httpd_uri_t status_uri = {
            .uri = "/status", .method = HTTP_GET,
            .handler = status_handler, .user_ctx = NULL
        };
        httpd_uri_t restart_uri = {
            .uri = "/restart", .method = HTTP_GET,
            .handler = restart_handler, .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &capture_uri);
        httpd_register_uri_handler(server, &stream_uri);
        httpd_register_uri_handler(server, &status_uri);
        httpd_register_uri_handler(server, &restart_uri);
        Serial.println("HTTP server started");
    } else {
        Serial.println("HTTP server failed to start!");
    }
}

// ============================================================
//  Setup
// ============================================================
void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    unsigned long start = millis();
    while (!Serial && (millis() - start < 5000)) delay(10);
    delay(500);
    Serial.println("\n=== M5 CamS3-5MP Boot ===");

    pinMode(LED_GPIO_NUM, OUTPUT);
    led_off();

    setup_camera();
    setup_wifi();
    setup_http_server();

    // Ready — brief LED flash
    led_on(); delay(200); led_off();

    // ---- Watchdog ----
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WDT_TIMEOUT_SEC * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    esp_task_wdt_reconfigure(&wdt_config);
    esp_task_wdt_add(NULL);  // subscribe Arduino loop task
    Serial.printf("Watchdog enabled (%ds timeout)\n", WDT_TIMEOUT_SEC);

    Serial.println("========================================");
    Serial.printf("  M5 CamS3-5MP Ready!\n");
    char boot_ip[16];
    WiFi.localIP().toString().toCharArray(boot_ip, sizeof(boot_ip));
    Serial.printf("  http://%s\n", boot_ip);
    Serial.printf("  http://%s.local\n", MDNS_HOSTNAME);
    Serial.println("========================================");
}

// ============================================================
//  Loop — WiFi reconnection + camera health monitoring
// ============================================================
void loop() {
    esp_task_wdt_reset();
    unsigned long now = millis();

    // WiFi reconnection
    if (now - last_wifi_check >= WIFI_RECONNECT_INTERVAL_MS) {
        last_wifi_check = now;
        wifi_reconnect();
    }

    // Camera health: grab a test frame to detect sensor hangs
    if (now - last_health_check >= CAMERA_HEALTH_INTERVAL_MS) {
        last_health_check = now;
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) {
            esp_camera_fb_return(fb);
            camera_fail_count = 0;
        } else {
            camera_fail_count++;
            Serial.printf("Camera health: frame grab failed (%d/%d)\n",
                          camera_fail_count, CAMERA_MAX_FAILURES);
            if (camera_fail_count >= CAMERA_MAX_FAILURES) {
                Serial.println("Camera unresponsive — rebooting!");
                delay(100);
                ESP.restart();
            }
        }
    }

    delay(1000);
}

// ============================================================
//  Handler: /  (Web UI)
// ============================================================
static esp_err_t index_handler(httpd_req_t *req) {
    // Render the template into a heap buffer (PSRAM-backed when available)
    // to avoid consuming 25% of the HTTP task's 16KB stack.
    char ip[16];
    WiFi.localIP().toString().toCharArray(ip, sizeof(ip));
    size_t needed = strlen_P(INDEX_HTML) + 128;  // template + IP substitutions
    char *html = static_cast<char *>(malloc(needed));
    if (!html) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "Out of memory", HTTPD_RESP_USE_STRLEN);
    }
    snprintf(html, needed, INDEX_HTML, ip, ip);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t res = httpd_resp_send(req, html, strlen(html));
    free(html);
    return res;
}

// ============================================================
//  Handler: /capture  (Single JPEG frame)
// ============================================================
static esp_err_t capture_handler(httpd_req_t *req) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Capture failed — fb_get returned NULL");
        const char *err_msg = "{\"error\":\"Camera frame buffer is NULL. "
            "The sensor may not be producing frames.\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, err_msg, strlen(err_msg));
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition",
                       "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");

    esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return res;
}

// ============================================================
//  Handler: /stream  (MJPEG stream — Home Assistant compatible)
// ============================================================
static esp_err_t stream_handler(httpd_req_t *req) {
    esp_err_t res = ESP_OK;
    char part_buf[128];

    httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "15");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");

    led_on();
    Serial.println("Stream started");

    int64_t last_frame = esp_timer_get_time();
    int frame_failures = 0;

    while (true) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            frame_failures++;
            Serial.printf("Stream: frame grab failed (%d/%d)\n",
                          frame_failures, STREAM_MAX_FRAME_FAILURES);
            if (frame_failures >= STREAM_MAX_FRAME_FAILURES) {
                res = ESP_FAIL;
                break;
            }
            delay(100);
            continue;
        }
        frame_failures = 0;

        // Send MJPEG boundary
        res = httpd_resp_send_chunk(req, STREAM_BOUNDARY,
                                    strlen(STREAM_BOUNDARY));
        if (res != ESP_OK) {
            esp_camera_fb_return(fb);
            break;
        }

        // Send part header with content length
        size_t hlen = snprintf(part_buf, sizeof(part_buf),
                               STREAM_PART, fb->len);
        res = httpd_resp_send_chunk(req, part_buf, hlen);
        if (res != ESP_OK) {
            esp_camera_fb_return(fb);
            break;
        }

        // Send JPEG data
        size_t frame_len = fb->len;
        res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        esp_camera_fb_return(fb);
        if (res != ESP_OK) break;

        // Frame timing stats
        int64_t now = esp_timer_get_time();
        int64_t frame_time = (now - last_frame) / 1000;
        last_frame = now;
        Serial.printf("MJPG: %4luKB %3lums (%.1f fps)\r",
                      (unsigned long)(frame_len / 1024),
                      (unsigned long)frame_time,
                      frame_time > 0 ? 1000.0 / frame_time : 0.0);
    }

    led_off();
    Serial.println("Stream ended");
    return res;
}

// ============================================================
//  Handler: /status  (JSON device info)
// ============================================================
static esp_err_t status_handler(httpd_req_t *req) {
    char json[768];
    sensor_t *s = esp_camera_sensor_get();
    char ip[16];
    WiFi.localIP().toString().toCharArray(ip, sizeof(ip));

    int written = snprintf(json, sizeof(json),
        "{"
        "\"device\":\"M5Stack Unit CamS3-5MP\","
        "\"sensor_pid\":\"0x%04X\","
        "\"sensor_midh\":\"0x%02X\","
        "\"sensor_midl\":\"0x%02X\","
        "\"ip\":\"%s\","
        "\"hostname\":\"%s.local\","
        "\"rssi\":%d,"
        "\"uptime_sec\":%lu,"
        "\"free_heap\":%u,"
        "\"psram_size\":%u,"
        "\"free_psram\":%u,"
        "\"framesize\":%d,"
        "\"quality\":%d,"
        "\"stream_url\":\"http://%s/stream\","
        "\"capture_url\":\"http://%s/capture\""
        "}",
        s ? s->id.PID : 0,
        s ? s->id.MIDH : 0,
        s ? s->id.MIDL : 0,
        ip,
        MDNS_HOSTNAME,
        WiFi.RSSI(),
        (unsigned long)(esp_timer_get_time() / 1000000),
        ESP.getFreeHeap(),
        ESP.getPsramSize(),
        ESP.getFreePsram(),
        s ? s->status.framesize : -1,
        s ? s->status.quality : -1,
        ip,
        ip
    );

    if (written < 0 || static_cast<size_t>(written) >= sizeof(json)) {
        Serial.println("WARNING: /status JSON truncated");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json, strlen(json));
}

// ============================================================
//  Handler: /restart  (Reboot device)
// ============================================================
static esp_err_t restart_handler(httpd_req_t *req) {
    Serial.println("Restart requested via /restart");
    const char *resp = "{\"message\":\"Restarting...\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, resp, strlen(resp));
    delay(500);
    ESP.restart();
    return ESP_OK;
}
