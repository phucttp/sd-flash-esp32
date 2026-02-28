/*
 * Adafruit_DAP_STM32F1.h — STM32F1 (Cortex-M3) flash controller via SWD.
 *
 * Inherits Adafruit_DAP directly (NOT Adafruit_DAP_STM32/F4).
 * F1 FPEC registers, bit layout, and programming unit (16-bit half-word)
 * are fundamentally different from F4.
 *
 * See docs/stm32f1-swd-engine-spec.md for register details and flow diagrams.
 */

#ifndef ADAFRUIT_DAP_STM32F1_H_
#define ADAFRUIT_DAP_STM32F1_H_

// ============================================================
// STM32F1 FPEC REGISTERS (Base: 0x40022000)
// ============================================================
#define STM32F1_FLASH_ACR       0x40022000
#define STM32F1_FLASH_KEYR      0x40022004
#define STM32F1_FLASH_OPTKEYR   0x40022008
#define STM32F1_FLASH_SR        0x4002200C
#define STM32F1_FLASH_CR        0x40022010
#define STM32F1_FLASH_AR        0x40022014
#define STM32F1_FLASH_OBR       0x4002201C
#define STM32F1_FLASH_WRPR      0x40022020

// Keys (F1 uses SAME keys for both KEYR and OPTKEYR — unlike F4)
#define STM32F1_FLASH_KEY1      0x45670123
#define STM32F1_FLASH_KEY2      0xCDEF89AB

// ============================================================
// FLASH_SR bits
// ============================================================
#define STM32F1_SR_BSY           (1UL << 0)
#define STM32F1_SR_PGERR         (1UL << 2)
#define STM32F1_SR_WRPRTERR      (1UL << 4)
#define STM32F1_SR_EOP           (1UL << 5)
// Write-1-to-clear mask: PGERR | WRPRTERR | EOP = 0x34
#define STM32F1_SR_ERR_CLEAR     0x34

// ============================================================
// FLASH_CR bits
// ============================================================
#define STM32F1_CR_PG            (1UL << 0)   // Programming (half-word)
#define STM32F1_CR_PER           (1UL << 1)   // Page Erase
#define STM32F1_CR_MER           (1UL << 2)   // Mass Erase
#define STM32F1_CR_OPTPG         (1UL << 4)   // Option Byte Programming
#define STM32F1_CR_OPTER         (1UL << 5)   // Option Byte Erase
#define STM32F1_CR_STRT          (1UL << 6)   // Start (trigger erase)
#define STM32F1_CR_LOCK          (1UL << 7)   // Lock (1 = FPEC locked)
#define STM32F1_CR_OPTWRE        (1UL << 9)   // Option Write Enable (HW auto-set on unlock)

// ============================================================
// FLASH_OBR bits
// ============================================================
#define STM32F1_OBR_OPTERR       (1UL << 0)   // Option byte complement check fail
#define STM32F1_OBR_RDPRT        (1UL << 1)   // Read Protection active (1 = Level 1)

// ============================================================
// Addresses
// ============================================================
#define STM32F1_FLASHSIZE_REG    0x1FFFF7E0   // 16-bit, value in KB
#define STM32F1_OPT_RDP_ADDR    0x1FFFF800   // Option Bytes memory-mapped
#define STM32F1_RDP_UNLOCK_CODE  0x00A5       // Write to OPT_RDP to disable RDP
#define STM32F1_DBGMCU_IDCODE   0xE0042000   // DEV_ID in bits[11:0]
#define STM32F1_FLASH_START      0x08000000

// Cortex-M3 debug registers (shared with F4)
#define STM32F1_DHCSR            0xE000EDF0
#define STM32F1_DEMCR            0xE000EDFC
#define STM32F1_AIRCR            0xE000ED0C

// CSW SIZE field values (bits[2:0])
#define AP_CSW_SIZE_HALFWORD     0x01   // 16-bit
#define AP_CSW_SIZE_WORD         0x02   // 32-bit

// ============================================================
// CLASS
// ============================================================
class Adafruit_DAP_STM32F1 : public Adafruit_DAP {
public:
  Adafruit_DAP_STM32F1(void);
  ~Adafruit_DAP_STM32F1(void) {};

  //------------- Pure virtual overrides (from Adafruit_DAP) -------------//
  uint32_t getTypeID(void) override { return DAP_TYPEID_STM32; }
  bool select(uint32_t *id) override;
  void deselect(void) override;
  void erase(void) override;
  uint32_t program_start(uint32_t addr, uint32_t size) override;
  void programBlock(uint32_t addr, const uint8_t *buf, uint32_t size) override;
  bool programFlash(uint32_t addr, const uint8_t *buf, uint32_t count,
                    bool do_verify = true) override;
  bool protectBoot(void) override;
  bool unprotectBoot(void) override;

  //------------- F1-specific public methods -------------//
  bool verifyFlash(uint32_t addr, const uint8_t *data, uint32_t size);
  uint32_t getPageSize(void) { return _page_size; }

private:
  uint32_t _page_size;  // 1024 (≤128KB flash) or 2048 (>128KB)

  bool flash_unlock(void);
  void flash_lock(void);
  bool flash_busy(void);
  void flash_clear_errors(void);
  bool erase_page(uint32_t page_addr);
};

#endif /* ADAFRUIT_DAP_STM32F1_H_ */
