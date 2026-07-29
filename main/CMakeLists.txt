set(APP_SRCS
    "./hal/hal.cpp"
    "./hal/hal_board.cpp"
    "./hal/hal_buzzer.cpp"
    "./hal/hal_display.cpp"
    "./hal/hal_imu.cpp"
    "./hal/hal_lora.cpp"
    "./hal/hal_mic.cpp"
    "./hal/hal_tf_card.cpp"
    "./hal/hal_touch.cpp"
    "./hal/utils/image/image_utils.cpp"
    "./apps/app_battery/app_battery.cpp"
    "./apps/app_brightness/app_brightness.cpp"
    "./apps/app_buzzer/app_buzzer.cpp"
    "./apps/app_clock/app_clock.cpp"
    "./apps/app_imu/app_imu.cpp"
    "./apps/app_keyboard/app_keyboard.cpp"
    "./apps/app_lora/app_lora.cpp"
    "./apps/app_mic_wave/app_mic_wave.cpp"
    "./apps/app_nfc_scan/app_nfc_scan.cpp"
    "./apps/app_rgb/app_rgb.cpp"
    "./apps/app_shutdown/app_shutdown.cpp"
    "./apps/app_sleep_wake/app_sleep_wake.cpp"
    "./apps/app_tf_card/app_tf_card.cpp"
    "./apps/app_wifi_scan/app_wifi_scan.cpp"
)

set(APP_INCLUDE_DIRS
    "."
    "./assets"
    "./hal"
    "./hal/utils/image"
    "./apps"
)

file(GLOB APP_EMBED_FILES
    assets/*.png
    assets/*.jpg
    assets/*.jpeg
    # Runtime display fonts.
    assets/*.vlw
)

list(APPEND APP_EMBED_FILES
    assets/img_bg_lite.png
    assets/img_logo.png
    assets/img_logo_lite.png
    assets/img_file.png
    assets/img_folder.png
    assets/img_picture.png
)
list(REMOVE_DUPLICATES APP_EMBED_FILES)

idf_component_register(SRCS "main.cpp" ${APP_SRCS}
                    PRIV_REQUIRES espressif__arduino-esp32 M5GFX M5Unified mooncake nvs_flash esp_timer fatfs sdmmc m5stack__m5pm1 m5stack__m5ioe1 M5Unit-NFC M5UnitUnified M5Utility M5HAL Arduino_BMI270_BMM150 RadioLib LGFX_Fonts
                    INCLUDE_DIRS ${APP_INCLUDE_DIRS}
                    EMBED_FILES ${APP_EMBED_FILES})
