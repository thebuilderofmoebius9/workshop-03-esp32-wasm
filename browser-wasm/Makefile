# browser-wasm — build the GIF decoder as a browser WASM module (emcc).
#
#   make            # build web/gifdec.{js,wasm}
#   make serve      # build + serve at http://localhost:8011
#   make clean
#
# Prerequisites: emscripten (emcc) — install via emsdk:
#   git clone https://github.com/emscripten-core/emsdk && cd emsdk
#   ./emsdk install latest && ./emsdk activate latest
#   source emsdk_env.sh

SHARED   = ../shared
VENDOR   = $(SHARED)/vendor/AnimatedGIF
CORE     = $(SHARED)/src/gifcore.cpp $(VENDOR)/AnimatedGIF.cpp
CFLAGS   = -O2 -DNO_SIMD -fno-exceptions -fno-rtti -I $(VENDOR) -include $(SHARED)/src/compat.h
EXPORTS  = _gif_open,_gif_width,_gif_height,_gif_play,_gif_fb,_gif_reset,_gif_close,_malloc,_free

.PHONY: all serve clean

all: web/gifdec.js

web/gifdec.js: $(SHARED)/src/gifcore.h $(SHARED)/src/compat.h $(CORE)
	emcc $(CFLAGS) $(CORE) --no-entry \
	  -sEXPORTED_FUNCTIONS=$(EXPORTS) -sEXPORTED_RUNTIME_METHODS=HEAPU8 \
	  -sALLOW_MEMORY_GROWTH=1 -sMODULARIZE=1 -sEXPORT_NAME=GifModule -o $@
	@ls -la web/gifdec.js web/gifdec.wasm

serve: all
	@echo "open http://localhost:8011/"
	python3 -m http.server 8011 -d web

clean:
	rm -f web/gifdec.js web/gifdec.wasm
