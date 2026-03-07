set(srcs "main.cpp"
         "usb_drive/usb_drive.cpp"
         "sd_card/sd_card.cpp"
         "flasher/flasher_common.cpp"
         "flasher/flasher_esp.cpp"
         "flasher/flasher_swd_stm32f4.cpp"
         "flasher/flasher_swd_stm32f1.cpp"
         "file_utils/file_utils.cpp"
         "app_actions/app_actions.cpp"
         "ui_state/ui_state.cpp"
         "flash_log/flash_log.cpp"
         )

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${include_dirs}
                       REQUIRES espressif__arduino-esp32 Adafruit_DAP Adafruit_GFX Adafruit_ST7735 Adafruit_BusIO TftUI
                                wear_levelling fatfs nvs_flash driver esp_tinyusb)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-parameter)
