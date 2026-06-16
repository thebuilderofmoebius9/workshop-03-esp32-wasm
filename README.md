# workshop-03-esp32-wasm: WebAssembly (WAMR) on ESP32-S3

This repository contains the source code and configuration files for compiling and running the WebAssembly Micro Runtime (WAMR v2.4.0) on the ESP32-S3 microcontroller under two distinct development systems: **PlatformIO** and **ESPHome**.

Both builds leverage the native **ESP-IDF** framework to load and execute sandboxed WebAssembly (WASM) modules on-device.

---

## 📂 Repository Structure

All submissions are organized by student running number and agent name:

*   **`submissions/03-no10/`** — Our submission folder (Student ID: `03`, Agent: `No.10 X`).
    *   `platformio/` — The PlatformIO project version utilizing the ESP-IDF framework.
        *   `main/gif_wamr_main.c` — The main launcher script that allocates PSRAM/SRAM boundaries, boots WAMR, copies WASM files to RAM, and translates frame offsets.
        *   `wasm/` — The WASM reactor module source code and Zig makefiles.
        *   `platformio.ini` — PlatformIO configuration for compiling WAMR on ESP32-S3dev board.
    *   `esphome/` — The ESPHome configuration version utilizing the native ESP-IDF components manager.
        *   `wasm-esphome.yaml` — Declarative ESPHome YAML config mapping WAMR as a git component dependency.
        *   `wasm_esphome.h` — Custom C++ wrapper defining the `WASMRunner` component running on Core 0.

---

## 🛠️ Compilation Instructions

### 1. PlatformIO Build
Ensure you have PlatformIO Core installed.

```bash
cd submissions/03-no10/platformio
# Compile the project
pio run

# Flash the firmware over USB-Serial-JTAG
pio run -t upload

# Monitor output logs
pio device monitor -b 115200
```

### 2. ESPHome Build
Ensure you have ESPHome installed (`pipx install esphome` or `uvx esphome`).

```bash
cd submissions/03-no10/esphome
# Validate the declarative configuration
esphome config wasm-esphome.yaml

# Compile, flash and run
esphome run wasm-esphome.yaml
```

---
🤖 **No.10 X** (Back-end Dev & Ops, Oracle Council)
