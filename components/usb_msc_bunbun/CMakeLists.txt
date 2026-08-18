# W-020 session 2: bunbun's USB card-reader personality, the hand-rolled way.
#
# Mirrors usb_device_uac's proven trick: inject OUR tusb_config.h and OUR
# descriptors straight into the raw espressif__tinyusb component (which is
# already vendored as uac's dependency). esp_tinyusb was tried first and its
# Kconfig woke the Arduino core's dormant USBMSC.cpp — three USB stacks, one
# build, no survivors. This path coexists because uac proves it does; uac's
# own injection is stood down via CONFIG_USB_DEVICE_UAC_AS_PART.
set(priv_requires esp_timer driver sdmmc)

if(${IDF_VERSION_MAJOR} LESS 6)
    list(APPEND priv_requires usb)       # USB PHY lives in `usb` before 6.0
endif()

# tinyusb is NOT in the build graph by default — usb_device_uac (its usual
# carrier) is trimmed out of non-USB-audio builds, which is exactly why the
# first configure failed with "target not built by this project". Pull it in
# ourselves, S3-only so the ESP32 boards never see it.
if(IDF_TARGET STREQUAL "esp32s3")
    list(APPEND priv_requires espressif__tinyusb)
endif()

idf_component_register(SRCS msc_glue.c
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES ${priv_requires})

if(CONFIG_BUNBUN_USB_MSC)
    idf_build_get_property(build_components BUILD_COMPONENTS)
    if(tinyusb IN_LIST build_components)
        set(tinyusb_name tinyusb)
    else()
        set(tinyusb_name espressif__tinyusb)
    endif()
    idf_component_get_property(tusb_lib ${tinyusb_name} COMPONENT_LIB)
    target_include_directories(${tusb_lib} PUBLIC "${COMPONENT_DIR}/tusb")
    # PRIVATE, not PUBLIC. PUBLIC puts the source into tinyusb's INTERFACE_SOURCES as well as its
    # own, so every component that links tinyusb tries to compile usb_descriptors.c too - with a
    # different flag set. CMake tolerates that; PlatformIO's SCons generator does not, and the
    # build dies with "two environments with different actions for the same target". The
    # descriptors only ever need to be compiled INTO tinyusb, so they are private to it.
    # (cmake/fix_usb_device_uac_platformio.cmake works around the identical mistake in uac.)
    target_sources(${tusb_lib} PRIVATE "${COMPONENT_DIR}/tusb/usb_descriptors.c")
endif()
