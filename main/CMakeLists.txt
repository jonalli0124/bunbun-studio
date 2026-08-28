set( SRC_FILES
    "main.c"
    "alac_magic_cookie.c"
    "settings.c"
    "audio/audio_receiver.c"
    "audio/audio_stream.c"
    "audio/audio_stream_realtime.c"
    "audio/audio_stream_buffered.c"
    "audio/audio_decoder.c"
    "audio/audio_buffer.c"
    "audio/audio_timing.c"
    "audio/audio_crypto.c"
    "audio/audio_resample.c"
    "rtsp/rtsp_server.c"
    "rtsp/rtsp_conn.c"
    "rtsp/rtsp_message.c"
    "rtsp/rtsp_handlers.c"
    "rtsp/rtsp_fairplay.c"
    "rtsp/rtsp_crypto.c"
    "rtsp/rtsp_rsa.c"
    "rtsp/rtsp_events.c"
    "plist/plist_xml.c"
    "plist/bplist_parser.c"
    "plist/bplist_builder.c"
    "plist/base64.c"
    "hap/hap.c"
    "hap/hap_pair_verify.c"
    "hap/hap_pair_setup.c"
    "hap/hap_crypto.c"
    "hap/srp.c"
    "hap/tlv8.c"
    "network/wifi.c"
    "network/mdns_airplay.c"
    "network/ptp_clock.c"
    "network/ntp_clock.c"
    "network/socket_utils.c"
    "network/wall_clock.c"
    "network/web_server.c"
    "network/dns_server.c"
    "network/log_stream.c"
    "led.c"
    "audio/eq_events.c"
    "buttons.c"
    "playback_control.c"
    "dacp_client.c"
)

# ---- BUNBUN_FIRMWARE_OTA: who may write the app partition ----------------
#
# Firmware OTA is the fleet's alone. A public image has NO network path to the
# firmware: the handler, its URI registration and ota.c itself are all absent,
# not merely refused. Absent beats disabled - there is nothing to reach and
# nothing to get wrong - and it is what makes the "no /api/ota/update string in
# the .bin" release check a real check. Public units are updated over USB.
#
# On a fleet unit the endpoint requires that unit's own key (X-Bunbun-Key, set
# via POST /api/ota/key) and refuses everything until one is provisioned. This
# is the CI-2608 blocker: ota.c verifies a SHA-256 the uploader appended itself,
# which proves the bytes arrived intact and NOTHING about who sent them, so
# without the key any device on the wifi could flash any bunbun.
#
# DELIBERATELY NOT A KCONFIG OPTION. Kconfig would route this through
# sdkconfig, and sdkconfig overrides the defaults files for every symbol it
# already contains - so the first fleet build would write CONFIG_..._OTA=y into
# the tracked sdkconfig and EVERY LATER PUBLIC BUILD WOULD SILENTLY KEEP THE
# ENDPOINT. That is precisely how BUNBUN_PUBLIC_BUILD came to mean its own
# opposite for weeks. A CMake cache variable has no merge semantics, is passed
# explicitly on every build (0 or 1, never omitted), and forces a reconfigure
# when it changes, so the image cannot disagree with the command that made it.
if(BUNBUN_FLEET)
    list(APPEND SRC_FILES "network/ota.c")
endif()

# Select audio output implementation at compile time
if(CONFIG_AUDIO_OUTPUT_SPDIF)
    list(APPEND SRC_FILES "audio/audio_output_spdif.c")
elseif(CONFIG_AUDIO_OUTPUT_USB)
    list(APPEND SRC_FILES "audio/audio_output_usb.c")
else()
    list(APPEND SRC_FILES "audio/audio_output.c")
    list(APPEND SRC_FILES "audio/wish_recorder.c")
    list(APPEND SRC_FILES "network/wish_uploader.c")
    list(APPEND SRC_FILES "network/fw_update.c")
    list(APPEND SRC_FILES "network/fw_selfcheck.c")
    # W-020 USB card-reader boot mode (self-guards to S3/OTG targets)
    list(APPEND SRC_FILES "usb_msc_mode.c")
endif()

# Bluetooth A2DP sink (only available on ESP32 with Classic BT)
if(CONFIG_BT_A2DP_ENABLE)
    list(APPEND SRC_FILES "audio/a2dp_sink.c")
    list(APPEND SRC_FILES "bt_coex.c")
endif()

# W5500 SPI Ethernet
if(CONFIG_ETH_W5500_ENABLED)
    list(APPEND SRC_FILES "network/ethernet.c")
endif()

set( INC_DIRS
    "." 
    "audio" 
    "rtsp" 
    "plist" 
    "hap" 
    "network"
)

set( DEPS
    driver
    nvs_flash
    esp_wifi
    esp_netif
    esp_event
    esp_ringbuf
    esp_driver_i2s
    esp_timer
    esp_http_server
    esp_http_client
    app_update
    json
    mbedtls
    esp_driver_ledc
    boards
    dac
    dac_tas58xx
    display
    audio-resampler
    esp_eth
    spiffs_storage
)

# TODO: why these fall over in CI and fine locally...
# Bluetooth component only needed when A2DP is enabled
#if(CONFIG_BT_A2DP_ENABLE)
    list(APPEND DEPS bt)
#endif()

# Ethernet component only needed when W5500 is enabled
#if(CONFIG_ETH_W5500_ENABLED)
    list(APPEND DEPS esp_eth)
#endif()

idf_component_register(
    SRCS ${SRC_FILES}
    INCLUDE_DIRS ${INC_DIRS}
    PRIV_REQUIRES ${DEPS} bunbun dac_es8311 usb_msc_bunbun
)

# The C side of BUNBUN_FLEET (see the note above the ota.c source list). Sources
# guarded by #if BUNBUN_FIRMWARE_OTA see 0 when this is not defined, which is the
# behaviour we want for every build that did not explicitly ask to be a fleet
# image.
if(BUNBUN_FLEET)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BUNBUN_FIRMWARE_OTA=1)
    message(STATUS "bunbun: FLEET image - /api/ota/update present, key required")
else()
    message(STATUS "bunbun: PUBLIC image - no firmware OTA endpoint")
endif()

# Enable function wrapping for abort() to intercept panics and mute GPIO
if(CONFIG_BOARD_SQUEEZEAMP_COMMON)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=abort")
endif()
