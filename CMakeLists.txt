# AirPlay 2 Receiver for ESP32
cmake_minimum_required(VERSION 3.16)

# Read firmware version from version.txt
#
# file(READ) creates no dependency, so an existing build directory would happily rebuild and
# keep stamping the version it was CONFIGURED with. That makes the one field the fleet ledger
# and /api/system/info are read for — "which build is this unit on" — quietly wrong exactly
# when it matters most: after a bump, on an incremental build, on the way to an OTA.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/version.txt")
file(READ "${CMAKE_SOURCE_DIR}/version.txt" PROJECT_VER)
string(STRIP "${PROJECT_VER}" PROJECT_VER)

# Append the git short hash so every build carries a real identity. Managing the fleet over
# OTA with every unit reporting a bare "0.1.29" — while running visibly different code — made
# "which build is this unit on" unanswerable from /api/system/info. The hash is captured at
# CMake configure time, so after a commit, a build from a stale configure can carry the
# previous hash; a full build (or touching a CMakeLists) reconfigures and refreshes it.
execute_process(COMMAND git rev-parse --short HEAD
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                OUTPUT_VARIABLE BUNBUN_GIT_HASH
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(BUNBUN_GIT_HASH)
  set(PROJECT_VER "${PROJECT_VER}+${BUNBUN_GIT_HASH}")
endif()

# Add components directory
set(EXTRA_COMPONENT_DIRS "${CMAKE_SOURCE_DIR}/components")

# bunbun: Arduino-as-a-component drags in Espressif's RainMaker/Insights telemetry stack as
# transitive dependencies, even with their Arduino libraries deselected. esp_insights embeds a
# certificate through target_add_binary_data, and under PlatformIO that path comes out doubled
# (.pio/build/<env>/.pio/build/<env>/https_server.crt.S), so the build fails on a file that is
# present on disk. Set here rather than via cmake_extra_args because PlatformIO's quoting mangles
# a semicolon-separated CMake list. None of this belongs on a virtual pet anyway.
set(EXCLUDE_COMPONENTS
    espressif__esp_insights
    espressif__esp_diagnostics
    espressif__esp_rainmaker
    # W-020: once tinyusb entered the build graph (bunbun's MSC personality),
    # usb_device_uac became buildable and immediately demanded its own header
    # injection back — but no in-tree build variant uses USB audio output.
    # Excluded until one does; re-include alongside CONFIG_AUDIO_OUTPUT_USB.
    espressif__usb_device_uac)

include($ENV{IDF_PATH}/tools/cmake/version.cmake)
if(IDF_VERSION_MAJOR LESS 5 OR (IDF_VERSION_MAJOR EQUAL 5 AND IDF_VERSION_MINOR LESS 5))
    message(WARNING
        "ESP-IDF ${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH} detected. "
        "This project is developed and tested against ESP-IDF >= 5.5. "
        "Older versions may require workarounds (e.g. for hosted-WiFi Kconfig symbols on ESP32-P4). "
        "Please upgrade with: cd $ENV{IDF_PATH} && git fetch && git checkout v5.5.2 && ./install.sh")
endif()


include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# bunbun: put Arduino's `Serial` on the USB-JTAG port rather than UART0.
#
# Arduino defaults Serial to UART0, which on this board is GPIO43/44 and reaches nothing you can
# read over the USB cable — so every bunbun diagnostic (the I2C scan, the RTC probe, the power
# log, the trace ring) was being written into the void while the IDF's own logs arrived fine over
# USB-JTAG. These are compile definitions rather than Kconfig, and they must apply to the WHOLE
# build: the Arduino core only instantiates HWCDCSerial when it sees them, so setting them for
# one component would leave `Serial` referring to an object that was never created.
idf_build_set_property(COMPILE_DEFINITIONS "ARDUINO_USB_MODE=1" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "ARDUINO_USB_CDC_ON_BOOT=1" APPEND)

project(airplay2-receiver VERSION ${PROJECT_VER})

include(cmake/fix_usb_device_uac_platformio.cmake)

# Build a SPIFFS image from data/ and flash it alongside the firmware.
# Partition label "storage" matches partitions.csv. Used by:
#   - components/dac_tas57xx/ (HybridFlow DAC firmware)
#   - components/display/     (ST7789 background image)
# Flash firmware + SPIFFS together with: idf.py build flash
spiffs_create_partition_image(storage "${CMAKE_SOURCE_DIR}/data" FLASH_IN_PROJECT)
