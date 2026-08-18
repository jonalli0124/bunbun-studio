# TFT_eSPI, vendored for bunbun's Freenove ESP32-S3 CYD (ILI9341 240x320).
#
# Two changes from the stock file that ships with the library:
#
# 1. REQUIRES the managed Arduino component by its real name. Upstream says `arduino`, which is
#    what the core is called when vendored by hand; installed through the component manager it
#    is `espressif__arduino-esp32`, and the stock name silently fails to resolve.
#
# 2. Configuration is passed as compile definitions rather than a User_Setup.h. TFT_eSPI is
#    normally configured by editing a header inside the library — exactly the kind of edit that
#    disappears on reinstall. bunbun already drives it from build flags in platformio.ini, so
#    those same flags are reproduced here verbatim.
#
# PUBLIC, not PRIVATE: bunbun's own sources include TFT_eSPI.h and must see an identical
# configuration. If the two disagree, the class layout differs between translation units — which
# does not fail to compile, it corrupts at runtime.
idf_component_register(SRCS "TFT_eSPI.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES espressif__arduino-esp32)

# NO target_compile_definitions here. Built as an IDF component, TFT_eSPI configures itself from
# Kconfig (CONFIG_TFT_*) via TFT_config.h and ERRORS OUT on the -D form that platformio.ini
# uses — "Invalid Chip Select pin" and friends. The pin values now live in
# sdkconfig.defaults.freenove-s3, which is also the right place for them in an IDF project.

# This project builds with -Werror=all; TFT_eSPI is third-party code that does not meet that bar
# (misleading indentation around some SPI wait loops). Scoped to this component only, so the
# strictness still applies to everything we actually own.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=misleading-indentation
    -Wno-misleading-indentation
    -Wno-error=unused-but-set-variable
    -Wno-error=sign-compare)
