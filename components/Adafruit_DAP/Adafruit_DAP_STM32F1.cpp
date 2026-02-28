/*
 * Adafruit_DAP_STM32F1.cpp — STM32F1 FPEC flash operations via SWD.
 *
 * Key differences from Adafruit_DAP_STM32 (F4):
 *   - Programming unit: half-word (16-bit), CSW toggle required
 *   - Page-based erase (1KB/2KB uniform pages, using FLASH_AR register)
 *   - FLASH_CR bit layout: STRT=bit6, LOCK=bit7 (F4: STRT=bit16, LOCK=bit31)
 *   - Option Bytes: memory-mapped at 0x1FFFF800 (F4: register OPTCR)
 *   - Peripheral registers readable under RDP Level 1
 */

#include "Adafruit_DAP.h"
#include "dap.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "Arduino.h"

//--------------------------------------------------------------------+
// Device whitelist (DEV_ID from DBGMCU_IDCODE bits[11:0])
//--------------------------------------------------------------------+
static struct {
  uint32_t dev_id;
  const char *name;
} _stm32f1_devices[] = {
    {0x412, "STM32F1 Low-density"},
    {0x410, "STM32F1 Medium-density"},
    {0x414, "STM32F1 High-density"},
    {0x418, "STM32F1 Connectivity"},
    {0x420, "STM32F1 VL Medium-density"},
    {0x428, "STM32F1 VL High-density"},
    {0x430, "STM32F1 XL-density"},
};

enum {
  STM32F1_DEVICES_COUNT =
      sizeof(_stm32f1_devices) / sizeof(_stm32f1_devices[0])
};

//--------------------------------------------------------------------+
// Constructor
//--------------------------------------------------------------------+
Adafruit_DAP_STM32F1::Adafruit_DAP_STM32F1(void) {
  memset(&target_device, 0, sizeof(target_device));
  _page_size = 1024;
}

//--------------------------------------------------------------------+
// select() — Halt, reset, identify chip, determine page size
//--------------------------------------------------------------------+
bool Adafruit_DAP_STM32F1::select(uint32_t *found_id) {
  dap_target_prepare();

  // Halt → SYSRESETREQ → re-halt (same pattern as F4 select)
  dap_write_word(STM32F1_DHCSR, 0xA05F0003);
  dap_write_word(STM32F1_DEMCR, 0x00000001);
  dap_write_word(STM32F1_AIRCR, 0x05FA0004);
  delay(15);
  dap_write_word(STM32F1_DHCSR, 0xA05F0003);

  // Read flash size (16-bit at 0x1FFFF7E0, unit = KB)
  uint32_t raw = dap_read_word(STM32F1_FLASHSIZE_REG);
  uint32_t flash_kb = raw & 0xFFFF;
  target_device.flash_size = flash_kb * 1024;

  // Page size: ≤128KB → 1KB, >128KB → 2KB
  _page_size = (flash_kb <= 128) ? 1024 : 2048;

  // Read DBGMCU_IDCODE (DEV_ID bits[11:0])
  uint32_t mcuid = dap_read_word(STM32F1_DBGMCU_IDCODE) & 0xFFF;
  *found_id = mcuid;

  target_device.name = NULL;
  for (int i = 0; i < STM32F1_DEVICES_COUNT; i++) {
    if (mcuid == _stm32f1_devices[i].dev_id) {
      target_device.name = _stm32f1_devices[i].name;
      break;
    }
  }

  return (target_device.name != NULL);
}

//--------------------------------------------------------------------+
// deselect() — Lock flash, release core
//--------------------------------------------------------------------+
void Adafruit_DAP_STM32F1::deselect(void) {
  flash_lock();
  // Release target: clear debug traps → disable debug → reset
  dap_write_word(STM32F1_DEMCR, 0x00000000);   // Clear VC_CORERESET
  dap_write_word(STM32F1_DHCSR, 0xA05F0000);   // Clear C_DEBUGEN + C_HALT
  dap_write_word(STM32F1_AIRCR, 0x05FA0004);   // SYSRESETREQ → normal run
}

