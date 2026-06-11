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

// OTA update settings
#ifndef OTA_PORT
#define OTA_PORT 3232
#endif
#ifndef OTA_PASSWORD_HASH
#define OTA_PASSWORD_HASH ""
#endif

// Camera settings
#ifndef CAMERA_FRAME_SIZE
#define CAMERA_FRAME_SIZE   FRAMESIZE_VGA   // 640x480
#endif
#ifndef CAMERA_JPEG_QUALITY
#define CAMERA_JPEG_QUALITY 12              // 0-63, lower = better quality, more bandwidth
#endif
#ifndef CAMERA_FB_COUNT
#define CAMERA_FB_COUNT     2               // Double-buffered for smooth streaming
#endif
#ifndef CAMERA_XCLK_FREQ
#define CAMERA_XCLK_FREQ    20000000        // 20 MHz
#endif

// PY260 ISP settings
// White balance: 0=auto, 1=sunny, 2=cloudy, 3=office, 4=home
#ifndef PY260_WB_MODE_SETTING
#define PY260_WB_MODE_SETTING 3             // Office/fluorescent preset; home over-corrects blue at night
#endif
#ifndef PY260_AGC_MODE_SETTING
#define PY260_AGC_MODE_SETTING 0            // 0=auto, 1=manual
#endif

// Web server port
#define HTTP_PORT 80
