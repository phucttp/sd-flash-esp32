# ============================================================
# Download SWD & STM32 Reference Documents
# Run: powershell -ExecutionPolicy Bypass -File download-swd-docs.ps1
# ============================================================

$OutputDir = ".\swd-references"

# Create output directory
if (!(Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

Write-Host "Downloading SWD & STM32 Reference Documents..." -ForegroundColor Cyan
Write-Host "Output directory: $OutputDir" -ForegroundColor Gray
Write-Host ""

# Document URLs
$docs = @(
    @{
        Name = "ARM_IHI0031_ADIv5_Debug_Interface.pdf"
        Url = "https://documentation-service.arm.com/static/5f900b1af86e16515cdc0642"
        Description = "[CRITICAL] ARM Debug Interface Specification - SWD Protocol"
    },
    @{
        Name = "PM0075_STM32F10xxx_Flash_Programming.pdf"
        Url = "https://www.st.com/resource/en/programming_manual/pm0075-stm32f10xxx-flash-memory-microcontrollers-stmicroelectronics.pdf"
        Description = "[CRITICAL] STM32F10xxx Flash Programming Manual"
    },
    @{
        Name = "RM0008_STM32F103_Reference_Manual.pdf"
        Url = "https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf"
        Description = "[HIGH] STM32F103 Reference Manual (Full)"
    },
    @{
        Name = "DS5319_STM32F103C8_Datasheet.pdf"
        Url = "https://www.st.com/resource/en/datasheet/stm32f103c8.pdf"
        Description = "[MEDIUM] STM32F103C8 Datasheet"
    },
    @{
        Name = "PM0056_Cortex_M3_Programming_Manual_ST.pdf"
        Url = "https://www.st.com/resource/en/programming_manual/cd00228163-stm32f10xxx-20xxx-21xxx-l1xxxx-cortex-m3-programming-manual-stmicroelectronics.pdf"
        Description = "[MEDIUM] Cortex-M3 Programming Manual (ST version)"
    },
    @{
        Name = "AN2606_STM32_Boot_Mode.pdf"
        Url = "https://www.st.com/resource/en/application_note/an2606-stm32-microcontroller-system-memory-boot-mode-stmicroelectronics.pdf"
        Description = "[LOW] STM32 System Memory Boot Mode"
    }
)

# Download each document
$total = $docs.Count
$current = 0

foreach ($doc in $docs) {
    $current++
    $outputPath = Join-Path $OutputDir $doc.Name

    Write-Host "[$current/$total] $($doc.Description)" -ForegroundColor Yellow
    Write-Host "        -> $($doc.Name)" -ForegroundColor Gray

    if (Test-Path $outputPath) {
        Write-Host "        [SKIP] Already exists" -ForegroundColor DarkGray
    } else {
        try {
            Invoke-WebRequest -Uri $doc.Url -OutFile $outputPath -UseBasicParsing
            Write-Host "        [OK] Downloaded" -ForegroundColor Green
        } catch {
            Write-Host "        [FAIL] $($_.Exception.Message)" -ForegroundColor Red
        }
    }
    Write-Host ""
}

# Create README
$readmePath = Join-Path $OutputDir "README.txt"
$readmeContent = @"
SWD & STM32 Reference Documents
===============================
Downloaded: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")

Files:
------
1. ARM_IHI0031_ADIv5_Debug_Interface.pdf
   - ARM Debug Interface Architecture Specification
   - Chapter 4: SWD Protocol (MUST READ)
   - Chapter 2-3: DP/AP Registers

2. PM0075_STM32F10xxx_Flash_Programming.pdf
   - Flash unlock sequence
   - Page erase / Mass erase
   - Programming procedure (16-bit half-word)

3. RM0008_STM32F103_Reference_Manual.pdf
   - Chapter 3: Flash Memory Interface
   - Chapter 31: Debug Support

4. DS5319_STM32F103C8_Datasheet.pdf
   - Pinout, electrical specs

5. PM0056_Cortex_M3_Programming_Manual_ST.pdf
   - Cortex-M3 instruction set
   - Exception handling

6. AN2606_STM32_Boot_Mode.pdf
   - UART bootloader (reference only)

Online Resources:
-----------------
- Cortex-M3 TRM: https://developer.arm.com/documentation/ddi0337/latest/
- ARM DUI0552 User Guide: https://developer.arm.com/documentation/dui0552/latest
- ST Documentation: https://www.st.com/en/microcontrollers-microprocessors/stm32f103/documentation.html
"@

Set-Content -Path $readmePath -Value $readmeContent

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "Download complete!" -ForegroundColor Green
Write-Host "Documents saved to: $OutputDir" -ForegroundColor Gray
Write-Host ""
Write-Host "Note: ARM online docs (Cortex-M3 TRM, User Guide) require" -ForegroundColor Yellow
Write-Host "      browser access. Links saved in README.txt" -ForegroundColor Yellow
Write-Host "============================================================" -ForegroundColor Cyan
