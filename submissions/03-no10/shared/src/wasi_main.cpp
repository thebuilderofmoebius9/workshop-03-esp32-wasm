/*
 * wasi_main.cpp — the WASI entry point. Reads a GIF from stdin, decodes every
 * frame with gifcore, and writes the frames as a Netpbm P6 (PPM) stream to
 * stdout; a per-frame log goes to stderr. Pure stdin/stdout, so it runs under
 * any WASI host with no filesystem preopen:
 *
 *   wasmtime gifdec.wasm < bufo.gif > frames.ppm      # all frames (P6 stream)
 *   wasmtime --env GIF_FRAME=0 gifdec.wasm < bufo.gif > frame0.ppm   # one frame
 *
 * Build: zig c++ -target wasm32-wasi  (see Makefile target `wasi`).
 */
#include "gifcore.h"
#include <stdio.h>
#include <stdlib.h>

static uint8_t *read_all_stdin(long *out_len) {
    size_t cap = 1 << 16, len = 0;
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!buf) return nullptr;
    for (;;) {
        if (len == cap) {
            cap *= 2;
            uint8_t *nb = (uint8_t *)realloc(buf, cap);
            if (!nb) { free(buf); return nullptr; }
            buf = nb;
        }
        size_t n = fread(buf + len, 1, cap - len, stdin);
        len += n;
        if (n == 0) break;
    }
    *out_len = (long)len;
    return buf;
}

static void write_ppm(int w, int h, const uint8_t *rgba) {
    printf("P6\n%d %d\n255\n", w, h);
    for (long i = 0; i < (long)w * h; i++) fwrite(rgba + i * 4, 1, 3, stdout);  // RGBA -> RGB
}

int main(void) {
    long len = 0;
    uint8_t *gif = read_all_stdin(&len);
    if (!gif || len <= 0) { fprintf(stderr, "gifdec: no GIF on stdin\n"); return 1; }
    fprintf(stderr, "gifdec(wasi): read %ld bytes\n", len);

    int rc = gif_open(gif, (int)len);
    if (rc != 0) { fprintf(stderr, "gifdec: open failed rc=%d\n", rc); free(gif); return 2; }

    const int w = gif_width(), h = gif_height();
    fprintf(stderr, "gifdec(wasi): canvas %dx%d\n", w, h);

    const char *only = getenv("GIF_FRAME");
    const int onlyN = only ? atoi(only) : -1;   // -1 = emit all frames

    int frame = 0, r;
    do {
        int delay = 0;
        r = gif_play(&delay);
        if (r < 0) break;
        const bool emit = (onlyN < 0 || onlyN == frame);
        if (emit) write_ppm(w, h, gif_fb());
        fprintf(stderr, "  frame %d  delay=%dms%s\n", frame, delay, emit ? "  [emitted]" : "");
        frame++;
        if (onlyN >= 0 && frame > onlyN) break;
    } while (r == 1 && frame < 100000);

    fprintf(stderr, "gifdec(wasi): decoded %d frame(s), %dx%d\n", frame, w, h);
    gif_close();
    free(gif);
    return 0;
}
