# gifapp.wasm — the on-device wasm reactor (decoder + embedded GIF).
# Built for WAMR on ESP32: ZERO imports (no WASI needed), small growable memory.
#
# Key flags (hard-won — see the build notes):
#   -mexec-model=reactor      no main(); host calls _initialize then the exports
#   -z stack-size=131072       128KB shadow stack (zig defaults to a 16MB stack!)
#   --initial-memory=524288    512KB initial linear memory (data sits low)
#   --max-memory=2097152       grows to 2MB — MUST be > initial or dlmalloc's
#                              first malloc (which grows memory) returns NULL
#   --strip-all                ~22KB module

SHARED   = ../../shared
CORE     = gifapp.cpp $(SHARED)/src/gifcore.cpp $(SHARED)/vendor/AnimatedGIF/AnimatedGIF.cpp
# -fno-jump-tables: emit switch statements as if/else branch chains, NOT wasm
# br_table. WAMR's loader rejects LLVM's -O2 br_table in stack-polymorphic
# (unreachable) code with "br_table targets must all use same result type" — a
# known WAMR validator gap. No jump tables → no br_table → loads clean.
CFLAGS   = -O2 -g0 -DNO_SIMD -fno-exceptions -fno-rtti -fno-jump-tables -I . -I $(SHARED)/src -I $(SHARED)/vendor/AnimatedGIF -include $(SHARED)/src/compat.h
EXPORTS  = -Wl,--export=gifapp_run -Wl,--export=gifapp_width -Wl,--export=gifapp_height -Wl,--export=gifapp_fb -Wl,--export=gifapp_selftest
MEM      = -Wl,-z,stack-size=131072 -Wl,--initial-memory=524288 -Wl,--max-memory=2097152

.PHONY: all verify clean
all: gifapp.wasm
gifapp.wasm: gifapp.cpp gif_data.h $(SHARED)/src/gifcore.cpp $(SHARED)/vendor/AnimatedGIF/AnimatedGIF.cpp
	zig c++ -target wasm32-wasi -mexec-model=reactor -Wl,--strip-all $(MEM) $(EXPORTS) $(CFLAGS) $(CORE) -o $@
	@ls -la $@

# decode the embedded GIF under wasmtime — selftest packs (w<<16|h); 6291556 = 96x100
verify: gifapp.wasm
	@echo "selftest (want 6291556 = 96x100): $$(wasmtime run --invoke gifapp_selftest gifapp.wasm 2>/dev/null | tail -1)"
	@echo "gifapp_run frames (want 2):        $$(wasmtime run --invoke gifapp_run gifapp.wasm 2>/dev/null | tail -1)"

clean:
	rm -f gifapp.wasm
