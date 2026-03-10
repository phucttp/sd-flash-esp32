set(srcs "main.cpp"
         "modules/storage/usb_drive/usb_drive.cpp"
         "modules/storage/sd_card/sd_card.cpp"
         "modules/prog/prog_common.cpp"
         "modules/prog/esp32/prog_esp32.cpp"
         "modules/prog/stm32/prog_stm32_f4.cpp"
         "modules/prog/stm32/prog_stm32_f1.cpp"
         "modules/utils/file_utils/file_utils.cpp"
         "modules/ui/app_actions/app_actions.cpp"
         "modules/ui/ui_state/ui_state.cpp"
         "modules/utils/flash_log/flash_log.cpp"
         )

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${include_dirs}
                       REQUIRES espressif__arduino-esp32 Adafruit_DAP Adafruit_GFX Adafruit_ST7735 Adafruit_BusIO TftUI
                                wear_levelling fatfs nvs_flash driver esp_tinyusb)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-parameter)
