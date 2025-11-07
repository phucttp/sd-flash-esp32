# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)
set(SUPPORTED_TARGETS esp32c3)  # Thêm dòng này cho ESP32-C3

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Thêm Arduino component path
# set(EXTRA_COMPONENT_DIRS "C://Espressif//frameworks//esp-idf-v5.1.6//arduino-esp32")
project(ESP_MultiFlasher)
