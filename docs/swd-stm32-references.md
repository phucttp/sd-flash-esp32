# SWD & STM32 Flash Programming References

> **Project:** ESP32 Multi-Flasher - STM32 Support via SWD
> **Created:** 2026-02-03
> **Status:** Research Phase

---

## Table of Contents

1. [ARM Debug Interface (SWD Protocol)](#1-arm-debug-interface-swd-protocol)
2. [Cortex-M3 Debug Architecture](#2-cortex-m3-debug-architecture)
3. [STM32F103 Flash Programming](#3-stm32f103-flash-programming)
4. [Quick Reference Values](#4-quick-reference-values)
5. [Implementation Checklist](#5-implementation-checklist)

---

## 1. ARM Debug Interface (SWD Protocol)

### 1.1 Primary Document - **MUST READ**

| Document | ARM IHI 0031 - ARM Debug Interface Architecture Specification |
|----------|---------------------------------------------------------------|
| Version | ADIv5.0 to ADIv5.2 |
| Link | https://developer.arm.com/documentation/ihi0031/latest/ |
| PDF | https://documentation-service.arm.com/static/5f900b1af86e16515cdc0642 |

#### Important Chapters:

| Chapter | Title | Priority | Notes |
|---------|-------|----------|-------|
| **Chapter 2** | The Debug Port (DP) | **HIGH** | DP registers, IDCODE, CTRL/STAT |
| **Chapter 4** | Serial Wire Debug | **CRITICAL** | SWD packet format, protocol, timing |
| **Chapter 3** | About Access Ports | **HIGH** | MEM-AP for memory access |
| **Chapter 5** | The Memory Access Port | **HIGH** | TAR, DRW, CSW registers |

#### Key Sections in Chapter 4 (SWD):

```
4.1  About the Serial Wire Debug protocol
4.2  SWD protocol operation
     4.2.1  Connection and line reset sequence  ← MUST READ
     4.2.2  Successful SWD operation            ← MUST READ
     4.2.3  SWD WAIT response
     4.2.4  SWD FAULT response
     4.2.5  SWD protocol error
4.3  SWD request phase
4.4  SWD acknowledge phase
4.5  SWD data transfer phase
4.6  SWD bit timing                              ← Important for implementation
```

### 1.2 ADIv5.1 Supplement

| Document | ARM IHI 0031 Supplement |
|----------|-------------------------|
| Link | https://documentation-service.arm.com/static/5ed643eaca06a95ce53f92aa |
| Content | Multi-drop SWD, protocol extensions |
| Priority | LOW (không cần cho single-target) |

---

## 2. Cortex-M3 Debug Architecture

### 2.1 Cortex-M3 Technical Reference Manual

| Document | DDI0337 - Cortex-M3 TRM |
|----------|-------------------------|
| Version | r2p0 |
| Link | https://developer.arm.com/documentation/ddi0337/latest/ |
| Specific | https://developer.arm.com/documentation/ddi0337/h |

#### Important Chapters:

| Chapter | Title | Priority | Notes |
|---------|-------|----------|-------|
| **Chapter 10** | Debug | **CRITICAL** | Debug registers, halt control |
| **Chapter 11** | Debug Port | **HIGH** | SW-DP implementation |
| **Chapter 12** | System Debug | **MEDIUM** | Optional features |

#### Key Sections in Chapter 10:

```
10.1  About debug
10.2  Debug registers                           ← MUST READ
      10.2.1  Debug Halting Control and Status Register (DHCSR)
      10.2.2  Debug Core Register Selector Register (DCRSR)
      10.2.3  Debug Core Register Data Register (DCRDR)
      10.2.4  Debug Exception and Monitor Control Register (DEMCR)
10.3  Debug system register summary
10.4  Core debug access examples               ← Implementation reference
```

### 2.2 Cortex-M3 Devices Generic User Guide

| Document | DUI0552 |
|----------|---------|
| Link | https://developer.arm.com/documentation/dui0552/latest |
| Priority | MEDIUM |
| Content | High-level debug overview, programmer's model |

#### Important Chapters:

```
Chapter 2: The Cortex-M3 Processor
  2.3.4  Debug                                 ← Overview

Chapter 4: Core Peripherals
  4.5  System control block                    ← VTOR, AIRCR registers
```

### 2.3 Debug Port Details

| Document | DDI0337 - Debug Port Section |
|----------|------------------------------|
| Link | https://developer.arm.com/documentation/ddi0337/e/Debug-Port/About-the-DP |
| Priority | HIGH |
| Content | DP register addresses, SW-DP operation |

---

## 3. STM32F103 Flash Programming

### 3.1 Flash Programming Manual - **MUST READ**

| Document | PM0075 - STM32F10xxx Flash Memory Programming |
|----------|-----------------------------------------------|
| PDF | https://www.st.com/resource/en/programming_manual/pm0075-stm32f10xxx-flash-memory-microcontrollers-stmicroelectronics.pdf |
| Pages | 31 pages |
| Priority | **CRITICAL** |

#### Document Structure:

| Section | Title | Priority | Notes |
|---------|-------|----------|-------|
| **Section 2** | Flash module organization | **HIGH** | Memory map, page sizes |
| **Section 3** | Read operations | MEDIUM | Wait states |
| **Section 4** | Flash programming | **CRITICAL** | Unlock, program, verify |
| **Section 5** | Flash erase | **CRITICAL** | Page erase, mass erase |
| **Section 6** | Option bytes | LOW | Read/write protection |
| **Section 7** | Flash registers | **CRITICAL** | All register definitions |

#### Key Sections in PM0075:

```
4.1  Unlocking the FPEC
     - Write KEY1 (0x45670123) to FLASH_KEYR
     - Write KEY2 (0xCDEF89AB) to FLASH_KEYR
     - Check FLASH_CR.LOCK bit = 0

4.2  Main flash programming
     - Set FLASH_CR.PG bit
     - Write 16-bit half-word to flash address
     - Wait until FLASH_SR.BSY = 0
     - Check FLASH_SR.EOP = 1

5.1  Page erase
     - Set FLASH_CR.PER bit
     - Write page address to FLASH_AR
     - Set FLASH_CR.STRT bit
     - Wait until FLASH_SR.BSY = 0

5.2  Mass erase
     - Set FLASH_CR.MER bit
     - Set FLASH_CR.STRT bit
     - Wait until FLASH_SR.BSY = 0
```

### 3.2 STM32F103 Reference Manual

| Document | RM0008 |
|----------|--------|
| PDF | https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf |
| Pages | 1100+ pages (only need ~50 pages) |
| Priority | **HIGH** |

#### Important Chapters:

| Chapter | Title | Priority | Notes |
|---------|-------|----------|-------|
| **Chapter 2** | Memory and bus architecture | **HIGH** | Memory map |
| **Chapter 3** | Flash memory interface | **CRITICAL** | Detailed flash operations |
| **Chapter 31** | Debug support (DBG) | **HIGH** | DBGMCU registers |

#### Key Sections in RM0008:

```
3.3  Flash program and erase operations
     3.3.1  Unlocking the Flash memory
     3.3.2  Main Flash memory programming
     3.3.3  Flash memory erase
     3.3.4  Flash memory protection

3.4  Flash memory interface registers
     3.4.1  Flash access control register (FLASH_ACR)
     3.4.2  Flash key register (FLASH_KEYR)
     3.4.3  Flash option key register (FLASH_OPTKEYR)
     3.4.4  Flash status register (FLASH_SR)
     3.4.5  Flash control register (FLASH_CR)
     3.4.6  Flash address register (FLASH_AR)

31.6 DBGMCU registers
     31.6.1  MCU device ID code (DBGMCU_IDCODE)  ← Identify chip
```

### 3.3 STM32F103C8 Datasheet

| Document | DS5319 |
|----------|--------|
| PDF | https://www.st.com/resource/en/datasheet/stm32f103c8.pdf |
| Priority | MEDIUM |
| Content | Pinout, electrical specs |

### 3.4 STM32 Documentation Hub

| Resource | STM32F103 Documentation Page |
|----------|------------------------------|
| Link | https://www.st.com/en/microcontrollers-microprocessors/stm32f103/documentation.html |
| Content | All datasheets, app notes, errata |

---

## 4. Quick Reference Values

### 4.1 SWD Protocol Constants

```c
// ============================================================
// SWD LINE RESET SEQUENCE
// ============================================================
// 50+ clock cycles with SWDIO HIGH
// Followed by JTAG-to-SWD switch sequence (if needed)

#define SWD_RESET_CLOCKS        50

// JTAG-to-SWD Switch Sequence (16-bit, LSB first)
#define JTAG_TO_SWD_SEQUENCE    0xE79E

// ============================================================
// SWD PACKET FORMAT
// ============================================================
// Request: Start(1) + APnDP(1) + RnW(1) + A[2:3](2) + Parity(1) + Stop(1) + Park(1)
// ACK:     ACK[0:2](3)
// Data:    DATA[0:31](32) + Parity(1)

// Request bits
#define SWD_START_BIT           1
#define SWD_STOP_BIT            0
#define SWD_PARK_BIT            1

// APnDP bit
#define SWD_DP_ACCESS           0
#define SWD_AP_ACCESS           1

// RnW bit
#define SWD_WRITE               0
#define SWD_READ                1

// ACK responses
#define SWD_ACK_OK              0b001
#define SWD_ACK_WAIT            0b010
#define SWD_ACK_FAULT           0b100

// ============================================================
// DP REGISTER ADDRESSES (A[3:2])
// ============================================================
#define DP_IDCODE               0x00    // Read-only
#define DP_ABORT                0x00    // Write-only
#define DP_CTRL_STAT            0x04    // R/W (when DPBANKSEL=0)
#define DP_SELECT               0x08    // Write-only
#define DP_RDBUFF               0x0C    // Read-only

// CTRL/STAT bits
#define CSYSPWRUPREQ            (1 << 30)
#define CSYSPWRUPACK            (1 << 31)
#define CDBGPWRUPREQ            (1 << 28)
#define CDBGPWRUPACK            (1 << 29)

// ABORT bits
#define DAPABORT                (1 << 0)
#define STKCMPCLR               (1 << 1)
#define STKERRCLR               (1 << 2)
#define WDERRCLR                (1 << 3)
#define ORUNERRCLR              (1 << 4)

// ============================================================
// MEM-AP REGISTER ADDRESSES
// ============================================================
#define AP_CSW                  0x00    // Control/Status Word
#define AP_TAR                  0x04    // Transfer Address Register
#define AP_DRW                  0x0C    // Data Read/Write
#define AP_IDR                  0xFC    // Identification Register

// CSW bits
#define CSW_SIZE_BYTE           0
#define CSW_SIZE_HALFWORD       1
#define CSW_SIZE_WORD           2
#define CSW_ADDRINC_OFF         (0 << 4)
#define CSW_ADDRINC_SINGLE      (1 << 4)
#define CSW_ADDRINC_PACKED      (2 << 4)
#define CSW_DBGSWENABLE         (1 << 31)
```

### 4.2 Cortex-M3 Debug Registers

```c
// ============================================================
// DEBUG REGISTERS (accessed via MEM-AP)
// ============================================================

// Debug Halting Control and Status Register
#define DHCSR                   0xE000EDF0
#define DHCSR_DBGKEY            0xA05F0000  // Must write with this key
#define DHCSR_C_DEBUGEN         (1 << 0)    // Enable halting debug
#define DHCSR_C_HALT            (1 << 1)    // Halt the core
#define DHCSR_C_STEP            (1 << 2)    // Single step
#define DHCSR_C_MASKINTS        (1 << 3)    // Mask interrupts while stepping
#define DHCSR_S_REGRDY          (1 << 16)   // Register ready (read-only)
#define DHCSR_S_HALT            (1 << 17)   // Core is halted (read-only)
#define DHCSR_S_LOCKUP          (1 << 19)   // Core is locked up (read-only)

// Debug Core Register Selector Register
#define DCRSR                   0xE000EDF4
#define DCRSR_REGWNR            (1 << 16)   // 0=read, 1=write
// REGSel values: R0-R15 = 0-15, xPSR=16, MSP=17, PSP=18, etc.

// Debug Core Register Data Register
#define DCRDR                   0xE000EDF8

// Debug Exception and Monitor Control Register
#define DEMCR                   0xE000EDFC
#define DEMCR_TRCENA            (1 << 24)   // Trace enable
#define DEMCR_VC_HARDERR        (1 << 10)   // Halt on hard fault
#define DEMCR_VC_CORERESET      (1 << 0)    // Halt on reset

// ============================================================
// SYSTEM CONTROL BLOCK
// ============================================================
#define AIRCR                   0xE000ED0C  // Application Interrupt and Reset Control
#define AIRCR_VECTKEY           0x05FA0000  // Write key
#define AIRCR_SYSRESETREQ       (1 << 2)    // System reset request
```

### 4.3 STM32F103 Flash Registers

```c
// ============================================================
// FLASH REGISTER BASE
// ============================================================
#define FLASH_BASE              0x40022000

// Register offsets
#define FLASH_ACR               (FLASH_BASE + 0x00)  // Access control
#define FLASH_KEYR              (FLASH_BASE + 0x04)  // Key register
#define FLASH_OPTKEYR           (FLASH_BASE + 0x08)  // Option key
#define FLASH_SR                (FLASH_BASE + 0x0C)  // Status
#define FLASH_CR                (FLASH_BASE + 0x10)  // Control
#define FLASH_AR                (FLASH_BASE + 0x14)  // Address
#define FLASH_OBR               (FLASH_BASE + 0x1C)  // Option byte

// ============================================================
// FLASH UNLOCK KEYS
// ============================================================
#define FLASH_KEY1              0x45670123
#define FLASH_KEY2              0xCDEF89AB

// ============================================================
// FLASH_SR (Status Register) bits
// ============================================================
#define FLASH_SR_BSY            (1 << 0)    // Busy
#define FLASH_SR_PGERR          (1 << 2)    // Programming error
#define FLASH_SR_WRPRTERR       (1 << 4)    // Write protection error
#define FLASH_SR_EOP            (1 << 5)    // End of operation

// ============================================================
// FLASH_CR (Control Register) bits
// ============================================================
#define FLASH_CR_PG             (1 << 0)    // Programming
#define FLASH_CR_PER            (1 << 1)    // Page erase
#define FLASH_CR_MER            (1 << 2)    // Mass erase
#define FLASH_CR_OPTPG          (1 << 4)    // Option byte programming
#define FLASH_CR_OPTER          (1 << 5)    // Option byte erase
#define FLASH_CR_STRT           (1 << 6)    // Start
#define FLASH_CR_LOCK           (1 << 7)    // Lock

// ============================================================
// FLASH MEMORY MAP (STM32F103C8 - 64KB)
// ============================================================
#define STM32_FLASH_BASE        0x08000000
#define STM32_FLASH_SIZE        (64 * 1024)     // 64KB
#define STM32_PAGE_SIZE         1024            // 1KB per page
#define STM32_PAGE_COUNT        64              // 64 pages

// ============================================================
// SRAM
// ============================================================
#define STM32_SRAM_BASE         0x20000000
#define STM32_SRAM_SIZE         (20 * 1024)     // 20KB

// ============================================================
// DEVICE ID
// ============================================================
#define DBGMCU_IDCODE           0xE0042000
// Expected values:
// 0x20036410 - STM32F103 medium-density (C8, CB)
// 0x20016410 - STM32F103 medium-density (alternate)

// Cortex-M3 IDCODE (via SWD DP)
#define CORTEX_M3_IDCODE        0x1BA01477
```

### 4.4 Hardware Connections

```
ESP32-C3 (Host)              STM32F103 (Target)
─────────────────────────────────────────────────
GPIO 2  ◄─────────────────►  SWDIO (PA13)
        (bidirectional)

GPIO 3  ─────────────────►   SWCLK (PA14)
        (output)

GPIO 4  ─────────────────►   NRST
        (output, active low)

GND     ─────────────────    GND
3.3V    ─────────────────    VDD (if powering target)
```

---

## 5. Implementation Checklist

### Phase 1: SWD Physical Layer
- [ ] Read: ARM IHI 0031 Chapter 4.1-4.2 (SWD basics)
- [ ] Read: ARM IHI 0031 Chapter 4.6 (timing)
- [ ] Implement: GPIO setup
- [ ] Implement: `swd_write_bits()`
- [ ] Implement: `swd_read_bits()`
- [ ] Implement: `swd_turnaround()`
- [ ] Test: Verify clock frequency (~100-500KHz)

### Phase 2: SWD Protocol Layer
- [ ] Read: ARM IHI 0031 Chapter 2 (DP registers)
- [ ] Read: ARM IHI 0031 Chapter 4.2.1 (line reset)
- [ ] Implement: `swd_line_reset()`
- [ ] Implement: `swd_read_dp()`
- [ ] Implement: `swd_write_dp()`
- [ ] Test: Read IDCODE (expect 0x1BA01477)

### Phase 3: Memory Access
- [ ] Read: ARM IHI 0031 Chapter 3, 5 (MEM-AP)
- [ ] Implement: `swd_read_ap()` / `swd_write_ap()`
- [ ] Implement: `swd_mem_read32()` / `swd_mem_write32()`
- [ ] Test: Read/Write SRAM at 0x20000000

### Phase 4: Core Control
- [ ] Read: DDI0337 Chapter 10 (Debug registers)
- [ ] Implement: `swd_halt()` - Write DHCSR
- [ ] Implement: `swd_resume()`
- [ ] Implement: `swd_reset()`
- [ ] Test: Halt, read PC, resume

### Phase 5: STM32 Flash
- [ ] Read: PM0075 Section 4-5 (Flash programming)
- [ ] Read: RM0008 Chapter 3 (Flash interface)
- [ ] Implement: `stm32_flash_unlock()`
- [ ] Implement: `stm32_flash_erase_page()`
- [ ] Implement: `stm32_flash_program()`
- [ ] Test: Erase and program one page

### Phase 6: Integration
- [ ] Implement: `stm32_flasher_flash_file()`
- [ ] Implement: Progress callback
- [ ] Test: Flash full firmware from SD card
- [ ] Test: Verify after flash

---

## 6. Additional Resources

### Open Source Implementations (Reference)

| Project | Description | Link |
|---------|-------------|------|
| DAPLink | ARM official DAP implementation | https://github.com/ARMmbed/DAPLink |
| free-dap | Minimal SWD implementation | https://github.com/ataradov/free-dap |
| Black Magic Probe | Full debugger | https://github.com/blackmagic-debug/blackmagic |
| OpenOCD | Open On-Chip Debugger | https://github.com/openocd-org/openocd |

### ST Tools

| Tool | Description | Link |
|------|-------------|------|
| STM32CubeProgrammer | Official programming tool | https://www.st.com/en/development-tools/stm32cubeprog.html |
| ST-LINK Utility | Legacy tool | https://www.st.com/en/development-tools/stsw-link004.html |

---

## Changelog

| Date | Changes |
|------|---------|
| 2026-02-03 | Initial document created |

