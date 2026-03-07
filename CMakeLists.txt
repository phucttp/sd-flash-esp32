# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)
set(SUPPORTED_TARGETS esp32s3)  # CHIVI build cho ESP32-S3

# Exclude OLED components (da chuyen sang TFT)
set(EXCLUDE_COMPONENTS
    Adafruit_SH110X
    Adafruit_SSD1306
    OledUI
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(CHIVI_FlashPorter)
