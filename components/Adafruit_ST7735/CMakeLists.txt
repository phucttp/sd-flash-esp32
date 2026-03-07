# Adafruit ST7735/ST7789/ST7796S Library
# https://github.com/adafruit/Adafruit-ST7735-Library
# MIT License
idf_component_register(
    SRCS "Adafruit_ST77xx.cpp" "Adafruit_ST7735.cpp" "Adafruit_ST7789.cpp" "Adafruit_ST7796S.cpp"
    INCLUDE_DIRS "."
    REQUIRES espressif__arduino-esp32 Adafruit_GFX
)
