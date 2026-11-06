# main - Arduino-based application layer: start sequence, console, modules that touch hardware.
# Hardware-independent logic lives in components/cb_core and is unit-tested on the PC.
idf_component_register(
    SRCS
        "main.cpp"
        "cb_log.cpp"
        "cb_console.cpp"
        "cb_system.cpp"
        "cb_led.cpp"
        "cb_provisioning.cpp"
        "cb_modes.cpp"
        "cb_net.cpp"
        "cb_audio.cpp"
        "cb_mic.cpp"
        "cb_touch.cpp"
        "cb_door.cpp"
        "cb_status.cpp"
        "cb_remote.cpp"
        "cb_update.cpp"
        "cb_setup.cpp"
        "cb_pairing.cpp"
        "cb_phones.cpp"
    INCLUDE_DIRS "."
    REQUIRES cb_core
    PRIV_REQUIRES arduino-esp32 app_update espcoredump esp_driver_gpio nvs_flash esp_partition lwip esp_driver_i2s esp_driver_gptimer esp_timer esp_http_server esp_http_client mbedtls
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)

