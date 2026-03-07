# TftUI Library - TFT UI Component for ESP32 (ST7735 128x160)
# Port from OledUI (SH1106G) to ST7735 color TFT
# Author: TTP27
idf_component_register(
    SRCS "tft_ui.cpp"
    INCLUDE_DIRS "."
    REQUIRES espressif__arduino-esp32 Adafruit_GFX Adafruit_ST7735
)
