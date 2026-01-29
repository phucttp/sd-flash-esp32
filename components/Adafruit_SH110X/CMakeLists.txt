# Adafruit SH110X Library
# https://github.com/adafruit/Adafruit_SH110X
# MIT License
idf_component_register(SRCS "Adafruit_SH110X.cpp"
                            "Adafruit_SH1106G.cpp"
                            "Adafruit_SH1107.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES espressif__arduino-esp32 Adafruit_GFX)
