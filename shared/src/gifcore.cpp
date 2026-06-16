/*
 * gifcore.cpp — shared GIF decode front-end (see gifcore.h). Single-instance:
 * one GIF open at a time, behind a file-scope state `g`, so the AnimatedGIF
 * scanline callback can reach the canvas directly (no per-call user pointer).
 */
#include "gifcore.h"
#include <AnimatedGIF.h>
#include <stdlib.h>
#include <string.h>

namespace {

struct Core {
    AnimatedGIF gif;
    uint8_t *data = nullptr;   // copied GIF bytes — AnimatedGIF keeps a pointer into this
    uint8_t *fb   = nullptr;   // RGBA8888 canvas (persistent across frames)
    int      w = 0, h = 0;
    bool     open = false;
};
Core g;

/* One decoded scanline -> RGBA canvas. Compose: transparent pixels keep the
 * previous frame (handles "leave in place" disposal, the common case). RGB565
 * little-endian palette -> RGB888 with low-bit replication. */
void drawCb(GIFDRAW *d) {
    if (!g.fb) return;
    const int y = d->iY + d->y;
    if (y < 0 || y >= g.h) return;
    const uint16_t *pal = d->pPalette;
    const uint8_t  *src = d->pPixels;
    const uint8_t   t   = d->ucTransparent;
    const bool      hasT = d->ucHasTransparency;
    uint8_t *row = g.fb + (size_t)y * g.w * 4;
    for (int x = 0; x < d->iWidth; x++) {
        const int cx = d->iX + x;
        if (cx < 0 || cx >= g.w) continue;
        const uint8_t idx = src[x];
        if (hasT && idx == t) continue;                 // transparent -> compose
        const uint16_t p = pal[idx];                    // host-order RGB565
        uint8_t r = (uint8_t)((p >> 11) & 0x1F); r = (uint8_t)((r << 3) | (r >> 2));
        uint8_t gg= (uint8_t)((p >>  5) & 0x3F); gg= (uint8_t)((gg << 2) | (gg >> 4));
        uint8_t b = (uint8_t)( p        & 0x1F); b = (uint8_t)((b << 3) | (b >> 2));
        uint8_t *px = row + (size_t)cx * 4;
        px[0] = r; px[1] = gg; px[2] = b; px[3] = 255;
    }
}

} // namespace

extern "C" {

int gif_open(const uint8_t *data, int len) {
    gif_close();
    if (!data || len <= 0) return -1;
    g.data = (uint8_t *)malloc((size_t)len);
    if (!g.data) return -1;
    memcpy(g.data, data, (size_t)len);

    g.gif.begin(GIF_PALETTE_RGB565_LE);
    if (!g.gif.open(g.data, len, drawCb)) { free(g.data); g.data = nullptr; return -2; }
    g.w = g.gif.getCanvasWidth();
    g.h = g.gif.getCanvasHeight();
    if (g.w <= 0 || g.h <= 0 || (long)g.w * g.h > 8L * 1024 * 1024) { gif_close(); return -3; }

    g.fb = (uint8_t *)malloc((size_t)g.w * g.h * 4);
    if (!g.fb) { gif_close(); return -1; }
    memset(g.fb, 0, (size_t)g.w * g.h * 4);   // start transparent-black
    g.open = true;
    return 0;
}

int gif_width(void)  { return g.w; }
int gif_height(void) { return g.h; }

int gif_play(int *delay_ms) {
    if (!g.open) return -1;
    int d = 0;
    int r = g.gif.playFrame(false, &d);   // false = don't pace; we drive the timing
    if (delay_ms) *delay_ms = d;
    return r;                              // 1 = more, 0 = last, -1 = error
}

const uint8_t *gif_fb(void) { return g.fb; }

void gif_reset(void) { if (g.open) g.gif.reset(); }

void gif_close(void) {
    if (g.open) { g.gif.close(); g.open = false; }
    if (g.fb)   { free(g.fb);   g.fb   = nullptr; }
    if (g.data) { free(g.data); g.data = nullptr; }
    g.w = g.h = 0;
}

} // extern "C"
