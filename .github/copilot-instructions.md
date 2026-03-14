# Copilot Instructions — M5Stack Unit CamS3-5MP Firmware

## Build

PlatformIO + Arduino framework on ESP32-S3. Uses [pioarduino](https://github.com/pioarduino/platform-espressif32) (Arduino core 3.x / ESP-IDF 5.x) — the stock `espressif32` platform (IDF 4.4) will **not** build. No tests or linters configured.

```bash
pio run                    # build
pio run -t upload          # flash
pio run -t clean           # clean
```

## Architecture

Single-file firmware (`src/main.cpp`): boots → inits camera → connects WiFi → serves HTTP (`/`, `/capture`, `/stream`, `/status`). HTTP server runs on its own FreeRTOS task; `loop()` is empty.

### Key files

| File | Purpose |
|------|---------|
| `src/main.cpp` | All firmware logic, embedded HTML UI |
| `include/config.h` | User config (WiFi, resolution, quality) via `#ifndef` guards |
| `include/camera_pins.h` | GPIO pin mapping |
| `src/cam_hal.c` | Patched esp32-camera DMA layer (512KB JPEG buffer). Overrides pre-compiled lib via `-Wl,--allow-multiple-definition` |
| `boards/m5stack-unitcams3.json` | Custom board def (16MB flash, OPI PSRAM) |
| `load_env.py` | Pre-script: reads `.env` → `-D` build flags |

### PY260 sensor workaround

The esp32-camera mega_ccm driver doesn't reset PY260 properly (stays in YUV422). After `esp_camera_init()`, firmware manually resets via SCCB register writes with specific timing, then configures format/resolution/quality via SCCB. **Most fragile code** — don't change init order or timing.

## Conventions

- WiFi/mDNS config via `.env` file → `load_env.py` → `-D` flags. `config.h` has `#ifndef` fallbacks.
- HTML UI embedded as `PROGMEM` `R"rawliteral(...)"` string. `%%` = escaped `%` for `snprintf`.
- SCCB register addresses are raw hex from PY260 datasheet (e.g., `0x0120`).
- `cam_hal.c` linked via symbol override (`--allow-multiple-definition`).
