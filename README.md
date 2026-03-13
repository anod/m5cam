# M5Stack Unit CamS3-5MP — WiFi MJPEG Streaming Firmware

Camera firmware for the [M5Stack Unit CamS3-5MP](https://docs.m5stack.com/en/unit/Unit-CAMS3%205MP) (ESP32-S3, PY260 5MP sensor) with Home Assistant integration.

## Features

- **MJPEG streaming** at `/stream`
- **JPEG snapshot** at `/capture`
- **Web UI** at `/` with live viewer
- **JSON status** at `/status`
- **mDNS** — reachable at `http://m5cam.local`
- **Home Assistant** compatible via MJPEG camera integration

## Setup

### 1. Configure WiFi

Create a `.env` file in the project root (copy from `.env.sample`):

```bash
cp .env.sample .env
```

Then edit `.env` with your values:

```
WIFI_SSID=MyNetwork
WIFI_PASSWORD=MyPassword
MDNS_HOSTNAME=m5cam
```

### 2. Build & Upload

#### Arduino IDE

1. Install **esp32 by Espressif Systems** board package (latest version) via Board Manager
2. Select board: **Tools → Board → esp32 → M5UnitCAMS3**
3. Set: **Tools → USB CDC On Boot → Enabled**
4. Set: **Tools → PSRAM → OPI PSRAM**
5. Connect via [Grove2USB-C](https://docs.m5stack.com/en/accessory/Grove2USB-C) adapter
6. Upload

> **Tip:** If upload fails, short G0 and GND pins before powering on to enter download mode.

#### PlatformIO

```bash
pio run -t upload          # build and upload
pio run                    # build only
pio run -t clean           # clean build artifacts
pio run -t clean && pio run -t upload  # clean build + upload
```

### 3. Verify

Open Serial Monitor at 115200 baud. You should see the device IP address. Open it in a browser to access the web UI.

## Home Assistant Integration

Add to your `configuration.yaml`:

```yaml
camera:
  - platform: mjpeg
    name: "M5 CamS3"
    mjpeg_url: "http://m5cam.local/stream"
    still_image_url: "http://m5cam.local/capture"
```

Or use the IP address instead of `m5cam.local` if mDNS doesn't work on your network.

For a live stream card on your dashboard:

```yaml
type: picture-entity
entity: camera.m5_cams3
camera_view: live
```

## API Endpoints

| Endpoint   | Description                  | Content-Type                           |
|------------|------------------------------|----------------------------------------|
| `/`        | Web UI with stream viewer    | `text/html`                            |
| `/capture` | Single JPEG snapshot         | `image/jpeg`                           |
| `/stream`  | MJPEG video stream           | `multipart/x-mixed-replace`           |
| `/status`  | JSON device info             | `application/json`                     |

## Configuration Options (`config.h`)

| Option                | Default          | Description                              |
|-----------------------|------------------|------------------------------------------|
| `WIFI_SSID`           | —                | WiFi network name                        |
| `WIFI_PASSWORD`       | —                | WiFi password                            |
| `MDNS_HOSTNAME`       | `"m5cam"`        | mDNS hostname (`.local`)                 |
| `CAMERA_FRAME_SIZE`   | `FRAMESIZE_VGA`  | Resolution (VGA=640×480)                 |
| `CAMERA_JPEG_QUALITY` | `12`             | JPEG quality (0-63, lower=better)        |
| `CAMERA_FB_COUNT`     | `2`              | Frame buffer count                       |

### Available Frame Sizes

| Constant           | Resolution  |
|--------------------|-------------|
| `FRAMESIZE_QVGA`   | 320×240     |
| `FRAMESIZE_VGA`    | 640×480     |
| `FRAMESIZE_SVGA`   | 800×600     |
| `FRAMESIZE_XGA`    | 1024×768    |
| `FRAMESIZE_HD`     | 1280×720    |
| `FRAMESIZE_SXGA`   | 1280×1024   |
| `FRAMESIZE_UXGA`   | 1600×1200   |
| `FRAMESIZE_QXGA`   | 2048×1536   |
| `FRAMESIZE_QSXGA`  | 2592×1944   |

> Higher resolutions reduce frame rate. VGA is recommended for smooth streaming.

## Hardware Notes

- **Camera sensor:** PY260 (OV5640-compatible), 5MP, fixed focus (0.6m)
- **MCU:** ESP32-S3 with 16MB flash + 8MB OPI PSRAM
- **LED** on GPIO 14 indicates streaming status

## Technical Details

### PY260 JPEG Workaround

The pre-compiled `esp32-camera` library's mega_ccm driver doesn't include proper
timing in the PY260 reset sequence, causing the sensor to stay in raw YUV422 mode
instead of JPEG. This firmware works around the issue by:

1. **Manual SCCB reset** — After `esp_camera_init()`, we reset the PY260 via direct
   SCCB (I2C) register writes with a 100ms hold + 1500ms settle delay (matching
   the M5Stack reference implementation)
2. **Direct register configuration** — Pixel format (JPEG), resolution, and quality
   are set via SCCB register writes to bypass the non-functional driver functions
3. **Patched `cam_hal.c`** — A modified version of the esp32-camera DMA layer with
   a larger JPEG receive buffer (512KB vs the default formula) to handle PY260's
   variable-size JPEG output

### Custom Board Definition

`boards/m5stack-unitcams3.json` defines the correct hardware configuration
(16MB flash, OPI PSRAM) since the stock PlatformIO board definitions use 8MB flash.
