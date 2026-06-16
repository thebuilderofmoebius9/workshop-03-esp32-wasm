/*
 * gifcore.h — framework-agnostic GIF decode front-end shared by the WASI CLI
 * (src/wasi_main.cpp) and the browser WASM build (web/index.html via emcc).
 *
 * Wraps the vendored AnimatedGIF decoder: open a GIF from a memory buffer,
 * decode frame-by-frame into a persistent RGBA8888 canvas (transparent pixels
 * compose over the previous frame), and hand the canvas + timing to the caller.
 * No platform deps — the same .o links into wasm32-wasi and wasm32-emscripten.
 *
 * All entry points are C-linkage so emscripten can export them by name
 * (_gif_open, _gif_play, _gif_fb, ...) and the WASI main can call them directly.
 */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Copy `len` bytes of GIF data, open it, allocate + clear the RGBA canvas.
 * Returns 0 on success, <0 on error (bad GIF / OOM). Call gif_close() when done. */
int gif_open(const uint8_t *data, int len);

int gif_width(void);                 /* canvas width  (0 before a successful open) */
int gif_height(void);                /* canvas height                              */

/* Decode the NEXT frame into the canvas. Returns 1 = more frames follow,
 * 0 = this was the last frame, -1 = error. *delay_ms (if non-NULL) = this
 * frame's display duration in ms. */
int gif_play(int *delay_ms);

/* Pointer to the RGBA8888 canvas (width*height*4 bytes), valid until gif_close().
 * In the browser build this is a byte offset into the wasm heap. */
const uint8_t *gif_fb(void);

void gif_reset(void);                /* seek back to frame 0 (loop the animation)  */
void gif_close(void);                /* free the canvas + the copied GIF data      */

#ifdef __cplusplus
}
#endif
