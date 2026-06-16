/*
 * gif_wamr_main.c — run our gifdec WASM on the ESP32 via WAMR.
 *
 * Loads the embedded gifapp.wasm (a reactor: exports gifapp_run/width/height/fb/
 * selftest, ZERO imports), instantiates it under the WebAssembly Micro Runtime,
 * calls the exports, translates the returned framebuffer offset to a native
 * pointer with wasm_runtime_addr_app_to_native(), and logs the decoded result
 * over serial. Proof: WAMR decodes our GIF inside a sandbox, on the chip.
 *
 * The wasm linear memory (512KB->2MB) is allocated from PSRAM via the SPIRAM
 * malloc integration. The wasm has no imports, so no WASI/libc registration is
 * needed — WAMR just runs it.
 *
 * IMPORTANT: WAMR's os_self_thread() (espidf_thread.c) calls pthread_self(),
 * which ESP-IDF only supports from a real POSIX thread — the main FreeRTOS task
 * makes it assert ("Failed to find current thread ID!"). WAMR's own comment says
 * "xTaskCreate is not enough, look at product_mini". So the whole load/call
 * sequence runs inside a pthread_create()'d thread (run_wamr below).
 */
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wasm_export.h"

static const char *TAG = "gif-wamr";

/* wasm exec stack (operand + interpreter call frames) and a generous native
 * stack for the pthread the classic interpreter runs on. */
#define WASM_STACK_SIZE   (32 * 1024)
#define PTHREAD_STACK     (32 * 1024)

/* embedded gifapp.wasm (EMBED_FILES in main/CMakeLists.txt) */
extern const uint8_t g_wasm_start[] asm("_binary_gifapp_wasm_start");
extern const uint8_t g_wasm_end[]   asm("_binary_gifapp_wasm_end");

/* call a 0-arg export that returns an i32; returns that value (or `def` on failure) */
static int call_i32(wasm_exec_env_t env, wasm_module_inst_t inst, const char *name, int def) {
    wasm_function_inst_t f = wasm_runtime_lookup_function(inst, name);  /* WAMR 2.x: 2-arg */
    if (!f) { ESP_LOGW(TAG, "export '%s' not found", name); return def; }
    uint32_t argv[1] = {0};
    if (!wasm_runtime_call_wasm(env, f, 0, argv)) {
        ESP_LOGE(TAG, "call '%s' trapped: %s", name, wasm_runtime_get_exception(inst));
        return def;
    }
    return (int)argv[0];
}

/* The full WAMR sequence. Runs on a pthread (see app_main) so os_self_thread()'s
 * pthread_self() works. */
