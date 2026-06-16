/*
 * gifapp.cpp — the on-device wasm REACTOR. Decodes an embedded GIF with the
 * shared gifcore (../gif-wasm/src) and exposes plain wasm32 exports the WAMR
 * host (on the ESP32) calls:
 *
 *   gifapp_run()    -> decode every frame of the embedded GIF; returns frame count.
 *                      gif_fb() then holds the LAST composed RGBA8888 frame.
 *   gifapp_width()  -> canvas width
 *   gifapp_height() -> canvas height
 *   gifapp_fb()     -> the framebuffer's offset in wasm linear memory (the host
 *                      translates it with wasm_runtime_addr_app_to_native()).
 *
 * Reactor model (-mexec-model=reactor): no main(); WAMR runs _initialize then
 * calls the exports. Built with zig (wasm32-wasi) for WASI libc (malloc/free).
 */
#include "gifcore.h"
#include "gif_data.h"   /* unsigned char gif_data[]; unsigned int gif_data_len; */

extern "C" {

int gifapp_run(void) {
    if (gif_open(gif_data, (int)gif_data_len) != 0) return -1;
    int frames = 0, r;
    do {
        r = gif_play(0);
        if (r < 0) break;
        frames++;
    } while (r == 1 && frames < 100000);
    return frames;                 /* gif_fb() now holds the last frame */
}

int gifapp_width(void)  { return gif_width(); }
int gifapp_height(void) { return gif_height(); }

/* wasm32: a pointer IS a 32-bit linear-memory offset. Return it as int; the host
 * passes it to wasm_runtime_addr_app_to_native() to read the RGBA bytes. */
int gifapp_fb(void) { return (int)(__INTPTR_TYPE__)gif_fb(); }

/* One-shot self-test usable from a single wasmtime --invoke (or the host as a
 * smoke check): decode, then pack (width<<16 | height). busy.gif -> 0x00600064
 * (96<<16 | 100) = 6291556. Negative = decode error. */
int gifapp_selftest(void) {
    int f = gifapp_run();
    if (f < 0) return -1;
    return ((gif_width() & 0xFFFF) << 16) | (gif_height() & 0xFFFF);
}

}
