/*
 * M5Stack Unit CamS3-5MP — WiFi MJPEG Streaming Firmware
 *
 * Endpoints:
 *   /         — Web UI with live stream viewer
 *   /capture  — Single JPEG snapshot
 *   /stream   — MJPEG stream (for Home Assistant)
 *   /status   — JSON device status
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

#include "config.h"
#include "camera_pins.h"

// ---- Forward declarations ----
static esp_err_t index_handler(httpd_req_t *req);
static esp_err_t capture_handler(httpd_req_t *req);
static esp_err_t stream_handler(httpd_req_t *req);
static esp_err_t status_handler(httpd_req_t *req);

// ---- MJPEG stream boundary ----
#define PART_BOUNDARY "frame"
static const char *STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART =
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// ---- LED helpers ----
static void led_on()  { digitalWrite(LED_GPIO_NUM, HIGH); }
static void led_off() { digitalWrite(LED_GPIO_NUM, LOW); }

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
  </div>
  <div class="info">
    <strong>Endpoints:</strong><br>
    Stream: <code><a href="/stream">/stream</a></code> &nbsp;
    Snapshot: <code><a href="/capture">/capture</a></code> &nbsp;
    Status: <code><a href="/status">/status</a></code><br><br>
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
</script>
</html>
)rawliteral";

// ============================================================
//  Setup
// ============================================================
void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    Serial.println();

    // LED indicator
    pinMode(LED_GPIO_NUM, OUTPUT);
    led_off();

    // ---- Camera init ----
    camera_config_t config;
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
    config.fb_location  = CAMERA_FB_IN_PSRAM;
    config.jpeg_quality = CAMERA_JPEG_QUALITY;
    config.fb_count     = CAMERA_FB_COUNT;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed: 0x%x\n", err);
        // Blink LED rapidly to indicate error
        for (;;) {
            led_on(); delay(100); led_off(); delay(100);
        }
    }
    Serial.println("Camera initialized");

    // ---- WiFi ----
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
    Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());

    // ---- mDNS ----
    if (MDNS.begin(MDNS_HOSTNAME)) {
        MDNS.addService("http", "tcp", HTTP_PORT);
        Serial.printf("mDNS: http://%s.local\n", MDNS_HOSTNAME);
    }

    // ---- HTTP Server ----
    httpd_config_t httpd_config = HTTPD_DEFAULT_CONFIG();
    httpd_config.server_port    = HTTP_PORT;
    httpd_config.max_uri_handlers = 8;
    httpd_config.stack_size     = 8192;

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
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &capture_uri);
        httpd_register_uri_handler(server, &stream_uri);
        httpd_register_uri_handler(server, &status_uri);
        Serial.println("HTTP server started");
    } else {
        Serial.println("HTTP server failed to start!");
    }

    // Ready — brief LED flash
    led_on(); delay(200); led_off();

    Serial.println("========================================");
    Serial.printf("  M5 CamS3-5MP Ready!\n");
    Serial.printf("  http://%s\n", WiFi.localIP().toString().c_str());
    Serial.printf("  http://%s.local\n", MDNS_HOSTNAME);
    Serial.println("========================================");
}

// ============================================================
//  Loop — nothing needed; HTTP server runs on its own task
// ============================================================
void loop() {
    delay(10000);
}

// ============================================================
//  Handler: /  (Web UI)
// ============================================================
static esp_err_t index_handler(httpd_req_t *req) {
    String ip = WiFi.localIP().toString();
    // INDEX_HTML has two %s placeholders for the IP address
    size_t html_len = strlen_P(INDEX_HTML) + ip.length() * 2 + 1;
    char *html = (char *)malloc(html_len);
    if (!html) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    snprintf(html, html_len, INDEX_HTML, ip.c_str(), ip.c_str());
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, html, strlen(html));
    free(html);
    return ESP_OK;
}

// ============================================================
//  Handler: /capture  (Single JPEG frame)
// ============================================================
static esp_err_t capture_handler(httpd_req_t *req) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
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

    while (true) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            Serial.println("Stream: frame capture failed");
            res = ESP_FAIL;
            break;
        }

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
        Serial.printf("MJPG: %4luKB %3lums (%.1f fps)\r\n",
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
    char json[512];
    sensor_t *s = esp_camera_sensor_get();
    String ip = WiFi.localIP().toString();

    snprintf(json, sizeof(json),
        "{"
        "\"device\":\"M5Stack Unit CamS3-5MP\","
        "\"sensor_pid\":\"0x%02X\","
        "\"ip\":\"%s\","
        "\"hostname\":\"%s.local\","
        "\"rssi\":%d,"
        "\"uptime_sec\":%lu,"
        "\"free_heap\":%u,"
        "\"free_psram\":%u,"
        "\"framesize\":%d,"
        "\"quality\":%d,"
        "\"stream_url\":\"http://%s/stream\","
        "\"capture_url\":\"http://%s/capture\""
        "}",
        s ? s->id.PID : 0,
        ip.c_str(),
        MDNS_HOSTNAME,
        WiFi.RSSI(),
        (unsigned long)(esp_timer_get_time() / 1000000),
        ESP.getFreeHeap(),
        ESP.getFreePsram(),
        s ? s->status.framesize : -1,
        s ? s->status.quality : -1,
        ip.c_str(),
        ip.c_str()
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json, strlen(json));
}