static void run_wamr(void) {
    ESP_LOGI(TAG, "=== gif-wamr === decode a GIF via WebAssembly, on the ESP32");
    uint32_t wasm_size = (uint32_t)(g_wasm_end - g_wasm_start);
    ESP_LOGI(TAG, "embedded gifapp.wasm: %u bytes | PSRAM free: %u KB",
             (unsigned)wasm_size, (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));

    /* 1) init WAMR with the SYSTEM allocator. WAMR's runtime structs must live in
     * internal RAM (an all-PSRAM allocator cache-error-panics in load); the big
     * wasm linear-memory alloc still lands in PSRAM via the SPIRAM_MALLOC config
     * (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL threshold). */
    RuntimeInitArgs init_args;
    memset(&init_args, 0, sizeof(init_args));
    init_args.mem_alloc_type = Alloc_With_System_Allocator;
    if (!wasm_runtime_full_init(&init_args)) { ESP_LOGE(TAG, "wasm_runtime_full_init failed"); return; }
    ESP_LOGI(TAG, "WAMR initialized");

    /* 2) load the module. WAMR may MODIFY the wasm bytes IN PLACE during load and
     * needs them word-aligned in RAM — but our embedded copy is READ-ONLY FLASH
     * (.rodata), and writing flash-mapped memory cache-error-panics. So copy the
     * module into internal RAM first, then load from there. */
    char err[160];
    uint8_t *wasm_buf = heap_caps_malloc(wasm_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!wasm_buf) { ESP_LOGE(TAG, "no internal RAM for wasm copy (%u B)", (unsigned)wasm_size); return; }
    memcpy(wasm_buf, g_wasm_start, wasm_size);
    ESP_LOGI(TAG, "copied wasm -> internal RAM @%p", wasm_buf);
    wasm_module_t module = wasm_runtime_load(wasm_buf, wasm_size, err, sizeof(err));
    if (!module) { ESP_LOGE(TAG, "load failed: %s", err); heap_caps_free(wasm_buf); return; }
    ESP_LOGI(TAG, "module loaded");

    /* 3) instantiate. stack = wasm exec stack; heap = WAMR app-heap (our wasm
     *    manages its own heap inside linear memory, so 0). */
    wasm_module_inst_t inst = wasm_runtime_instantiate(module, WASM_STACK_SIZE, 0, err, sizeof(err));
    if (!inst) { ESP_LOGE(TAG, "instantiate failed: %s", err); wasm_runtime_unload(module); heap_caps_free(wasm_buf); return; }
    ESP_LOGI(TAG, "instantiated");

    wasm_exec_env_t env = wasm_runtime_create_exec_env(inst, WASM_STACK_SIZE);
    if (!env) { ESP_LOGE(TAG, "create_exec_env failed"); goto done; }

    /* 4) reactor init: run C++ static constructors (AnimatedGIF ctor lives here) */
    wasm_function_inst_t f_init = wasm_runtime_lookup_function(inst, "_initialize");
    if (f_init) { wasm_runtime_call_wasm(env, f_init, 0, NULL); ESP_LOGI(TAG, "_initialize ran"); }

    /* 5) decode + read back */
    int frames = call_i32(env, inst, "gifapp_run", -1);
    int w      = call_i32(env, inst, "gifapp_width", 0);
    int h      = call_i32(env, inst, "gifapp_height", 0);
    int fb_off = call_i32(env, inst, "gifapp_fb", 0);

    ESP_LOGI(TAG, "WAMR decoded: frames=%d  %dx%d  fb@0x%x", frames, w, h, fb_off);

    if (frames > 0 && w > 0 && h > 0 && fb_off > 0) {
        uint8_t *fb = (uint8_t *)wasm_runtime_addr_app_to_native(inst, (uint64_t)(uint32_t)fb_off);
        if (fb) {
            uint8_t *p0 = fb;                          /* top-left */
            uint8_t *pc = fb + ((size_t)(h / 2) * w + (w / 2)) * 4;  /* center */
            ESP_LOGI(TAG, "  px(0,0)   RGBA = %d,%d,%d,%d", p0[0], p0[1], p0[2], p0[3]);
            ESP_LOGI(TAG, "  center    RGBA = %d,%d,%d,%d", pc[0], pc[1], pc[2], pc[3]);
            ESP_LOGI(TAG, ">>> WAMR ran our GIF decoder ON THE ESP32 — sandboxed wasm, native pixels <<<");
        } else {
            ESP_LOGE(TAG, "addr_app_to_native returned NULL for fb@0x%x", fb_off);
        }
    } else {
        ESP_LOGE(TAG, "decode failed (frames=%d %dx%d) — check heap/exec-stack/exception", frames, w, h);
    }

    wasm_runtime_destroy_exec_env(env);
done:
    wasm_runtime_deinstantiate(inst);
    wasm_runtime_unload(module);
    heap_caps_free(wasm_buf);
}

static void *wamr_thread(void *arg) {
    (void)arg;
    run_wamr();
    return NULL;
}

void app_main(void) {
    /* USB-Serial-JTAG re-enumerates on reset — the host serial reattaches ~1-2s
     * after boot. Hold here so the one-shot WAMR log sequence below isn't printed
     * into a detached console (otherwise the capture misses everything). */
    for (int i = 5; i > 0; i--) {
        ESP_LOGI(TAG, "boot marker — WAMR run in %d", i);
        vTaskDelay(pdMS_TO_TICKS(600));
    }

    /* WAMR's os_self_thread() -> pthread_self() asserts on the main FreeRTOS task
     * (ESP-IDF only maps pthread-created tasks). Run the whole sequence on a
     * pthread with a generous native stack for the classic interpreter. */
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, PTHREAD_STACK);
    pthread_t tid;
    if (pthread_create(&tid, &attr, wamr_thread, NULL) == 0) {
        pthread_join(tid, NULL);
    } else {
        ESP_LOGE(TAG, "pthread_create failed — cannot run WAMR");
    }
    pthread_attr_destroy(&attr);

    uint32_t beat = 0;
    while (1) {
        if ((beat % 5) == 0)
            ESP_LOGI(TAG, "[%u] alive — internal free %u KB", (unsigned)beat,
                     (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
        beat++;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