//--------------------------------------------------------------------+
// Flash lock/unlock/busy/clear_errors
//--------------------------------------------------------------------+
bool Adafruit_DAP_STM32F1::flash_unlock(void) {
  dap_write_word(STM32F1_FLASH_KEYR, STM32F1_FLASH_KEY1);
  dap_write_word(STM32F1_FLASH_KEYR, STM32F1_FLASH_KEY2);
  // Verify LOCK bit (bit 7) = 0
  uint32_t cr = dap_read_word(STM32F1_FLASH_CR);
  return !(cr & STM32F1_CR_LOCK);
}

void Adafruit_DAP_STM32F1::flash_lock(void) {
  dap_write_word(STM32F1_FLASH_CR, STM32F1_CR_LOCK);
}

bool Adafruit_DAP_STM32F1::flash_busy(void) {
  return dap_read_word(STM32F1_FLASH_SR) & STM32F1_SR_BSY;
}

void Adafruit_DAP_STM32F1::flash_clear_errors(void) {
  dap_write_word(STM32F1_FLASH_SR, STM32F1_SR_ERR_CLEAR);
}

//--------------------------------------------------------------------+
// erase_page() — Erase a single page using FLASH_AR
//--------------------------------------------------------------------+
bool Adafruit_DAP_STM32F1::erase_page(uint32_t page_addr) {
  flash_clear_errors();

  // RM0008 page erase sequence: PER → AR → STRT
  // 1. Set PER in FLASH_CR (must be set BEFORE writing AR)
  dap_write_word(STM32F1_FLASH_CR, STM32F1_CR_PER);
  // 2. Write page address to FLASH_AR
  dap_write_word(STM32F1_FLASH_AR, page_addr);
  // 3. Set PER + STRT to trigger erase
  dap_write_word(STM32F1_FLASH_CR, STM32F1_CR_PER | STM32F1_CR_STRT);

  // Wait BSY
  for (int t = 0; t < 200; t++) {
    if (!flash_busy()) {
      uint32_t sr = dap_read_word(STM32F1_FLASH_SR);
      dap_write_word(STM32F1_FLASH_CR, 0);
      return !(sr & STM32F1_SR_WRPRTERR);
    }
    delay(1);
  }

  dap_write_word(STM32F1_FLASH_CR, 0);
  return false; // timeout
}

//--------------------------------------------------------------------+
// erase() — Mass erase (MER + STRT)
//--------------------------------------------------------------------+
void Adafruit_DAP_STM32F1::erase(void) {
  flash_unlock();
  flash_clear_errors();

  while (flash_busy()) {
    yield();
  }

  dap_write_word(STM32F1_FLASH_CR, STM32F1_CR_MER);
  dap_write_word(STM32F1_FLASH_CR, STM32F1_CR_MER | STM32F1_CR_STRT);

  // F1 mass erase: 3-8 seconds depending on flash size
  while (flash_busy()) {
    delay(100);
  }

  dap_write_word(STM32F1_FLASH_CR, 0);
  flash_lock();
}

//--------------------------------------------------------------------+
// program_start() — Erase pages covering [addr, addr+size)
//--------------------------------------------------------------------+
uint32_t Adafruit_DAP_STM32F1::program_start(uint32_t addr, uint32_t size) {
  uint32_t start_page = (addr - STM32F1_FLASH_START) / _page_size;
  uint32_t end_page = (addr - STM32F1_FLASH_START + size - 1) / _page_size;

  flash_unlock();

  for (uint32_t p = start_page; p <= end_page; p++) {
    uint32_t page_addr = STM32F1_FLASH_START + p * _page_size;
    while (flash_busy()) {
      yield();
    }
    erase_page(page_addr);
    delay(1);
  }

  while (flash_busy()) {
    yield();
  }
  flash_lock();

  return addr;
}

