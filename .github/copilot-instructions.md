# Copilot Instructions — M5Stack Unit CamS3-5MP Firmware

## Build Commands

This is a PlatformIO (Arduino framework) project targeting the ESP32-S3.

```bash
pio run                    # build
pio run -t upload          # build + flash to device
pio run -t clean           # clean build artifacts
```

There are no tests or linters configured.

## Architecture

Single-file Arduino firmware (`src/main.cpp`) that runs an ESP-IDF HTTP server on an ESP32-S3 with a PY260 camera sensor. The firmware boots, initializes the camera, connects to WiFi, and serves four HTTP endpoints (`/`, `/capture`, `/stream`, `/status`). The HTTP server runs on its own FreeRTOS task; `loop()` is intentionally empty.

### Key files

- `include/config.h` — user-facing configuration (WiFi, camera resolution, quality). Defaults can be overridden at build time via `.env` file.
- `include/camera_pins.h` — GPIO pin mapping for the CamS3-5MP hardware.
- `src/cam_hal.c` — **patched** copy of the esp32-camera DMA layer. Overrides the pre-compiled library version at link time (via `-Wl,--allow-multiple-definition` in `platformio.ini`). The patch increases the JPEG receive buffer to 512KB for PY260's variable-size output.
- `boards/m5stack-unitcams3.json` — custom PlatformIO board definition (16MB flash, OPI PSRAM) since stock definitions use 8MB flash.
- `load_env.py` — PlatformIO pre-script that reads `.env` and injects values as `-D` build flags.

### PY260 sensor workaround

The pre-compiled `esp32-camera` mega_ccm driver doesn't reset the PY260 properly, leaving it in raw YUV422 mode. After `esp_camera_init()`, the firmware manually resets the sensor via direct SCCB (I2C) register writes with specific timing delays, then configures pixel format, resolution, and quality through SCCB registers. This is the most fragile part of the codebase — changes to camera init order or timing can break JPEG output.

## Conventions

- WiFi credentials and mDNS hostname are configured via a `.env` file (not committed). The `load_env.py` script converts these to C preprocessor defines. `config.h` provides fallback defaults via `#ifndef` guards.
- The HTML web UI is embedded directly in `main.cpp` as a `PROGMEM` C string literal using `R"rawliteral(...)rawliteral"` syntax. The `%%` sequences in CSS are printf-escaped `%` characters (the HTML is formatted with `snprintf` to inject the device IP).
- SCCB register addresses are raw hex values (e.g., `0x0120` for pixel format). These come from the PY260 datasheet / M5Stack reference code and are not abstracted.
- The patched `cam_hal.c` is linked via symbol override — both the patched source and the pre-compiled library define the same symbols, and `--allow-multiple-definition` makes the linker prefer our version.
