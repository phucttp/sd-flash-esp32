# set(srcs main.c example_common.c)
set(srcs "main.cpp" "sd_card/sd_card.cpp" "flasher/flasher.cpp" "oled/menu.cpp")

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${include_dirs}
                       REQUIRES espressif__arduino-esp32 Adafruit_GFX Adafruit_SSD1306)


# ⚠️ Thêm dòng này ngay sau idf_component_register
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-parameter)
set(target ${COMPONENT_LIB})

# Embed binaries into the app.
# In ESP-IDF this can also be done using EMBED_FILES option of idf_component_register.
# Here an external tool is used to make file embedding similar with other ports.
# include(${CMAKE_CURRENT_LIST_DIR}/bin2array.cmake)
# # create_resources(${CMAKE_CURRENT_LIST_DIR}/Hello-world ${CMAKE_BINARY_DIR}/binaries.c)
# set_property(SOURCE ${CMAKE_BINARY_DIR}/binaries.c PROPERTY GENERATED 1)
# target_sources(${target} PRIVATE ${CMAKE_BINARY_DIR}/binaries.c)
