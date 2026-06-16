# Workshop 03 -- GIF Decoder as WebAssembly

> One decoder, many runtimes: the same GIF decode core compiled to WASM runs
> on an ESP32-S3 (via WAMR) and in your browser (via emscripten).

```
        "Many bodies, one soul"

    +------------------+      +------------------+
    |   Browser WASM   |      |   ESP32-S3 WAMR  |
    |  (emscripten)    |      |  (ESP-IDF + pio)  |
    |  canvas render   |      |  serial log out   |
    +--------+---------+      +--------+---------+
             |                         |
             +----------+--------------+
                        |
              +---------+---------+
              |   shared/         |
              |  gifcore.cpp      |  <-- the "soul"
              |  AnimatedGIF lib  |
              +-------------------+
```

The shared C++ core (`gifcore.cpp` + Larry Bank's AnimatedGIF library) is
compiled **twice** from the exact same source:

| Target | Compiler | Runtime | Output |
|--------|----------|---------|--------|
| Browser | `emcc` (emscripten) | JavaScript + WebAssembly | `gifdec.js` + `gifdec.wasm` |
| ESP32-S3 | `zig c++` (wasm32-wasi reactor) | WAMR 2.4.0 classic interpreter | `gifapp.wasm` embedded in firmware |

Both decode the same 96x100 animated GIF pet sprites. The browser version
renders to a `<canvas>`, the ESP32 version logs decoded pixel values over
serial -- proof that WebAssembly sandboxing works on a microcontroller.

---

## Repository Structure

```
workshop-03-esp32-wasm/
|-- shared/                     # the "soul" -- shared across both targets
|   |-- src/
|   |   |-- gifcore.cpp         # GIF decode front-end (C-linkage exports)
|   |   |-- gifcore.h           # API: gif_open/play/fb/close
|   |   |-- compat.h            # Arduino millis/delay stubs for non-Arduino
|   |   +-- wasi_main.cpp       # standalone WASI CLI (bonus: stdin -> PPM)
|   +-- vendor/
|       +-- AnimatedGIF/        # Larry Bank's AnimatedGIF (vendored)
|           |-- AnimatedGIF.cpp
|           |-- AnimatedGIF.h
|           +-- gif.inl
|
|-- browser-wasm/               # browser target (emscripten)
|   |-- Makefile                # emcc build
|   +-- web/
|       |-- index.html          # pet pack demo UI
|       |-- gifdec.js           # pre-built emscripten glue (rebuild with make)
|       |-- gifdec.wasm         # pre-built wasm module
|       +-- gifs/               # pet sprite packs
|           |-- idle_0.gif, busy.gif, ...    (bufo)
|           |-- cat/            (7 states)
|           |-- cat-orange/     (7 states)
|           |-- cat-pet/        (7 states)
|           +-- clawd/          (8 states)
|
+-- submissions/
    +-- 03-no10/                # our submission (Student 03, Agent No.10)
        |-- esphome/            # ESPHome variant (YAML + custom component)
        +-- platformio/         # ESP32-S3 target (PlatformIO + ESP-IDF)
            |-- platformio.ini          # PlatformIO project config
            |-- CMakeLists.txt          # ESP-IDF top-level cmake
            |-- sdkconfig.defaults      # critical WAMR/PSRAM/flash settings
            |-- partitions.csv          # 8MB flash layout
            |-- main/
            |   |-- gif_wamr_main.c     # WAMR host: load, instantiate, call exports
            |   |-- CMakeLists.txt      # embeds gifapp.wasm via EMBED_FILES
            |   +-- idf_component.yml   # pulls wasm-micro-runtime ^2.4.0
            +-- wasm/
                |-- Makefile            # zig c++ build for the reactor wasm
                |-- gifapp.cpp          # reactor wrapper (calls gifcore, embeds gif_data)
                +-- gif_data.h          # GIF bytes as C array (xxd-style)
```

---

## Quick Start

### Browser WASM (try this first -- no hardware needed)

```bash
# Option A: use the pre-built wasm (fastest)
cd browser-wasm
python3 -m http.server 8011 -d web
# open http://localhost:8011

# Option B: rebuild from source (requires emscripten)
cd browser-wasm
make          # produces web/gifdec.{js,wasm}
make serve    # build + serve at :8011
```

### ESP32-S3 via PlatformIO

```bash
# Step 1: build the wasm reactor module (requires zig)
cd submissions/03-no10/platformio/wasm
make                    # produces gifapp.wasm (~22KB)
cp gifapp.wasm ../main/ # embed it in the firmware

# Step 2: build + flash the ESP32-S3 firmware
cd submissions/03-no10/platformio
pio run                 # compile (downloads WAMR component automatically)
pio run -t upload       # flash to board
pio device monitor      # watch the decode output
```

Expected serial output:
```
I gif-wamr: === gif-wamr === decode a GIF via WebAssembly, on the ESP32
I gif-wamr: embedded gifapp.wasm: 22344 bytes | PSRAM free: 7936 KB
I gif-wamr: WAMR initialized
I gif-wamr: copied wasm -> internal RAM @0x3fc9xxxx
I gif-wamr: module loaded
I gif-wamr: instantiated
I gif-wamr: _initialize ran
I gif-wamr: WAMR decoded: frames=2  96x100  fb@0x10c80
I gif-wamr:   px(0,0)   RGBA = 65,83,53,255
I gif-wamr:   center    RGBA = 90,138,98,255
I gif-wamr: >>> WAMR ran our GIF decoder ON THE ESP32 -- sandboxed wasm, native pixels <<<
```

### WASI CLI (bonus -- decode GIFs from the command line)

```bash
# Build the WASI module (requires zig + wasmtime)
cd shared
zig c++ -target wasm32-wasi -O2 -DNO_SIMD -fno-exceptions -fno-rtti \
  -I vendor/AnimatedGIF -include src/compat.h \
  src/wasi_main.cpp src/gifcore.cpp vendor/AnimatedGIF/AnimatedGIF.cpp \
  -o gifdec.wasm

# Decode a GIF to PPM
wasmtime gifdec.wasm < ../browser-wasm/web/gifs/busy.gif > out.ppm
```

---

## Prerequisites

| Tool | Version | What for |
|------|---------|----------|
| **Python 3** | any | `http.server` for the browser demo |
| **emscripten** (`emcc`) | 3.x+ | building the browser WASM module |
| **zig** | 0.13+ | building the ESP32 reactor WASM module |
| **PlatformIO** (`pio`) | 6.x+ | building + flashing the ESP32 firmware |
| **wasmtime** | 20+ | (optional) running the WASI CLI module |

---

## The 6 Hard-Won Fixes for WAMR on ESP-IDF v6

Getting WAMR to load and run a non-trivial C++ module on the ESP32 required
solving six problems that are not documented anywhere. Here they are, so you
don't have to rediscover them.

### Fix 1: WAMR version -- use 2.4.0, not 1.3.2

WAMR 1.3.2 (the version many tutorials reference) fails to compile on
ESP-IDF v6 because `espidf_file.c` uses POSIX `fstat`/`struct stat` APIs
that do not exist in v6. Version 2.4.0 from the ESP Component Registry
compiles clean.

```yaml
# main/idf_component.yml
dependencies:
  espressif/wasm-micro-runtime: "^2.4.0"
```

### Fix 2: Disable WASI libc

Our wasm module has **zero imports** (no filesystem, no clock, no WASI).
Enabling WASI libc in WAMR pulls in `espidf_file.c` which does not compile
on ESP-IDF v6. Since we do not need it, turn it off:

```
# sdkconfig.defaults
CONFIG_WAMR_ENABLE_LIBC_WASI=n
```

### Fix 3: ROM to RAM copy before wasm_runtime_load

The embedded wasm bytes live in **read-only flash** (`.rodata`). WAMR
modifies the wasm buffer in-place during load (patching, validation).
Writing to flash-mapped memory triggers a **cache error panic** on the
ESP32. Solution: `malloc` + `memcpy` into internal RAM before calling
`wasm_runtime_load`.

```c
// In gif_wamr_main.c
uint8_t *wasm_buf = heap_caps_malloc(wasm_size,
                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
memcpy(wasm_buf, g_wasm_start, wasm_size);
wasm_module_t module = wasm_runtime_load(wasm_buf, wasm_size, err, sizeof(err));
```

### Fix 4: Enable reference types

Zig/LLVM emits the WebAssembly reference-types encoding for `call_indirect`
table indices even in simple modules. WAMR's ESP-IDF component disables
`REF_TYPES` by default, causing load to fail with: *"zero byte expected ...
reference types feature which is disabled"*.

```
# sdkconfig.defaults
CONFIG_WAMR_ENABLE_REF_TYPES=y
```

### Fix 5: -fno-jump-tables (the critical flag)

At `-O2`, LLVM emits `br_table` instructions for switch statements. WAMR's
validator rejects certain `br_table` patterns in stack-polymorphic
(unreachable) code with: *"br_table targets must all use same result type"*.
This is a known WAMR validator gap. The fix is to tell the compiler to
emit if/else chains instead of jump tables:

```makefile
# In submissions/03-no10/platformio/wasm/Makefile
CFLAGS += -fno-jump-tables
```

This is the flag that took the longest to find. Without it, the module
loads fine on wasmtime but crashes on WAMR.

### Fix 6a: Classic interpreter, not fast

WAMR's "fast interpreter" pre-computes a per-function operand-offset table
in bounded space. Large functions (AnimatedGIF's `playFrame` / LZW decoder)
overflow it: *"fast interpreter offset overflow"* at load time. The classic
interpreter has no such limit.

```
# sdkconfig.defaults
CONFIG_WAMR_INTERP_CLASSIC=y
```

### Fix 6b: pthread wrapper for WAMR on ESP-IDF

WAMR's `os_self_thread()` calls `pthread_self()`, which ESP-IDF only
supports from threads created with `pthread_create()` -- not the main
FreeRTOS task. Running WAMR from `app_main()` directly causes an assert:
*"Failed to find current thread ID!"*.

The fix: wrap the entire WAMR sequence in a `pthread_create()`'d thread.

```c
static void *wamr_thread(void *arg) {
    run_wamr();   // all WAMR calls happen here
    return NULL;
}

void app_main(void) {
    pthread_t tid;
    pthread_create(&tid, &attr, wamr_thread, NULL);
    pthread_join(tid, NULL);
}
```

---

## How It Works

### The Shared Core (`gifcore.cpp`)

The decoder front-end wraps Larry Bank's AnimatedGIF library behind a
simple C-linkage API:

```
gif_open(data, len)   -- copy GIF bytes, open, allocate RGBA canvas
gif_play(&delay_ms)   -- decode next frame into canvas, return timing
gif_fb()              -- pointer to the RGBA8888 framebuffer
gif_reset()           -- seek back to frame 0
gif_close()           -- free everything
```

Both targets link the exact same `gifcore.cpp` and `AnimatedGIF.cpp`.

### Browser Path

`emcc` compiles the core into a WASM module + JS glue. The HTML page loads
the module, fetches `.gif` files via `fetch()`, passes bytes to `gif_open`,
and renders frames onto a `<canvas>` using `putImageData`.

Key emcc flags:
- `-sEXPORTED_FUNCTIONS=_gif_open,_gif_play,...` -- expose the C API
- `-sMODULARIZE=1 -sEXPORT_NAME=GifModule` -- ES module pattern
- `-sALLOW_MEMORY_GROWTH=1` -- dynamic heap

### ESP32 Path

`zig c++` compiles the core in **reactor mode** (`-mexec-model=reactor`) --
no `main()`, just exported functions. A tiny `gifapp.cpp` wrapper embeds
the GIF data as a C array (`gif_data.h`) and exposes `gifapp_run()` etc.

Key zig/linker flags:
- `-mexec-model=reactor` -- no main, host calls `_initialize` then exports
- `-fno-jump-tables` -- avoid br_table (Fix 5)
- `-Wl,--initial-memory=524288 --max-memory=2097152` -- 512KB to 2MB heap
- `-Wl,-z,stack-size=131072` -- 128KB shadow stack

The ESP32 host (`gif_wamr_main.c`) uses WAMR to:
1. Load the ~22KB wasm module from flash (copied to RAM first -- Fix 3)
2. Instantiate with 32KB exec stack, linear memory in PSRAM
3. Call `_initialize` (C++ static constructors)
4. Call `gifapp_run()` to decode, read back pixels via
   `wasm_runtime_addr_app_to_native()`

### Memory Layout (ESP32-S3)

```
Internal SRAM (~380KB free)
  +-- wasm module copy (~22KB, for wasm_runtime_load)
  +-- WAMR runtime structs
  +-- exec stack (32KB)

PSRAM (8MB OPI @80MHz)
  +-- wasm linear memory (512KB initial, grows to 2MB)
      |-- gifcore state + AnimatedGIF instance
      |-- GIF data copy (~7KB for busy.gif)
      +-- RGBA framebuffer (96*100*4 = 38KB)
```

---

## Generating gif_data.h

To embed a different GIF in the ESP32 firmware:

```bash
xxd -i your_sprite.gif > submissions/03-no10/platformio/wasm/gif_data.h
# Edit: rename the array to `gif_data` and length to `gif_data_len`
```

Or use Python:
```bash
python3 -c "
import sys; d = open(sys.argv[1],'rb').read()
print('unsigned char gif_data[] = {')
for i in range(0, len(d), 12):
    print('  ' + ', '.join(f'0x{b:02x}' for b in d[i:i+12]) + ',')
print('};')
print(f'unsigned int gif_data_len = {len(d)};')
" your_sprite.gif > submissions/03-no10/platformio/wasm/gif_data.h
```

---

## Credits

- **P'Nat** ([@nazt](https://github.com/nazt)) -- Oracle School teacher,
  original ESP-IDF pet project architecture
- **Larry Bank** -- [AnimatedGIF](https://github.com/bitbank2/AnimatedGIF)
  library (Apache 2.0)
- **Oracle School** -- [school.buildwithoracle.com](https://school.buildwithoracle.com)
- **WAMR** -- [WebAssembly Micro Runtime](https://github.com/bytecodealliance/wasm-micro-runtime)
  by Bytecode Alliance
- Cat sprite packs: CC0 (see each pack's PROVENANCE files)
- Clawd sprites: MIT (Claude mascot)

## License

Workshop educational material. AnimatedGIF library is Apache 2.0.
See `shared/vendor/AnimatedGIF/` for the library's own license terms.
