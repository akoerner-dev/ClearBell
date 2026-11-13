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
        "cb_nvs_secure.cpp"
    INCLUDE_DIRS "."
    REQUIRES cb_core
    PRIV_REQUIRES arduino-esp32 app_update bootloader_support espcoredump esp_driver_gpio nvs_flash esp_partition lwip esp_driver_i2s esp_driver_gptimer esp_timer esp_http_server esp_http_client mbedtls
                 nvs_sec_provider efuse esp_security
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)

# Härtung Stufe B (B1): the decision plain/encrypted/migrate must be made where the Arduino core opens the NVS - see
# cb_nvs_secure.h. The test build (tools/cb.py bauen innen-hbtest) uses a software key instead of the eFuse.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=nvs_flash_init")
if(CB_HB_TESTKEY)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CB_HB_TESTKEY=1)
    message(WARNING "ClearBell TEST BUILD with the NVS test key (CB_HB_TESTKEY) - never for a product")
endif()

# Build time for "info" and the status page (FW-B-11): refreshed at every build, see build_stamp.cmake.
set(CB_STAMP_H "${CMAKE_CURRENT_BINARY_DIR}/cb_build_stamp.h")
add_custom_target(cb_build_stamp
    COMMAND ${CMAKE_COMMAND} -DOUT=${CB_STAMP_H} -P ${CMAKE_CURRENT_LIST_DIR}/build_stamp.cmake
    BYPRODUCTS ${CB_STAMP_H}
    VERBATIM)
add_dependencies(${COMPONENT_LIB} cb_build_stamp)
target_include_directories(${COMPONENT_LIB} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

