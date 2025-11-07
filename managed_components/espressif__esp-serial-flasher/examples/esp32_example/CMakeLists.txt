cmake_minimum_required(VERSION 3.5)

set(EXTRA_COMPONENT_DIRS ../../)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp-serial-flasher)

# Fix compatibility with ESP-IDF 5.x
if ("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER "4.4")
    # Sửa lại tên component nếu cần (ví dụ: esp_serial_flasher)
    idf_component_get_property(flasher esp_serial_flasher COMPONENT_LIB)

    if (flasher)
        target_compile_options(${flasher}
            PRIVATE
                -Wunused-parameter
                -Wshadow
        )
    endif()
endif()
