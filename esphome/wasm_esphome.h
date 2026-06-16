#pragma once
#include "esphome.h"
#include "wasm_export.h"
#include <pthread.h>

class WASMRunner : public esphome::Component {
 public:
  void setup() override {
    ESP_LOGI("wasm", "WASM setup starting...");
    // Run the WAMR execution loop inside a POSIX thread to satisfy FreeRTOS assertions
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 32768);
    pthread_t tid;
    if (pthread_create(&tid, &attr, &WASMRunner::run_wasm_thread, this) == 0) {
        pthread_detach(tid);
    } else {
        ESP_LOGE("wasm", "Failed to create POSIX thread for WASM execution");
    }
    pthread_attr_destroy(&attr);
  }

  static void *run_wasm_thread(void *arg) {
    WASMRunner *self = (WASMRunner *)arg;
    self->execute();
    return nullptr;
  }

  void execute() {
    RuntimeInitArgs init_args;
    memset(&init_args, 0, sizeof(RuntimeInitArgs));
    init_args.mem_alloc_type = Alloc_With_System_Allocator;
    
    ESP_LOGI("wasm", "Initializing WebAssembly Micro Runtime (WAMR)...");
    if (!wasm_runtime_full_init(&init_args)) {
        ESP_LOGE("wasm", "WAMR initialization failed");
        return;
    }

    ESP_LOGI("wasm", "WAMR initialized successfully. Feed compiled wasm files to load.");
  }
};