//--------------------------------------------------------------------+
// programBlock() — Write chunk via half-word programming (CSW 16-bit)
//
// Critical F1 differences from F4:
//   1. CSW toggled to 16-bit at start, restored to 32-bit at end
//   2. Each half-word uses replicate pattern for byte lane striping
//   3. NO flash_busy() in inner loop (auto-increment trap)
//--------------------------------------------------------------------+
void Adafruit_DAP_STM32F1::programBlock(uint32_t addr, const uint8_t *buf,
                                         uint32_t size) {
  if (!size)
    return;

  // Clear sticky errors from any previous failed chunk
  dap_write_word(STM32F1_FLASH_SR, STM32F1_SR_ERR_CLEAR);

  // Unlock with verify + retry (matches F4 pattern)
  for (int attempt = 0; attempt < 3; attempt++) {
    flash_unlock();
    dap_write_word(STM32F1_FLASH_CR, STM32F1_CR_PG);

    uint32_t cr = dap_read_word(STM32F1_FLASH_CR);
    if ((cr & STM32F1_CR_PG) && !(cr & STM32F1_CR_LOCK)) {
      break;
    }
    flash_lock();
  }

  // ---- CSW toggle: 32-bit → 16-bit ----
  uint32_t csw_orig = dap_read_reg(SWD_AP_CSW);
  dap_write_reg(SWD_AP_CSW, (csw_orig & ~0x07) | AP_CSW_SIZE_HALFWORD);

  // Write half-words
  uint32_t hw_count = (size + 1) / 2;
  for (uint32_t i = 0; i < hw_count; i++) {
    uint32_t offset = i * 2;
    uint16_t data16;

    if (offset + 1 < size) {
      data16 = buf[offset] | ((uint16_t)buf[offset + 1] << 8);
    } else {
      data16 = buf[offset] | 0xFF00; // pad odd byte
    }

    // Replicate pattern: covers both byte lane positions
    // addr & 2 == 0 → lanes 0-1 (bits[15:0])
    // addr & 2 == 2 → lanes 2-3 (bits[31:16])
    uint32_t safe_data = (uint32_t)data16 | ((uint32_t)data16 << 16);
    dap_write_word(addr + i * 2, safe_data);
    // NO flash_busy() here — SWD bit-bang ~144μs >> F1 flash write ~40-70μs
  }

  // ---- CSW restore: 16-bit → 32-bit ----
  dap_write_reg(SWD_AP_CSW, (csw_orig & ~0x07) | AP_CSW_SIZE_WORD);

  // Clear PG and lock
  dap_write_word(STM32F1_FLASH_CR, 0);
  flash_lock();
}

//--------------------------------------------------------------------+
// verifyFlash() — Read-back and compare (word-by-word)
//--------------------------------------------------------------------+
bool Adafruit_DAP_STM32F1::verifyFlash(uint32_t addr, const uint8_t *data,
                                        uint32_t size) {
  const uint32_t *expected = (const uint32_t *)data;
  uint32_t word_count = (size + 3) / 4;

  for (uint32_t i = 0; i < word_count; i++) {
    uint32_t readback = dap_read_word(addr + i * 4);
    if (readback != expected[i]) {
      return false;
    }
  }
  return true;
}

//--------------------------------------------------------------------+
// programFlash() — programBlock + optional verify
//--------------------------------------------------------------------+
bool Adafruit_DAP_STM32F1::programFlash(uint32_t addr, const uint8_t *buf,
                                         uint32_t count, bool do_verify) {
  programBlock(addr, buf, count);
  if (do_verify) {
    return verifyFlash(addr, buf, count);
  }
  return true;
}

//--------------------------------------------------------------------+
// protectBoot / unprotectBoot — stubs (WRP not yet implemented)
//--------------------------------------------------------------------+
bool Adafruit_DAP_STM32F1::protectBoot(void) { return true; }
bool Adafruit_DAP_STM32F1::unprotectBoot(void) { return true; }
