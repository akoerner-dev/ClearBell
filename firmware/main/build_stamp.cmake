# Writes cb_build_stamp.h with the time of the build (FW-B-11). Run by the custom target in main/CMakeLists.txt at
# EVERY build: esp_app_desc's __DATE__ is only compiled when the app_update component itself is rebuilt, so the
# status page and "info" showed an old date after most builds. Only rewritten when the minute changes.
string(TIMESTAMP stamp "%Y-%m-%d %H:%M")
set(text "#pragma once\n#define CB_BUILD_STAMP \"${stamp}\"\n")
if(EXISTS "${OUT}")
    file(READ "${OUT}" old)
endif()
if(NOT "${old}" STREQUAL "${text}")
    file(WRITE "${OUT}" "${text}")
endif()
