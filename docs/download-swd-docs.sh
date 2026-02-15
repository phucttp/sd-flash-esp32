#!/bin/bash
# ============================================================
# Download SWD & STM32 Reference Documents
# Run: chmod +x download-swd-docs.sh && ./download-swd-docs.sh
# ============================================================

OUTPUT_DIR="./swd-references"
mkdir -p "$OUTPUT_DIR"

echo "Downloading SWD & STM32 Reference Documents..."
echo "Output directory: $OUTPUT_DIR"
echo ""

# Function to download with progress
download() {
    local url="$1"
    local output="$2"
    local desc="$3"

    echo "Downloading: $desc"
    echo "  -> $output"

    if [ -f "$OUTPUT_DIR/$output" ]; then
        echo "  [SKIP] Already exists"
    else
        if curl -L -o "$OUTPUT_DIR/$output" "$url" 2>/dev/null; then
            echo "  [OK] Downloaded"
        else
            if wget -O "$OUTPUT_DIR/$output" "$url" 2>/dev/null; then
                echo "  [OK] Downloaded"
            else
                echo "  [FAIL] Download failed"
            fi
        fi
    fi
    echo ""
}

# Download documents
download \
    "https://documentation-service.arm.com/static/5f900b1af86e16515cdc0642" \
    "ARM_IHI0031_ADIv5_Debug_Interface.pdf" \
    "[CRITICAL] ARM Debug Interface Specification"

download \
    "https://www.st.com/resource/en/programming_manual/pm0075-stm32f10xxx-flash-memory-microcontrollers-stmicroelectronics.pdf" \
    "PM0075_STM32F10xxx_Flash_Programming.pdf" \
    "[CRITICAL] STM32F10xxx Flash Programming Manual"

download \
    "https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf" \
    "RM0008_STM32F103_Reference_Manual.pdf" \
    "[HIGH] STM32F103 Reference Manual"

download \
    "https://www.st.com/resource/en/datasheet/stm32f103c8.pdf" \
    "DS5319_STM32F103C8_Datasheet.pdf" \
    "[MEDIUM] STM32F103C8 Datasheet"

download \
    "https://www.st.com/resource/en/programming_manual/cd00228163-stm32f10xxx-20xxx-21xxx-l1xxxx-cortex-m3-programming-manual-stmicroelectronics.pdf" \
    "PM0056_Cortex_M3_Programming_Manual_ST.pdf" \
    "[MEDIUM] Cortex-M3 Programming Manual"

download \
    "https://www.st.com/resource/en/application_note/an2606-stm32-microcontroller-system-memory-boot-mode-stmicroelectronics.pdf" \
    "AN2606_STM32_Boot_Mode.pdf" \
    "[LOW] STM32 Boot Mode"

echo "============================================================"
echo "Download complete!"
echo "Documents saved to: $OUTPUT_DIR"
echo "============================================================"
