idf_component_register(
    SRCS
        "dap.cpp"
        "Adafruit_DAP.cpp"
        "Adafruit_DAP_STM32.cpp"
        "Adafruit_DAP_SAM.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        espressif__arduino-esp32  # Quan trọng: Báo cho IDF biết thư viện này cần Arduino Core
)

# Suppress warnings in third-party library code
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-maybe-uninitialized -Wno-unused-parameter)