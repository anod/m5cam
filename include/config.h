// ============================================================
// Wi-Fi & Device Configuration
// Edit these values before uploading!
// ============================================================

#pragma once

// Wi-Fi credentials
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// mDNS hostname — device will be reachable at http://m5cam.local
#define MDNS_HOSTNAME "m5cam"

// Camera settings
#define CAMERA_FRAME_SIZE   FRAMESIZE_VGA   // 640x480 (good balance for streaming)
#define CAMERA_JPEG_QUALITY 12              // 0-63, lower = better quality, more bandwidth
#define CAMERA_FB_COUNT     2               // Frame buffer count (2 for smoother streaming)
#define CAMERA_XCLK_FREQ    20000000        // 20 MHz

// Web server port
#define HTTP_PORT 80
