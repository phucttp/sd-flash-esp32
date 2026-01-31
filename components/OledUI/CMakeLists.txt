# OledUI Library - OLED UI Component for ESP32
# Author: TTP27
idf_component_register(
    SRCS "oled_ui.cpp"
    INCLUDE_DIRS "."
    REQUIRES espressif__arduino-esp32 Adafruit_GFX Adafruit_SH110X
)
