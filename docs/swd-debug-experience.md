# SWD STM32 Flasher - Kinh Nghiem Debug

> Tai lieu tong hop tat ca loi da gap khi port Adafruit_DAP (bit-bang SWD) len ESP32-C3
> de nap firmware STM32F4xx tu SD card.
> Platform: ESP32-C3 (RISC-V, single-core) | ESP-IDF v5.1.6 + Arduino component

---

## Muc Luc

1. [GPIO2/FSPIQ - Xung dot pin SPI](#1-gpio2fspiq---xung-dot-pin-spi)
2. [RDP Level 1 - Doc flash tra ve 0](#2-rdp-level-1---doc-flash-tra-ve-0)
3. [noInterrupts() crash ESP32](#3-nointerrupts-crash-esp32)
4. [LED_BUILTIN xung dot I2C SDA](#4-led_builtin-xung-dot-i2c-sda)
5. [PIN_INPUT_ENABLE thieu cho ESP32](#5-pin_input_enable-thieu-cho-esp32)
6. [nRESET=-1 khong duoc guard](#6-nreset-1-khong-duoc-guard)
7. [verifyFlash buffer qua lon (stack overflow)](#7-verifyflash-buffer-qua-lon-stack-overflow)
8. [dap_read_block buffer qua lon](#8-dap_read_block-buffer-qua-lon)
9. [SYSRESETREQ mat SWD sau reset](#9-sysresetreq-mat-swd-sau-reset)
10. [RDP mass erase timing sai](#10-rdp-mass-erase-timing-sai)
11. [Serial.println bi "nuot" - error vo hinh](#11-serialprintln-bi-nuot---error-vo-hinh)
12. [Format string -Werror=format](#12-format-string--werrorformat)
13. [Sticky SWD errors truoc verify](#13-sticky-swd-errors-truoc-verify)
14. [dap_read/write_block luon return true](#14-dap_readwrite_block-luon-return-true)
15. [FLASH_SR error bits khong duoc clear](#15-flash_sr-error-bits-khong-duoc-clear)
16. [~~dap_read_block tra ve zeros~~ DA SUA SAI](#16-dap_read_block-tra-ve-zeros-tren-esp32-c3-da-sua-sai)
17. [dap_write_block bi loi tren ESP32-C3](#17-dap_write_block-cung-bi-loi-tren-esp32-c3-write-zeros)
18. [3x blind write retry khoa flash controller](#18-3x-blind-write-retry-khoa-flash-controller)
19. [CSW AddrInc + flash_busy() ghi vao FLASH_CR](#19-csw-addrinc--flash_busy-ghi-vao-flash_cr)
20. [SPRMOD bit 31 bat PCROP thay vi tat WRP](#20-sprmod-bit-31-bat-pcrop-thay-vi-tat-wrp)
21. [dap_write_word mat du lieu ~1/1000 words](#21-dap_write_word-mat-du-lieu-11000-words)
22. [5V cap nguon STM32 - SWD voltage mismatch](#22-5v-cap-nguon-stm32---swd-voltage-mismatch)
23. [SD.seek(0) khong hoat dong sau EOF](#23-sdseek0-khong-hoat-dong-sau-eof)
24. [static Adafruit_DAP_STM32 chiem RAM vinh vien](#24-static-adafruit_dap_stm32-chiem-ram-vinh-vien)

---

## 1. GPIO2/FSPIQ - Xung Dot Pin SPI

**Muc do**: CRITICAL - mat nhieu ngay debug
**Trieu chung**: SWD connect/select/OPTCR read OK, nhung program_start/programBlock fail voi DAP errors ngay sau khi SD.open()
**Nguyen nhan**: GPIO2 la pin mac dinh cua FSPIQ (SPI2 MISO) tren ESP32-C3. Khi SD card SPI hoat dong, no lam hong trang thai GPIO2, pha hong SWD bit-bang.

```
ESP32-C3 FSPI default pins:
  CLK  = GPIO6
  MOSI = GPIO7 (D)
  MISO = GPIO2 (Q)  <-- XUNG DOT!
  HD   = GPIO4
  WP   = GPIO5
  CS0  = GPIO10
```

**Cach fix**: Doi SWDIO tu GPIO2 sang GPIO0 (khong co FSPI function mac dinh)

**Bai hoc**: Khi dung bit-bang GPIO tren ESP32-C3, TRANH tat ca pin FSPI mac dinh (GPIO2, GPIO4, GPIO5, GPIO6, GPIO7, GPIO10), ke ca khi SPI driver dung pin khac. Hardware mux co the van anh huong.

**File**: `main/flasher/flasher_common.h`
```cpp
// TRUOC (loi):
#define SWD_SWDIO_PIN  GPIO_NUM_2   // = FSPIQ!
// SAU (fix):
#define SWD_SWDIO_PIN  FLASH_PIN_0  // GPIO0, an toan
```

---

## 2. RDP Level 1 - Doc Flash Tra Ve 0

**Muc do**: CRITICAL
**Trieu chung**: SWD reads tu flash (0x08000000) tra ve 0x00000000, nhung doc debug registers (DHCSR, DBGMCU_IDCODE) van OK.
**Nguyen nhan**: Read Data Protection Level 1 (RDP byte = 0x00 trong FLASH_OPTCR) chan tat ca flash/SRAM reads qua debug port.

**Phan biet cac level**:
| RDP byte | Level | Hau qua |
|----------|-------|---------|
| 0xAA     | 0     | Khong bao ve, doc/ghi tu do |
| != 0xAA, != 0xCC | 1 | Chan doc flash/SRAM qua debug, ghi bi ignore |
| 0xCC     | 2     | VINH VIEN khoa debug, KHONG THE MO LAI |

**Cach fix**:
```cpp
// 1. Unlock flash
dap_write_word(0x40023C04, 0x45670123);  // FLASH_KEYR key 1
dap_write_word(0x40023C04, 0xCDEF89AB);  // FLASH_KEYR key 2

// 2. Unlock option bytes
dap_write_word(0x40023C08, 0x08192A3B);  // FLASH_OPTKEYR key 1
dap_write_word(0x40023C08, 0x4C5D6E7F);  // FLASH_OPTKEYR key 2

// 3. Set RDP = 0xAA (Level 0) + OPTSTRT
optcr = (optcr & ~(0xFF << 8)) | (0xAA << 8) | (1 << 1);
dap_write_word(0x40023C14, optcr);       // FLASH_OPTCR
```

**Tac dung phu**: Disable RDP tu Level 1 -> Level 0 se TU DONG mass erase toan bo flash (~8-9 giay)!

**SWD access levels**:
| Loai doc | Bus | RDP1 |
|----------|-----|------|
| IDCODE (DP) | SWD protocol | OK |
| DBGMCU (0xE0042000) | Peripheral/APB | OK |
| Flash (0x08000000) | AHB | CHAN |
| SRAM | AHB | CHAN |
| Option bytes (0x40023C14) | Peripheral/APB | OK |

**File**: `main/flasher/flasher_swd.cpp` - function `swd_check_rdp()`

---

## 3. noInterrupts() Crash ESP32

**Muc do**: HIGH
**Trieu chung**: ESP32 crash/hang khi Adafruit_DAP goi `noInterrupts()` (co trong code goc)
**Nguyen nhan**: ESP32 chay FreeRTOS, `noInterrupts()` tat scheduler -> system crash vi watchdog timer, WiFi task, va cac system tasks khac bi block.

**Cach fix**: XOA tat ca `noInterrupts()` / `interrupts()` trong Adafruit_DAP source.

**Bai hoc**: Tren ESP32 (FreeRTOS), KHONG BAO GIO dung `noInterrupts()` cho thoi gian dai. SWD bit-bang van hoat dong tot ma khong can tat interrupt.

**File**: `components/Adafruit_DAP/dap.cpp` - xoa cac dong `noInterrupts()` va `interrupts()`

---

## 4. LED_BUILTIN Xung Dot I2C SDA

**Muc do**: MEDIUM
**Trieu chung**: OLED I2C khong hoat dong khi Adafruit_DAP init LED
**Nguyen nhan**: Tren ESP32-C3 DevKit, LED_BUILTIN (GPIO8) trung voi I2C SDA cho OLED.

**Cach fix**: Disable tat ca LED operations trong `dap_config.h`:
```cpp
static inline void DAP_CONFIG_LED(int index, int state) {
  (void)index;
  (void)state;
  // LED_BUILTIN disabled - conflicts with OLED I2C SDA on ESP32-C3
}
```

**File**: `components/Adafruit_DAP/dap_config.h`

---

## 5. PIN_INPUT_ENABLE Thieu Cho ESP32

**Muc do**: HIGH
**Trieu chung**: SWD doc du lieu luon tra ve 0 hoac gia tri sai
**Nguyen nhan**: Tren ESP32-C3, `pinMode(OUTPUT)` clear bit FUN_IE (Function Input Enable) trong IO MUX register. Khi SWDIO chuyen tu output sang input (SWD turnaround), input path bi tat -> doc ACK/data sai.

**Cach fix**: Them `PIN_INPUT_ENABLE()` o 2 cho:
1. `DAP_CONFIG_SWDIO_TMS_in()` - khi chuyen SWDIO sang input mode
2. `DAP_CONFIG_CONNECT_SWD()` - khi init SWD connection

```cpp
// Trong SWDIO_TMS_in():
GPIO.enable_w1tc.val = (1UL << DAP_CONFIG_SWDIO_PIN);
PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[DAP_CONFIG_SWDIO_PIN]);  // THEM DONG NAY

// Trong CONNECT_SWD():
PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[DAP_CONFIG_SWDIO_PIN]);
PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[DAP_CONFIG_SWCLK_PIN]);
```

**Bai hoc**: ESP32 GPIO khac ARM (SAMD/nRF): output mode tat input path. Phai enable lai manually qua IO MUX register.

**File**: `components/Adafruit_DAP/dap_config.h`

---

## 6. nRESET=-1 Khong Duoc Guard

**Muc do**: MEDIUM
**Trieu chung**: Crash khi goi `pinMode(-1, ...)` hoac `digitalWrite(-1, ...)`
**Nguyen nhan**: Code goc Adafruit_DAP gia dinh nRESET luon duoc noi. Khi nRESET=-1 (khong noi), cac ham GPIO goi voi pin -1 -> undefined behavior.

**Cach fix**: Them guard `if (DAP_CONFIG_nRESET_PIN >= 0)` truoc moi operation voi nRESET:
```cpp
static inline void DAP_CONFIG_nRESET_write(int value) {
  if (DAP_CONFIG_nRESET_PIN >= 0)
    digitalWrite(DAP_CONFIG_nRESET_PIN, value);
}

static inline int DAP_CONFIG_nRESET_read(void) {
  if (DAP_CONFIG_nRESET_PIN >= 0)
    return digitalRead(DAP_CONFIG_nRESET_PIN);
  return 1; // Khong noi -> gia dinh HIGH (khong reset)
}
```

**File**: `components/Adafruit_DAP/dap_config.h` - 5 cho: SETUP, DISCONNECT, CONNECT_SWD, nRESET_write, nRESET_read

---

## 7. verifyFlash Buffer Qua Lon (Stack Overflow)

**Muc do**: HIGH
**Trieu chung**: ESP32 crash/reboot khi verify flash
**Nguyen nhan**: Code goc dung buffer 4KB tren stack. ESP32 task stack mac dinh ~4-8KB, khong du cho buffer + call stack.

**Cach fix**: Giam buffer tu 4KB xuong 256 bytes:
```cpp
// TRUOC:
uint8_t buf[4096];
// SAU:
uint8_t buf[256];
```

**File**: `components/Adafruit_DAP/Adafruit_DAP_STM32.cpp` - `verifyFlash()`

---

## 8. dap_read_block Buffer Qua Lon

**Muc do**: MEDIUM
**Trieu chung**: Tuong tu #7, crash do stack overflow
**Nguyen nhan**: dap_read_block trong code goc dung buffer lon tren stack

**Cach fix**: Giam xuong:
- Buffer: 64 bytes
- max_size: 24 bytes per iteration (= (32-5) & ~3)

**File**: `components/Adafruit_DAP/Adafruit_DAP.cpp` - `dap_read_block()`

---

## 9. SYSRESETREQ Mat SWD Sau Reset

**Muc do**: HIGH
**Trieu chung**: SWD hoat dong luc dau, nhung sau khi `select()` goi SYSRESETREQ, tat ca SWD operations fail (hoac firmware reassign SWD pins)
**Nguyen nhan**: `select()` goi `dap_write_word(AIRCR, 0x05fa0004)` de reset STM32. Sau reset:
1. Core bat dau chay firmware
2. Firmware reassign PA13/PA14 (SWD pins) thanh GPIO thuong
3. SWD connection mat

**Cach fix**: Trong `select()`, sau SYSRESETREQ:
1. `delay(15)` - cho reset hoan tat
2. `dap_target_prepare()` - reinit DP/AP
3. Re-halt core NGAY LAP TUC:
```cpp
dap_write_word(DHCSR, 0xa05f0003);  // Halt core
dap_write_word(DEMCR, 0x00000001);  // Enable VC_CORERESET
```

**Bai hoc**: VC_CORERESET (DEMCR) co the khong survive qua SYSRESETREQ tren mot so STM32 variants. Phai re-halt manually.

**File**: `components/Adafruit_DAP/Adafruit_DAP_STM32.cpp` - `select()`

---

## 10. RDP Mass Erase Timing Sai

**Muc do**: HIGH
**Trieu chung**: Reconnect sau RDP disable that bai, IDCODE = 0 hoac 0xFFFFFFFF
**Nguyen nhan**: Sau khi trigger RDP disable (OPTSTRT), mass erase mat ~8-9 giay. Code cu doi 200ms -> reconnect qua som, chip van dang erase.

**Cach fix**: Doi 10 giay truoc khi reconnect:
```cpp
vTaskDelay(pdMS_TO_TICKS(10000));  // Mass erase STM32F4 = 8-9s
```

**Luu y**: Khong the poll BSY vi SWD session CHET trong qua trinh mass erase (chip tu reset). Phai doi "mu" roi reconnect.

**File**: `main/flasher/flasher_swd.cpp` - `flasher_swd_begin_session()`

---

## 11. Serial.println Bi "Nuot" - Error Vo Hinh

**Muc do**: HIGH - gay mat nhieu thoi gian debug
**Trieu chung**: Khong co error log nao tu DAP library, nhung operations van fail
**Nguyen nhan**: Adafruit_DAP dung `Serial.println()` de log errors trong `dap_read_block` va `dap_write_block`. Nhung ESP-IDF project KHONG goi `Serial.begin()` -> tat ca Serial output bi discard im lang.

**Cach fix**: Thay `Serial.println()` bang `ESP_LOGE()`:
```cpp
// TRUOC (vo hinh):
Serial.println("invalid response while reading the block ");
Serial.println(buf[2]);

// SAU (hien thi trong log):
ESP_LOGE(TAG_DAP, "read_block error at 0x%08" PRIx32 " (resp=%d)", addr, buf[2]);
```

**Bai hoc**: Khi port Arduino library sang ESP-IDF, TIM VA THAY tat ca `Serial.print*()` bang `ESP_LOG*()`. Serial co the khong hoat dong trong ESP-IDF context.

**File**: `components/Adafruit_DAP/Adafruit_DAP.cpp` - `dap_read_block()`, `dap_write_block()`

---

## 12. Format String -Werror=format

**Muc do**: LOW (build error, de fix)
**Trieu chung**: Build fail voi error: `format '%d' expects argument of type 'int', but argument has type 'uint32_t'`
**Nguyen nhan**: ESP-IDF bat `-Werror=format`, uint32_t tren ESP32-C3 la `unsigned long` khong phai `unsigned int`

**Cach fix**: Dung `PRIx32`, `PRIu32` tu `<inttypes.h>`:
```cpp
// SAI:
printf("addr: 0x%08x", addr);     // %x cho uint32_t
printf("size: %d", size);          // %d cho uint32_t

// DUNG:
printf("addr: 0x%08" PRIx32, addr);
printf("size: %" PRIu32, size);
```

---

## 13. Sticky SWD Errors Truoc Verify

**Muc do**: HIGH - verify fail 5/6 lan
**Trieu chung**: Programming thanh cong (18316/18316 bytes, khong error), nhung verify fail tai 0x08000000 (dia chi DAU TIEN)
**Nguyen nhan**: Sau nhieu `dap_write_block()` calls trong programming, neu bat ky SWD transfer nao co transient FAULT, bit STICKYERR/WDATAERR trong DP CTRL_STAT duoc set. TAT CA SWD reads sau do fail cho den khi ghi ABORT register.

**Dau hieu nhan dien**: Verify fail tai dia chi DAU TIEN (0x08000000), khong phai dia chi cuoi -> van de la AP state, khong phai "data cuoi chua ghi xong"

**Cach fix**: Goi `dap_target_prepare()` truoc verify:
```cpp
vTaskDelay(pdMS_TO_TICKS(50));  // Cho flash settle
dap.dap_target_prepare();       // Clear sticky errors via ABORT + reinit AP

// Roi moi bat dau verify
```

`dap_target_prepare()` lam gi:
1. `ABORT = 0x1E` -> clear STICKYERR, WDATAERR, STICKYORUN, STICKYCMP
2. `SELECT = 0` -> reset AP bank selection
3. `CTRL_STAT = 0x50000000` -> power up DP
4. `CSW = 0x23000052` -> config AP cho 32-bit auto-increment reads

**File**: `main/flasher/flasher_swd.cpp` - `flasher_swd_write_firmware()`

---

## 14. dap_read/write_block Luon Return True

**Muc do**: HIGH
**Trieu chung**: Errors khong duoc propagate len caller, data sai bi dung nhu data dung
**Nguyen nhan**: Ca `dap_read_block()` va `dap_write_block()` luon `return true` bat ke co error hay khong. Error chi duoc log (qua Serial.println - xem #11) nhung return value khong reflect error state.

**Cach fix**: Return `false` khi gap error:
```cpp
if (DAP_TRANSFER_OK != buf[2]) {
  ESP_LOGE(TAG_DAP, "read_block error at 0x%08" PRIx32 " (resp=%d)", addr, buf[2]);
  return false;  // THEM DONG NAY (truoc day khong co)
}
```

**Anh huong**: Caller (verifyFlash, programBlock) gio co the phat hien loi va xu ly phu hop.

**File**: `components/Adafruit_DAP/Adafruit_DAP.cpp`

---

## 15. FLASH_SR Error Bits Khong Duoc Clear

**Muc do**: HIGH - programming fail im lang
**Trieu chung**: FLASH_SR = 0xD0 sau programming (WRPERR + PGPERR + PGSERR). Flash chi ghi duoc mot phan.
**Nguyen nhan**: FLASH_SR error bits la STICKY - mot khi set, tat ca flash operations sau do fail cho den khi clear. Loi dau tien (VD: sector erase race PGSERR) lam cascade tat ca writes tiep theo.

**Race condition trong program_start()**:
```cpp
// TRUOC (race):
for (uint32_t i = sec_start; i <= sec_end; i++) {
    while (flash_busy()) { yield(); }  // Co the doc BSY=0 truoc khi BSY kip set!
    dap_write_word(FLASH_CR, SER | STRT | SNB | PSIZE);
}

// SAU (fix):
for (uint32_t i = sec_start; i <= sec_end; i++) {
    while (flash_busy()) { yield(); }
    flash_clear_errors();              // Clear error bits truoc moi sector
    dap_write_word(FLASH_CR, SER | STRT | SNB | PSIZE);
    delay(1);                          // Cho BSY kip set truoc khi poll
}
```

**Bai hoc**: LUON clear FLASH_SR error bits truoc MOI flash operation (erase, program).

**File**: `components/Adafruit_DAP/Adafruit_DAP_STM32.cpp`

---

## 16. ~~dap_read_block Tra Ve Zeros Tren ESP32-C3~~ [DA SUA SAI]

> **[CAP NHAT]**: Gia thuyet SAI. `dap_read_block` HOAT DONG TOT cho doc.
> Zeros luc do la do sticky SWD errors (#13) hoac RDP Level 1 chan reads (#2).
> Sau khi fix sticky errors bang `dap_target_prepare()`, `dap_read_block` doc dung 100%.
> Hien tai dang dung `dap_read_block` cho on-the-fly verify trong `flasher_swd_flash_firmware()` — reliable.

**Gia thuyet cu (SAI)**: dap_read_block khong dung duoc tren ESP32-C3
**Thuc te**: dap_read_block hoat dong tot, van de nam o sticky errors chua duoc clear truoc khi doc

---

## 17. dap_write_block Cung Bi Loi Tren ESP32-C3 (WRITE zeros)

**Muc do**: CRITICAL - data khong duoc ghi dung vao flash
**Trieu chung**: Sau programming, verify fail tai ~0x08001CEC. Flash chua 0x00 thay vi data dung. FLASH_SR = 0xD0 (WRPERR+PGPERR+PGSERR).
**Nguyen nhan**: `dap_write_block` (ID_DAP_TRANSFER_BLOCK) cung bi loi nhu `dap_read_block` (#16). Auto-increment block write gui data sai (zeros) sau ~3-7KB. Cung root cause: ID_DAP_TRANSFER_BLOCK khong reliable tren ESP32-C3.

**Bang chung**:
1. Flash_clear_errors() + delay(1) DA THEM nhung FLASH_SR van = 0xD0
2. Diagnostic word reads (0x08000000-0x08000400) = DUNG → dau file ghi OK
3. Verify fail tai 0x08001CEC = 3308 bytes vao chunk 2 (4KB) → fail giua chuoi write
4. Word-level re-read tai 0x08001000 = DUNG → chung minh loi NAM O CACH GHI

**Cach fix**: Thay `dap_write_block` bang `dap_write_word` trong `programBlock()`:
```cpp
// TRUOC (broken):
dap_write_block(addr, buf, size);

// SAU (reliable):
uint32_t words = (size + 3) / 4;
for (uint32_t i = 0; i < words; i++) {
    uint32_t word = 0;
    for (uint32_t b = 0; b < 4 && b < remaining; b++)
        word |= ((uint32_t)buf[i*4+b]) << (b*8);
    dap_write_word(word_addr, word);
}
```

**Trade-off**: Cham hon nhung DUNG. Moi word can 2 SWD transfers (TAR + DRW).

**KET LUAN** ~~(cu - SAI MOT PHAN)~~: ~~ID_DAP_TRANSFER_BLOCK khong dung cho ca read lan write.~~

> **[CAP NHAT]**: Block READS hoat dong tot (dap_read_block reliable cho verify).
> Chi block WRITES la co van de — dap_write_word (single word) mat ~1/1000 words.
> Flow hien tai: programBlock dung dap_write_word (ghi) + dap_read_block (verify).

**File**: `components/Adafruit_DAP/Adafruit_DAP_STM32.cpp` - `programBlock()`

---

## 18. 3x Blind Write Retry Khoa Flash Controller

**Muc do**: CRITICAL - lam RDP disable KHONG BAO GIO thanh cong
**Trieu chung**: Gui blind write sequence 3 lan lien tiep de tang do tin cay. Nhung IDCODE giu 0x2ba01477 suot 15 giay cho → mass erase KHONG BAT DAU. Truoc do gui 1 lan thi IDCODE ve 0x00000000 (mass erase chay).
**Nguyen nhan**: Flash controller KEY registers la state machine TUAN TU: KEY1 roi KEY2. Neu KEY2 cua lan thu N bi drop im lang (SWD ~0.1% fail), controller DUNG o trang thai "cho KEY2". KEY1 cua lan N+1 den → controller hieu la KEY2 SAI → KHOA VINH VIEN cho den khi reset chip.

```
Lan 1: KEY1 ✓ → KEY2 ✗ (drop!)  → controller: "dang cho KEY2..."
Lan 2: KEY1 den → controller hieu la KEY2 SAI → LOCK!
        KEY2 den → da lock, bo qua
        OPTKEY1, OPTKEY2, OPTCR → cung bo qua vi flash chua unlock
```

**Cach fix**: Gui SYSRESETREQ + delay(50) + halt_core() GIUA moi lan thu:
```cpp
for (int bw = 1; bw <= 3; bw++) {
    if (bw > 1) {
        dap.dap_write_word(AIRCR, 0x05FA0004);  // SYSRESETREQ
        delay(50);
        halt_core();  // DHCSR + DEMCR
    }
    // Flash unlock
    dap.dap_write_word(FLASH_KEYR, 0x45670123);
    dap.dap_write_word(FLASH_KEYR, 0xCDEF89AB);
    // Option unlock
    dap.dap_write_word(FLASH_OPTKEYR, 0x08192A3B);
    dap.dap_write_word(FLASH_OPTKEYR, 0x4C5D6E7F);
    // Trigger
    dap.dap_write_word(FLASH_OPTCR, 0x0FFFAAEE);
}
```

**Bai hoc**: BAT KY thanh ghi nao yeu cau KEY TUAN TU (KEY1→KEY2), KHONG DUOC retry truc tiep. Phai reset state machine truoc khi thu lai. Ap dung cho: FLASH_KEYR, FLASH_OPTKEYR, va cac MCU khac co co che tuong tu.

**File**: `main/flasher/flasher_swd.cpp` - `flasher_swd_rdp_disable_trigger()`

---

## 19. CSW AddrInc + flash_busy() Ghi Vao FLASH_CR

**Muc do**: CRITICAL - flash ghi duoc ~44 words roi dung
**Trieu chung**: Flash ghi OK duoc ~176 bytes (44 words), sau do FLASH_CR = 0x80000000 (LOCK bit), FLASH_SR = PGSERR+PGPERR, phan con lai flash = 0xFF.
**Nguyen nhan**: CSW = 0x23000052 co AddrInc=single (bit 5:4=01). Khi `flash_busy()` doc FLASH_SR (0x40023C0C), TAR tu dong tang len FLASH_CR (0x40023C10). Neu DRW write tiep theo glitch (TAR khong duoc cap nhat dung), data ghi vao FLASH_CR thay vi flash → set LOCK bit.

```
Quy trinh BINH THUONG:
  1. TAR = flash_addr → DRW = data  (ghi vao flash)
  2. TAR = FLASH_SR  → DRW = read   (doc BSY)  ← TAR tang thanh FLASH_CR!
  3. TAR = flash_addr+4 → DRW = data (ghi tiep)

Khi glitch o buoc 3 (TAR khong cap nhat):
  3. TAR van = FLASH_CR → DRW = data → GHI VAO FLASH_CR → LOCK!
```

**Cach fix**: XOA `flash_busy()` khoi vong lap ghi trong `programBlock()`. SWD bit-bang cham (~144μs/word) >> flash write time (~16μs). BSY dam bao da clear truoc khi word tiep theo den. Khong can poll.

**Bai hoc**: Khi CSW co AddrInc, MOI doc/ghi AP deu tang TAR. Pha tron doc thanh ghi voi ghi flash NGUY HIEM. Giu nguyen chi ghi (writes-only) trong inner loop.

**File**: `components/Adafruit_DAP/Adafruit_DAP_STM32.cpp` - `programBlock()`

---

## 20. SPRMOD Bit 31 Bat PCROP Thay Vi Tat WRP

**Muc do**: HIGH - chip bi khoa PCROP, can mass erase de mo
**Trieu chung**: Sau RDP disable, chip o Level 0 nhung flash van khong ghi duoc. OPTCR co bit 31 = 1.
**Nguyen nhan**: Khi set nWRP (bits 27:16) de tat write protection, dung mask `0xFFFF << 16` thay vi `0x0FFF << 16`. Bit 31 la SPRMOD:
- SPRMOD=0: WRP mode (binh thuong, nWRP=1 = khong bao ve)
- SPRMOD=1: PCROP mode (bao ve doc code, KHONG THE TAT bang cach binh thuong)

```
// SAI (bat PCROP):
optcr |= (0xFFFF << 16);  // bit 31 = 1 = SPRMOD = PCROP!

// DUNG (chi set nWRP, giu SPRMOD=0):
optcr = 0x0FFFAAEE;  // Hardcode: SPRMOD=0, nWRP=0x0FFF, RDP=0xAA, OPTSTRT=1
```

**Bai hoc**: KHONG BAO GIO doc OPTCR roi modify khi o RDP1 (doc tra ve 0x00000000). Dung gia tri HARDCODE `0x0FFFAAEE` (factory default + RDP=0xAA + OPTSTRT).

**File**: `main/flasher/flasher_swd.cpp` - `flasher_swd_rdp_disable_trigger()`

---

## 21. dap_write_word Mat Du Lieu ~1/1000 Words

**Muc do**: HIGH - flash bi sai du lieu ngau nhien
**Trieu chung**: Sau khi ghi 18KB firmware (72 chunks x 256B), ~3 chunks co 1-2 words sai. Vi tri sai NGAU NHIEN, khong co pattern. Moi lan flash khac nhau.
**Nguyen nhan**: `dap_write_word()` ghi TAR + DRW, LUON return true (khong kiem tra SWD ACK). Tren ESP32-C3 bit-bang SWD, ~0.1% words bi drop im lang do timing jitter hoac interrupt interference.

**Cach fix**: On-the-fly verify BAT BUOC:
```cpp
#define CHUNK_SIZE 256
#define CHUNK_RETRIES 3

for (moi 256B chunk) {
    for (int retry = 0; retry < CHUNK_RETRIES; retry++) {
        dap.programBlock(addr, data, CHUNK_SIZE);   // Ghi (dap_write_word ben trong)
        vTaskDelay(1);                              // Cho flash settle + WDT feed
        dap.dap_read_block(addr, verify, CHUNK_SIZE); // Doc lai (block read OK)
        if (memcmp(data, verify, CHUNK_SIZE) == 0) break;  // OK!
        // Sai → retry chunk (programBlock tu unlock flash + set PG)
    }
}
```

**Thong ke thuc te**: ~3/72 chunks can 1 retry. Chua gap chunk nao fail 3 retries.

**Bai hoc**: Tren ESP32-C3 bit-bang SWD, KHONG TIN dap_write_word 100%. Phai verify sau moi chunk nho. 256B la kich thuoc tot (du nho de retry nhanh, du lon de khong qua cham).

**File**: `main/flasher/flasher_swd.cpp` - `flasher_swd_flash_firmware()`

---

## 22. 5V Cap Nguon STM32 - SWD Voltage Mismatch

**Muc do**: HIGH - SWD giao tiep hoan toan fail
**Trieu chung**: ESP32-C3 dev board co chan 5V (tu USB) va 3.3V (sau LDO). Dung chan 5V cap cho STM32 target → chip chay (firmware execute OK) nhung SWD connect/flash KHONG BAO GIO thanh cong.
**Nguyen nhan**: Logic level mismatch:
- ESP32-C3 output HIGH = 3.3V
- STM32 chay 5V can VIH = 0.7 × 5V = 3.5V → 3.3V KHONG DU HIGH
- STM32 SWDIO output = 5V → dua nguoc vao GPIO ESP32-C3 (max 3.6V) → CO THE HU GPIO

```
ESP32-C3 (3.3V) ──SWDIO──> STM32 (5V)
   3.3V output    →    Can 3.5V VIH   → KHONG DAT

STM32 (5V) ──SWDIO──> ESP32-C3 (3.3V)
   5.0V output    →    Max 3.6V       → QUA TAI, HU GPIO!
```

**Cach fix**: Doi day nguon tu chan 5V sang chan 3.3V cua ESP32-C3 dev board.

**Bai hoc**: Khi giao tiep giua 2 MCU, PHAI CUNG MUC DIEN AP. ESP32-C3 la 3.3V → target cung phai 3.3V. Neu can 5V, dung level shifter.

---

## 23. SD.seek(0) Khong Hoat Dong Sau EOF

**Muc do**: MEDIUM - doc file SD bi thieu du lieu
**Trieu chung**: Doc file firmware tu SD, sau khi doc het EOF, goi `SD.seek(0)` de doc lai tu dau → seek tra ve true nhung read tra ve 0 bytes.
**Nguyen nhan**: Arduino ESP32 SD library (dua tren SDFS/VFS) co bug: sau khi doc den EOF, internal file pointer khong reset dung khi seek(0).

**Cach fix**: Dong file roi mo lai thay vi seek:
```cpp
// SAI (khong hoat dong):
fwFile.seek(0);
fwFile.read(buf, size);  // → 0 bytes!

// DUNG:
fwFile.close();
fwFile = SD.open(path, FILE_READ);
fwFile.read(buf, size);  // → OK
```

**Giai phap tot hon**: Doc TOAN BO file vao RAM mot lan, dong file, roi thao tac tren RAM buffer. Day cung la cach hien tai trong `flasher_swd_flash_firmware()` — vua fix bug nay vua tranh SPI interference voi SWD.

**File**: `main/flasher/flasher_swd.cpp` - `flasher_swd_flash_firmware()` STEP 1

---

## 24. Static Adafruit_DAP_STM32 Chiem RAM Vinh Vien

**Muc do**: LOW (hien tai) / HIGH (neu can toi uu RAM)
**Trieu chung**: RAM bi chiem vinh vien boi object `dap` du khong dang dung SWD.
**Nguyen nhan**: Khai bao `static Adafruit_DAP_STM32 dap;` o file scope trong `flasher_swd.cpp`.

**Ban chat van de:**
- Day la mot object instance (thuc the) nam thuong truc trong RAM (.bss segment).
- Vi la `static` file-scope, no duoc cap phat ngay khi chip khoi dong va KHONG BAO GIO duoc giai phong.
- Du ban dang o menu OLED, dang nap ESP32 qua UART, hay dang nghi... thi cuc `dap` nay van chiem RAM.
- Bao gom tat ca bien noi bo, buffer dem, state machine cua class Adafruit_DAP_STM32.

**Heu qua RAM:**
```
static Adafruit_DAP_STM32 dap;
// → Sizeof(Adafruit_DAP_STM32) bytes chiem vinh vien
// Neu class nay ~2KB → Mat 2KB RAM vinh vien
// Tren ESP32-C3 (400KB SRAM) → ~0.5% RAM mat khong can thiet
// Anh huong lon hon khi can malloc() cho FW buffer (streaming mode)
```

**So sanh 2 cach:**
```cpp
// HIEN TAI: Static — don gian, khong leak, nhung chiem RAM vinh vien
static Adafruit_DAP_STM32 dap;

// THAY THE: Dynamic — chi cap phat khi can, giai phong khi xong
static Adafruit_DAP_STM32* dap = nullptr;

esp_err_t flasher_swd_init() {
    if (!dap) dap = new Adafruit_DAP_STM32();
    // ...
}

esp_err_t flasher_swd_deinit() {
    delete dap;
    dap = nullptr;
    // ...
}
```

**Trade-off:**
- Static: Don gian, an toan (khong leak), zero overhead. Phu hop khi RAM du.
- Dynamic: Tiet kiem RAM, nhung can can than: moi cho dung `dap.` phai doi thanh `dap->`, phai check nullptr, risk memory leak neu quyen goi deinit().

**Ket luan**: Giu static neu RAM con du. Chi doi sang dynamic khi streaming mode (FW lon) bi thieu RAM vi malloc(32KB) fail. Luc do 2KB tra lai tu `dap` co the la su khac biet giua thanh cong va that bai.

**File**: `main/flasher/flasher_swd.cpp` dong 48

---

## Tong Ket Thu Tu Debug

```
Loi gap truoc                    Loi gap sau
     |                                |
     v                                v

noInterrupts() crash ---------> LED_BUILTIN xung dot
     |                                |
     v                                v
PIN_INPUT_ENABLE thieu -------> nRESET=-1 crash
     |                                |
     v                                v
Stack overflow (buffer) ------> SYSRESETREQ mat SWD
     |                                |
     v                                v
RDP Level 1 chan flash -------> RDP timing sai (200ms)
     |                                |
     v                                v
GPIO2/FSPIQ xung dot ---------> Serial.println vo hinh
     |                                |
     v                                v
Sticky SWD errors ------------> dap block return true
     |                                |
     v                                v
FLASH_SR sticky errors -------> dap_read_block zeros
```

## Checklist Khi Port Adafruit_DAP Len ESP32

### Library Fixes
- [ ] Xoa `noInterrupts()` / `interrupts()` (#3)
- [ ] Disable `LED_BUILTIN` neu xung dot I2C (#4)
- [ ] Them `PIN_INPUT_ENABLE()` cho SWDIO (TMS_in + CONNECT_SWD) (#5)
- [ ] Guard tat ca nRESET operations voi `if (pin >= 0)` (#6)
- [ ] Giam buffer sizes (verifyFlash: 256B, dap_read_block: 64B) (#7, #8)
- [ ] Thay `Serial.println()` bang `ESP_LOGE()` (#11)
- [ ] Fix `dap_read/write_block` return false on error (#14)
- [ ] Re-halt core sau SYSRESETREQ trong `select()` (#9)
- [ ] Clear FLASH_SR error bits truoc moi flash operation (#15)
- [ ] XOA flash_busy() khoi programBlock inner loop (#19)
- [ ] Them delay(1) sau STRT bit trong sector erase loop (#15)

### Application Code
- [ ] Chon GPIO KHONG PHAI FSPI default (tranh GPIO2,4,5,6,7,10) (#1)
- [ ] Dung `PRIx32`/`PRIu32` cho uint32_t (#12)
- [ ] Them `dap_target_prepare()` truoc verify (clear sticky errors) (#13)
- [ ] Doi 10-15s cho RDP mass erase (khong 200ms) (#10)
- [ ] SYSRESETREQ giua moi lan retry blind write KEY sequence (#18)
- [ ] Dung hardcode OPTCR = 0x0FFFAAEE, KHONG doc-modify (#20)
- [ ] On-the-fly verify BAT BUOC sau moi 256B chunk (#21)
- [ ] Buffer toan bo FW vao RAM, dong SD truoc khi flash (#23)

### Phan Cung
- [ ] Cap nguon target CUNG MUC 3.3V voi ESP32-C3 (#22)

---

## Pin Assignment Hien Tai

```
GPIO0 = SWD SWDIO  (truoc la GPIO2 - bi xung dot FSPIQ)
GPIO1 = Free       (truoc la nRESET - khong noi)
GPIO2 = Free       (tranh - FSPIQ default)
GPIO3 = SWD SWCLK
GPIO4 = SD CS      (FSPI HD default - nhung SD driver override OK)
GPIO5 = SD MISO    (FSPI WP default)
GPIO6 = SD CLK     (FSPI CLK default)
GPIO7 = SD MOSI    (FSPI MOSI default)
nRESET = -1        (khong noi, dung nut reset vat ly)
```

---

> **Cap nhat**: 2026-02-16 | Tong cong 24 bugs (1 gia thuyet sai da danh dau)
