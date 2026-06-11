# Copilot Instructions — M5Stack Unit CamS3-5MP Firmware

## Build

PlatformIO + Arduino framework on ESP32-S3. Uses [pioarduino](https://github.com/pioarduino/platform-espressif32) (Arduino core 3.x / ESP-IDF 5.x) — the stock `espressif32` platform (IDF 4.4) will **not** build. No tests or linters configured.

```bash
pio run                    # build (default env: m5unitcams3, USB)
pio run -t upload          # flash over USB
pio run -t clean           # clean
```

### OTA flashing

A separate env `m5unitcams3_ota` (in `platformio.ini`) uploads over the network via `espota`. The device runs `ArduinoOTA` on `OTA_PORT` (default 3232); `OTA_PASSWORD` from `.env` is compiled as an MD5 hash. Device IP is `192.168.1.100`.

```bash
pio run -e m5unitcams3_ota -t upload --upload-port 192.168.1.100
```

**WSL caveat (important):** OTA from inside WSL **fails** — the device authenticates but the upload callback times out, because WSL's NAT'd vNIC isn't reachable from the camera's LAN. Run `espota.py` from **Windows** instead so its callback server binds to a LAN interface:

```bash
# from WSL, stage + invoke Windows python (espota.py is pure Python)
cp .pio/build/m5unitcams3_ota/firmware.bin /mnt/c/Users/<user>/AppData/Local/Temp/m5ota/
cp ~/.platformio/packages/framework-arduinoespressif32/tools/espota.py /mnt/c/.../m5ota/
powershell.exe -NoProfile -Command "python.exe espota.py -i 192.168.1.100 -p 3232 -a <pass> -f firmware.bin -r"
```

`m5cam.local` (mDNS) does **not** resolve from WSL (no avahi) — always use the IP. A stale/open MJPEG `/stream` connection saturates the HTTP server's sockets, making `/status` and `/capture` time out (device still pings) — close the stream tab to recover.

## Architecture

Single-file firmware (`src/main.cpp`): boots → inits camera → connects WiFi → serves HTTP (`/`, `/capture`, `/stream`, `/status`, `/restart`) + `ArduinoOTA`. HTTP server runs on its own FreeRTOS task; `loop()` runs `ArduinoOTA.handle()` and periodic WiFi/camera health checks.

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

### Sensor identity & color controls

The sensor is the **PY260** (5MP), reporting **PID `0x039E`** — **not** an OV2640 (PID `0x26`). Confirm via `/status` (`sensor_pid`). Consequences for any image-tuning work:

- The standard esp32-camera `sensor_t` ISP setters (`set_saturation`, `set_whitebal`, `set_special_effect`, `set_gainceiling`, etc.) are **mega_ccm no-ops** (return -1) — do **not** use them; they silently do nothing.
- Only **direct SCCB register writes** take effect, and only a few are honored: pixel fmt `0x0120`, resolution `0x0121`, brightness `0x0122`, contrast `0x0123`, saturation `0x0124`, AWB mode `0x0126`, sharpness `0x0128`, quality `0x012A`, AGC `0x0130`.
- Empirically, **AWB mode and saturation writes have little/no visible effect** on the image. Don't expect SCCB color tuning to fix color problems.

### Red night image = IR, not white balance (hardware limitation)

At night the image has a strong red/magenta wash. **Root cause: the CamS3-5MP has no IR-cut filter**, so near-IR floods the PY260's red channel in low light. Note the pattern: well-lit areas render with correct color; only dim areas go red (IR dominates where visible light is weak). This is **not** a white-balance bug and **cannot be fixed in software** — AWB presets, saturation, etc. don't remove IR already on the sensor. The real fix is a **hardware IR-cut filter** over the lens. (For comparison, the AI-Thinker ESP32-CAM/OV2640 has an IR-cut filter in its lens, which is why it looks neutral at night with zero color tuning.)

## Conventions

- WiFi/mDNS config via `.env` file → `load_env.py` → `-D` flags. `config.h` has `#ifndef` fallbacks.
- HTML UI embedded as `PROGMEM` `R"rawliteral(...)"` string. `%%` = escaped `%` for `snprintf`.
- SCCB register addresses are raw hex from PY260 datasheet (e.g., `0x0120`).
- `cam_hal.c` linked via symbol override (`--allow-multiple-definition`).
