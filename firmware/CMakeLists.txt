# ClearBell V0.2 firmware - ESP-IDF project with Arduino-ESP32 as a component (decision R21).
#
# Do not call idf.py directly unless you know the unit-type overlays; use the wrapper:
#   python tools/cb.py bauen aussen      (outdoor unit image)
#   python tools/cb.py bauen innen       (indoor unit image, same for EG and OG)
# The wrapper builds into %USERPROFILE%\clearbell-build\<unit>\ (outside OneDrive) with the matching
# sdkconfig.defaults.<unit> overlay.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The serial console runs over the S3's native USB-Serial-JTAG (GPIO19/20, the only USB on both boards).
# In the Arduino IDE this is the board option "USB CDC On Boot: Enabled"; as an IDF component the
# same switch is these two global defines. They must be visible to the Arduino core itself (where the
# `Serial` object is created), hence a build-wide property and not a per-component definition.
idf_build_set_property(COMPILE_DEFINITIONS "ARDUINO_USB_MODE=1" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "ARDUINO_USB_CDC_ON_BOOT=1" APPEND)

# Firmware version = version.txt plus the unit type as SemVer build metadata, e.g. "0.2.0-dev+aussen". The update
# upload (cb_update) reads it from the image header and refuses an image of the other unit type. CB_UNIT_NAME is
# passed by tools/cb.py (-D CB_UNIT_NAME=aussen|innen).
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/version.txt" CB_VERSION LIMIT_COUNT 1)
if(CB_UNIT_NAME)
    set(PROJECT_VER "${CB_VERSION}+${CB_UNIT_NAME}")
else()
    set(PROJECT_VER "${CB_VERSION}")
endif()

project(clearbell)
