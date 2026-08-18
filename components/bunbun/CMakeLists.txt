# bunbun — the pet, as a component of the AirPlay 2 receiver.
#
# main.cpp still carries its Arduino shape (setup()/loop(), TFT_eSPI, Wire, Preferences); the
# host calls bunbun_start() from app_main and it runs as an ordinary FreeRTOS task. That is what
# lets ~4,500 lines of working, debugged game code come across unchanged instead of being
# rewritten against raw IDF.
#
# `dac` is required because the compatibility layer that replaced bunbun's own audio stack talks
# to the host's codec abstraction rather than driving the ES8311 itself.
idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES espressif__arduino-esp32 tft_espi dac esp_wifi esp_netif esp_partition esp_driver_i2c boards chmorgan__esp-libhelix-mp3 esp_adc json)
# Deliberately does NOT require `main`. main requires bunbun (to call bunbun_start), and a
# component cycle would not resolve. Everything bunbun needs from the host comes through the
# `dac` abstraction and the IDF WiFi API instead of reaching into the application.

# bunbun predates this project's -Werror=all and is not going to satisfy it without a pass that
# would touch every file — which is exactly the kind of churn that hides real regressions in the
# diff. Relaxed here only; the host's own code keeps the strict settings.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=unused-variable
    -Wno-error=unused-but-set-variable
    -Wno-error=unused-function
    -Wno-error=misleading-indentation
    -Wno-error=sign-compare
    -Wno-error=char-subscripts
    -Wno-error=format
    -Wno-error=narrowing
    -Wno-error=format-truncation
    -Wno-error=format-overflow
    -Wno-error=maybe-uninitialized)
