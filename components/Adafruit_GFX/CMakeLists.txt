# Adafruit GFX Library
# https://github.com/adafruit/Adafruit-GFX-Library
# BSD License
idf_component_register(SRCS "Adafruit_GFX.cpp" "Adafruit_GrayOLED.cpp" "Adafruit_SPITFT.cpp" "glcdfont.c"
                       INCLUDE_DIRS "."
                       REQUIRES espressif__arduino-esp32 Adafruit_BusIO)

