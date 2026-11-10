# cb_core - the hardware-independent heart of the firmware: protocol, sessions, retry automaton, parsers.
#
# Everything in src/ compiles without Arduino or ESP-IDF and is unit-tested on the PC (test/host/).
# src/esp/ holds the few platform bindings (crypto via mbedtls) and is only built for the device.
idf_component_register(
    SRCS
        "src/cb_protocol.cpp"
        "src/cb_crypto_selftest.cpp"
        "src/cb_modes_logic.cpp"
        "src/cb_link.cpp"
        "src/cb_ratelimit.cpp"
        "src/cb_wav.cpp"
        "src/cb_audio_logic.cpp"
        "src/cb_dsp.cpp"
        "src/cb_touch_logic.cpp"
        "src/cb_http_auth.cpp"
        "src/cb_door_log.cpp"
        "src/cb_remote_logic.cpp"
        "src/cb_update_logic.cpp"
        "src/cb_update_sig.cpp"
        "src/cb_setup_logic.cpp"
        "src/cb_pairing_logic.cpp"
        "src/esp/cb_crypto_esp.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES mbedtls esp_hw_support
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
