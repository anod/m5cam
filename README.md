# M5Stack Unit CamS3-5MP — WiFi MJPEG Streaming Firmware

Firmware for the [M5Stack Unit CamS3-5MP](https://docs.m5stack.com/en/unit/Unit-CAMS3%205MP) (ESP32-S3, PY260 5MP sensor). Streams MJPEG over WiFi with a built-in web UI and Home Assistant support.

| Endpoint   | Description              |
|------------|--------------------------|
| `/`        | Web UI with live viewer  |
| `/capture` | Single JPEG snapshot     |
| `/stream`  | MJPEG video stream       |
| `/status`  | JSON device info         |
| `/restart` | Reboot the device        |

## Quick Start

### 1. Install PlatformIO

```bash
pip install platformio
```

Ensure `~/.local/bin` is on your PATH. The first build auto-downloads the toolchain (~1 GB).

> **Important:** This project requires the [pioarduino](https://github.com/pioarduino/platform-espressif32) platform (Arduino core 3.x / ESP-IDF 5.x), already configured in `platformio.ini`. The stock `espressif32` platform (ESP-IDF 4.4) will **not** compile `cam_hal.c`.

### 2. Configure WiFi

```bash
cp .env.sample .env   # then edit with your SSID/password
```

### 3. Build & Flash

```bash
pio run                # build only
pio run -t upload      # build + flash
pio device monitor     # serial monitor (115200 baud)
```

> **Tip:** If upload fails, hold G0 to GND while powering on to enter download mode.

### Arduino IDE (alternative)

Install **esp32 by Espressif Systems** board package **v3.x** via Board Manager. Select board **M5UnitCAMS3**, enable **USB CDC On Boot** and **OPI PSRAM**. Copy `.env` values into `include/config.h` manually (the `load_env.py` script is PlatformIO-only).

### WSL USB passthrough

WSL can't see USB devices natively. Install [usbipd-win](https://github.com/dorssel/usbipd-win) on **Windows** (`winget install usbipd`), then:

```powershell
usbipd list                          # find the ESP32-S3 bus ID
usbipd bind --busid <BUSID>          # first time only
usbipd attach --wsl --busid <BUSID>  # re-run after each device reset
```

In WSL, grant serial access once: `sudo usermod -aG dialout $USER` (re-login required).

## Home Assistant

Add to `configuration.yaml`:

```yaml
camera:
  - platform: mjpeg
    name: "M5 CamS3"
    mjpeg_url: "http://m5cam.local/stream"
    still_image_url: "http://m5cam.local/capture"
```

Use the IP address instead of `m5cam.local` if mDNS doesn't work on your network.

## Configuration (`config.h`)

Settings can be overridden via `.env` (see `.env.sample`) or edited directly in `include/config.h`.

| Option                | Default          | Description                        |
|-----------------------|------------------|------------------------------------|
| `WIFI_SSID`           | —                | WiFi network name                  |
| `WIFI_PASSWORD`       | —                | WiFi password                      |
| `MDNS_HOSTNAME`       | `"m5cam"`        | mDNS hostname (`.local`)           |
| `CAMERA_FRAME_SIZE`   | `FRAMESIZE_VGA`  | Resolution (VGA=640×480)           |
| `CAMERA_JPEG_QUALITY` | `12`             | JPEG quality (0–63, lower=better)  |
| `CAMERA_FB_COUNT`     | `2`              | Frame buffer count                 |

Available resolutions: `FRAMESIZE_QVGA` (320×240), `FRAMESIZE_VGA` (640×480), `FRAMESIZE_SVGA` (800×600), `FRAMESIZE_XGA` (1024×768), `FRAMESIZE_HD` (1280×720), `FRAMESIZE_SXGA` (1280×1024), `FRAMESIZE_UXGA` (1600×1200), `FRAMESIZE_QXGA` (2048×1536), `FRAMESIZE_QSXGA` (2592×1944). Higher resolutions reduce frame rate; VGA is recommended for smooth streaming.

## Hardware

- **Sensor:** PY260 (OV5640-compatible), 5MP, fixed focus (0.6m)
- **MCU:** ESP32-S3, 16MB flash, 8MB OPI PSRAM
- **LED:** GPIO 14 — indicates streaming status

## Technical Notes

### PY260 JPEG workaround

The pre-compiled `esp32-camera` mega_ccm driver doesn't reset the PY260 properly, leaving it in raw YUV422 mode. After `esp_camera_init()`, the firmware manually resets the sensor via direct SCCB register writes with specific timing delays, then configures pixel format, resolution, and quality through SCCB registers. A patched `cam_hal.c` increases the DMA receive buffer to 512KB for PY260's variable-size JPEG output.

### Custom board definition

`boards/m5stack-unitcams3.json` defines 16MB flash + OPI PSRAM (stock PlatformIO definitions use 8MB).
