idf_component_register(SRCS "gif_wamr_main.c"
                       PRIV_REQUIRES esp_psram
                       EMBED_FILES "gifapp.wasm")
