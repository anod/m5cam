// ============================================================
// Wi-Fi & Device Configuration
// Edit these values before uploading!
// ============================================================

#pragma once

// Wi-Fi credentials (override via .env file — see README)
#ifndef WIFI_SSID
#define WIFI_SSID     "YOUR_WIFI_SSID"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif

// mDNS hostname — device will be reachable at http://<name>.local
#ifndef MDNS_HOSTNAME
#define MDNS_HOSTNAME "m5cam"
#endif

// Camera settings
#define CAMERA_FRAME_SIZE   FRAMESIZE_VGA   // 640x480
#define CAMERA_JPEG_QUALITY 12              // 0-63, lower = better quality, more bandwidth
#define CAMERA_FB_COUNT     2               // Double-buffered for smooth streaming
#define CAMERA_XCLK_FREQ    20000000        // 20 MHz

// Web server port
#define HTTP_PORT 80
