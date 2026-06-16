/*
 * compat.h — force-included shim so the vendored AnimatedGIF compiles for
 * wasm32-wasi (zig) and wasm32 (emscripten). AnimatedGIF's bSync pacing path
 * references Arduino millis()/delay(); we always call playFrame(bSync=false),
 * so those are compiled-but-never-executed. Stub them (mirrors the ESP-IDF
 * idf_compat.h trick). ARDUINO is undefined here, so AnimatedGIF.h pulls plain
 * <stdio/stdint/...> and we just satisfy the two symbols.
 */
#pragma once
#ifndef ARDUINO
#include <stdint.h>
static inline uint32_t millis(void) { return 0; }
static inline void     delay(uint32_t ms) { (void)ms; }
#endif
