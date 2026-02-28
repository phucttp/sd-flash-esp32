# 🚀 Đặc Tả Engine Nạp STM32F4 qua SWD (Universal Flasher)

Tài liệu này mô tả chi tiết các địa chỉ thanh ghi, các cờ (bit) điều khiển và luồng logic để giao tiếp với bộ điều khiển Flash của STM32F4 thông qua giao thức SWD.

---

## 1. 🗄️ Bảng Định Nghĩa Thanh Ghi (Registers & Bits)

Toàn bộ các thanh ghi điều khiển Flash của STM32F4 có địa chỉ cơ sở (Base Address) bắt đầu từ `0x40023C00`.

| Tên Thanh Ghi | Địa Chỉ Lõi (Hex) | Chức năng |
| :--- | :--- | :--- |
| **FLASH_KEYR** | `0x40023C04` | Thanh ghi nhập khóa mở Flash Control Register (hạ cờ LOCK). |
| **FLASH_OPTKEYR** | `0x40023C08` | Thanh ghi nhập khóa mở vùng Option Bytes (cho phép ghi OPTCR). |
| **FLASH_SR** | `0x40023C0C` | Thanh ghi trạng thái (Status Register). |
| **FLASH_CR** | `0x40023C10` | Thanh ghi điều khiển (Control Register). |
| **FLASH_OPTCR** | `0x40023C14` | Thanh ghi cấu hình Option Bytes (chứa RDP, WRP, BOR, SPRMOD). |
| **DHCSR** | `0xE000EDF0` | (Cortex-M4) Debug Halting Control & Status — dừng/chạy CPU. |
| **DCRSR** | `0xE000EDF4` | Chọn register CPU để đọc/ghi qua DCRDR. |
| **DCRDR** | `0xE000EDF8` | Dữ liệu đọc/ghi register CPU đã chọn bởi DCRSR. |
| **DEMCR** | `0xE000EDFC` | Debug Exception & Monitor Control — VC_CORERESET để bắt CPU sau reset. |
| **AIRCR** | `0xE000ED0C` | (Cortex-M4) Application Interrupt & Reset Control — kích hoạt System Reset. |

### 🔑 Các Giá Trị Khóa (Keys)
* **KEY1 (Flash):** `0x45670123`
* **KEY2 (Flash):** `0xCDEF89AB`
* **OPTKEY1:** `0x08192A3B`
* **OPTKEY2:** `0x4C5D6E7F`
* **OPTCR_FACTORY_DEFAULT:** `0x0FFFAAED` (RDP=0xAA, nWRP=0x0FFF, SPRMOD=0, BOR=01)
* **OPTCR_UNLOCK_WITH_START:** `0x0FFFAAEE` (= factory default + OPTSTRT bit 1)
* **DHCSR_HALT:** `0xA05F0003` (Key + C_DEBUGEN + C_HALT)
* **DEMCR_VC_CORERESET:** `0x00000001` (Bắt CPU khi gặp vector Reset)
* **SYSRESETREQ:** `0x05FA0004` (Ghi vào AIRCR để kích hoạt System Reset)

### 🚩 Các Cờ (Bits) Quan Trọng Trong `FLASH_SR` (Trạng thái)
* `Bit 0` **(EOP):** End of Operation — bằng 1 khi vừa hoàn thành thao tác Flash.
* `Bit 1` **(OPERR):** Operation Error — lỗi thao tác (ghi khi chưa mở khóa, v.v.).
* `Bit 4` **(WRPERR):** Write Protection Error — cố ghi/xóa vùng bị WRP khóa.
* `Bit 5` **(PGAERR):** Programming Alignment Error — ghi sai alignment (phải theo PSIZE).
* `Bit 6` **(PGPERR):** Programming Parallelism Error — sai PSIZE so với điện áp VCC.
* `Bit 7` **(PGSERR):** Programming Sequence Error — sai trình tự lệnh (VD: chưa set PG mà ghi).
* `Bit 16` **(BSY):** Busy — bằng 1 khi Flash đang bận (xóa hoặc ghi). **Không được ghi CR khi BSY=1.**

### ⚙️ Các Cờ (Bits) Quan Trọng Trong `FLASH_CR` (Điều khiển)
* `Bit 0` **(PG):** Programming — chọn chế độ Ghi Flash.
* `Bit 1` **(SER):** Sector Erase — chọn chế độ Xóa theo Sector.
* `Bit 2` **(MER):** Mass Erase — chọn chế độ Xóa toàn bộ Flash.
* `Bits [6:3]` **(SNB):** Sector Number — chọn sector cần xóa (0–11 tùy chip).
* `Bits [9:8]` **(PSIZE):** Program Size — kích thước mỗi lần ghi:
  * `00` = x8 (1 byte) — VCC ≥ 1.8V
  * `01` = x16 (half-word) — VCC ≥ 2.1V
  * `10` = x32 (word) — VCC ≥ 2.7V ⬅️ **Dùng giá trị này (0x200)**
  * `11` = x64 (double word) — VCC = 3.3V + VPP = 9V
* `Bit 16` **(STRT):** Start — "bóp cò" bắt đầu thao tác Xóa.
* `Bit 31` **(LOCK):** Lock — bằng 1 khi Flash Controller bị khóa. Ghi Key để hạ.

### 🛡️ Các Trường Quan Trọng Trong `FLASH_OPTCR` (Option Bytes)
* `Bit 0` **(OPTLOCK):** Option Lock — bằng 1 khi Option Bytes bị khóa.
* `Bit 1` **(OPTSTRT):** Option Start — "bóp cò" bắt đầu ghi Option Bytes vào Flash.
* `Bits [3:2]` **(BOR_LEV):** Brownout Reset Level.
* `Bits [15:8]` **(RDP):** Read Data Protection Level:
  * `0xAA` = Level 0 — Không bảo vệ ✅
  * `0xCC` = Level 2 — Khóa vĩnh viễn (JTAG fuse đốt) ❌
  * Bất kỳ giá trị khác = Level 1 — Chặn đọc, cho phép ghi mù ⚠️
* `Bits [27:16]` **(nWRP):** Not Write Protect — mỗi bit = 1 sector, 1 = không khóa.
* `Bit 31` **(SPRMOD):** Selection of Protection Mode:
  * `0` = WRP mode (bình thường) ✅
  * `1` = PCROP mode (Proprietary Code Readout Protection) ❌ **KHÔNG ĐƯỢC SET!**

> ⚠️ **CẢNH BÁO SPRMOD:** Nếu dùng mask `0xFFFF << 16` thì bit 31 (SPRMOD) = 1 → kích hoạt PCROP thay vì tắt WRP. **Mask đúng: `0x0FFF << 16`** (chỉ bits 27:16, giữ bit 31 = 0).

---

## 2. 📊 Bảng Sector Flash STM32F4 (512KB model — VD: STM32F411)

| Sector | Địa chỉ bắt đầu | Kích thước | SNB |
| :--- | :--- | :--- | :--- |
| 0 | `0x08000000` | 16 KB | 0 |
| 1 | `0x08004000` | 16 KB | 1 |
| 2 | `0x08008000` | 16 KB | 2 |
| 3 | `0x0800C000` | 16 KB | 3 |
| 4 | `0x08010000` | 64 KB | 4 |
| 5 | `0x08020000` | 128 KB | 5 |
| 6 | `0x08040000` | 128 KB | 6 |
| 7 | `0x08060000` | 128 KB | 7 |

> **Lưu ý:** Xóa = xóa TOÀN BỘ sector. Sector 4 (64KB) và sector 5+ (128KB) rất lớn — dev phải thiết kế linker script cẩn thận để config data không nằm chung sector với firmware.

---

## 3. 🔍 Luồng 0: Phát Hiện RDP (Detect RDP Level)
*Mục tiêu: Xác định chip đang ở mức bảo vệ nào trước khi quyết định hành động tiếp.*

1. **Init SWD & Connect:**
   * Gửi chuỗi JTAG-to-SWD → Đọc `IDCODE` (VD: `0x2BA01477` cho Cortex-M4).
2. **Halt CPU:**
   * Ghi `0xA05F0003` vào `DHCSR` (`0xE000EDF0`).
   * Ghi `0x00000001` vào `DEMCR` (`0xE000EDFC`).
3. **Identify Chip:**
   * Gọi `select()` — đọc `DBGMCU_IDCODE` (VD: `0x413` = STM32F405/407, `0x431` = STM32F411).
4. **Đọc OPTCR:**
   * Đọc `FLASH_OPTCR` (`0x40023C14`).
   * Trích byte RDP tại bits [15:8].
   * `0xAA` → Level 0 | `0xCC` → Level 2 | Khác → Level 1.
   * ⚠️ **Ở Level 1, OPTCR đọc ra `0x00000000`** (AHB-AP bị chặn) → đây là Level 1.
5. **Kiểm chứng thực tế:**
   * Đọc `0x08000000` (đầu Flash).
   * Nếu trả về `0x00000000` → AHB-AP bị chặn → xác nhận Level 1.
   * Nếu trả về dữ liệu thật → Level 0 đúng.
6. **Disconnect & Deinit.**

> 💡 **detect_rdp() còn có vai trò warm-up:** Chu kỳ connect-select-read-disconnect "đánh thức" debug interface, cải thiện đáng kể tỷ lệ kết nối thành công cho phiên tiếp theo.

---

## 4. 🚧 Luồng 1: Phá Khóa Đọc (RDP Disable — Blind Writes + Mass Erase)
*Mục tiêu: Đưa RDP từ Level 1 về Level 0, chip tự động Mass Erase toàn bộ Flash.*

> ⚠️ **Đặc tính RDP Level 1 trên STM32F4:** Mọi lệnh ĐỌC qua AHB-AP trả về `0x00` (Flash, SRAM, ngoại vi). Nhưng lệnh GHI vẫn đến được Flash Controller → đây là "lỗ hổng" để cứu chip.

1. **Init SWD & Connect** (chỉ physical layer, **KHÔNG gọi select()**).
2. **Chuẩn bị AP:** Gọi `dap_target_prepare()` để bật Debug Power.
3. **Halt Core + SYSRESETREQ:**
   * Ghi `0xA05F0003` vào `DHCSR` + `0x00000001` vào `DEMCR`.
   * Ghi `0x05FA0004` vào `AIRCR` → Reset mềm toàn hệ thống.
   * Delay 50ms → Halt lại ngay.
   * *Mục đích:* Đưa Flash Controller về trạng thái sạch (tất cả LOCKED, không pending).
4. **Blind Write Sequence (Retry 3 lần, SYSRESETREQ giữa mỗi lần):**

   Mỗi lần thử:
   ```
   a. Ghi 0x45670123 → FLASH_KEYR (0x40023C04)    ← Flash Key 1
   b. Ghi 0xCDEF89AB → FLASH_KEYR (0x40023C04)    ← Flash Key 2
   c. Ghi 0x08192A3B → FLASH_OPTKEYR (0x40023C08)  ← Option Key 1
   d. Ghi 0x4C5D6E7F → FLASH_OPTKEYR (0x40023C08)  ← Option Key 2
   e. Ghi 0x0FFFAAEE → FLASH_OPTCR (0x40023C14)    ← RDP=0xAA + OPTSTRT
   ```

   > ⚠️ **TUYỆT ĐỐI KHÔNG ĐỌC AHB-AP** trong suốt quá trình này! Đọc AHB-AP sẽ làm lệch con trỏ TAR (Auto-increment) → ghi sai địa chỉ.

   > ⚠️ **PHẢI SYSRESETREQ giữa các lần retry:** Key registers là sequential (KEY1 rồi KEY2). Nếu KEY2 của lần N bị rớt, controller kẹt ở "đang chờ KEY2". KEY1 của lần N+1 bị hiểu nhầm là KEY2 → LOCKED. Reset mới xóa được state machine.

5. **Chờ Mass Erase (15 giây):**
   * Vòng lặp: Đọc `IDCODE` (tầng DP — luôn hoạt động) mỗi giây để keep-alive.
   * Trong lúc erase, IDCODE có thể trả về `0x00000000` → chip đang bận xóa → bình thường.
   * **TUYỆT ĐỐI KHÔNG đọc Flash/SRAM** lúc này → treo bus.
6. **Disconnect & Deinit.**
7. **YÊU CẦU NGƯỜI DÙNG RÚT ĐIỆN CẮM LẠI (Power Cycle).**

   > ⚠️ **STM32F4 KHÔNG CÓ OBL_LAUNCH bit** (khác F1/F0). Option Bytes chỉ được reload khi **power-on reset**. SYSRESETREQ hoặc nút Reset **KHÔNG ĐỦ** — PHẢI rút điện hoàn toàn.

### Luồng 1b: Thẩm Định Sau Power Cycle (Verify)

1. **Reconnect:** Init → Connect → Halt.
2. **Đọc OPTCR:** Kiểm tra RDP byte = `0xAA` → Level 0 confirmed.
3. **Đọc Flash[0x08000000]:**
   * `0xFFFFFFFF` → Flash đã xóa sạch ✅
   * Khác → **Zombie state:** RDP đã mở nhưng Mass Erase bị ngắt quãng → chạy **Rescue Erase**.

### Luồng 1c: Rescue Erase (Xóa thủ công khi chip đã Level 0)

1. **Mở khóa Flash:**
   * Ghi KEY1 + KEY2 vào `FLASH_KEYR`.
   * Đọc `FLASH_CR`, kiểm tra bit LOCK (31) = 0.
2. **Xóa lỗi cũ:** Ghi `0xF3` vào `FLASH_SR` (write-1-to-clear).
3. **Kích hoạt Mass Erase:**
   * Ghi `0x00000204` vào `FLASH_CR` → MER + PSIZE=x32.
   * Ghi `0x00010204` vào `FLASH_CR` → MER + PSIZE=x32 + STRT.
4. **Chờ BSY:** Đọc `FLASH_SR`, chờ bit BSY (16) = 0 (timeout 20s).
5. **Kiểm tra lỗi:** Nếu `FLASH_SR` bits [7:4] có lỗi → Erase fail.

---

## 5. 🚀 Luồng 2: Nạp Firmware Mới
*Mục tiêu: Xóa các Sector cũ, nạp firmware mới theo Word (32-bit), xác thực on-the-fly.*

> **Yêu cầu:** Chip PHẢI ở RDP Level 0. Nếu Level 1/2 → chạy Luồng 1 trước.

### Bước 1: Đọc File & Quản Lý RAM (Dual Strategy)

| Chiến lược | Điều kiện | Ưu điểm | Nhược điểm |
| :--- | :--- | :--- | :--- |
| **Full-RAM** | `malloc(file_size)` thành công | Nhanh, đóng SD ngay | FW phải ≤ RAM trống (~200KB) |
| **Streaming** | Full-RAM fail, `malloc(32KB)` | FW bất kỳ kích thước | Chậm hơn, giữ SD mở |

* Pad file đến bội số 4 byte bằng `0xFF` (alignment 32-bit).
* Full-RAM: đọc hết → đóng file → nạp từ RAM.
* Streaming: giữ file mở, đọc 32KB/lần trong lúc nạp.

### Bước 2: Warm-up & Kiểm tra RDP

* Gọi `detect_rdp()` → warm-up debug interface + xác nhận Level 0.
* Nếu Level 1/2 → dừng ngay, báo lỗi "RDP locked!".

### Bước 3: Kết Nối SWD & Identify Chip

* Init → Connect (retry 10 lần) → Halt → Select.
* Đọc flash_size → validate: firmware ≤ flash.

### Bước 4: Xóa Flash (Sector Erase)

* Gọi `program_start(0x00, padded_size)` (thư viện Adafruit_DAP tự tính sectors).
* Bên trong library:
  * Mở khóa Flash (KEY1 + KEY2).
  * Xóa lỗi cũ trong SR.
  * Với mỗi sector bị ảnh hưởng: set SER + SNB + STRT → chờ BSY = 0.
* ⚠️ Gọi `flash_clear_errors()` trước **mỗi** sector erase (SR sticky bits).
* ⚠️ Delay 1ms sau khi set STRT để BSY kịp assert.

### Bước 5: Nạp + Verify On-the-fly (Chunk 256 byte)

> **Tại sao 256B?** Mỗi chunk = 1 lần flash_unlock + set PG + ghi 64 words + lock. Nhỏ hơn (128B) = overhead unlock/lock cao. Lớn hơn (512B) = nếu 1 word fail thì cascade → khó retry.

Với mỗi chunk 256B:

1. **Ghi chunk:**
   * Gọi `programBlock(addr, data, 256)`.
   * Bên trong: Mở khóa → Set PG + PSIZE=x32 → Ghi 64 words bằng `dap_write_word()` → Lock.
   * ⚠️ **KHÔNG gọi flash_busy()** trong inner loop! CSW auto-increment sẽ làm TAR trượt từ FLASH_SR sang FLASH_CR → ghi nhầm → LOCK bit set → PGSERR/PGPERR → phần còn lại = 0xFF.
2. **Delay 1ms:** `vTaskDelay(1)` — cho SWD bus ổn định.
3. **Verify chunk:**
   * Gọi `dap_read_block(addr, verify_buf, 256)` — đọc block nhanh hơn read_word.
   * `memcmp(data, verify_buf, 256)`.
4. **Nếu mismatch → Retry tại chỗ (tối đa 3 lần):**
   * Ghi lại chunk bằng `programBlock()` → delay → verify lại.
   * Thực tế: ~3/72 chunks cần 1 retry (tỷ lệ lỗi `dap_write_word` ≈ 0.1%).
5. **Nếu retry hết 3 lần vẫn fail → Re-flash toàn bộ (tối đa 3 attempts):**
   * Erase lại → Nạp lại từ đầu.

### Bước 6: Hoàn Tất

* Lock Flash: Đọc `FLASH_CR`, set bit LOCK (31).
* Disconnect + Deinit.
* Reset chip (SYSRESETREQ) để chạy firmware mới.

---

## 6. ⚠️ Bẫy & Lưu Ý Quan Trọng (Lessons Learned)

### 6.1 dap_write_word() Silent Fail (~0.1%)
* ESP32-C3 bit-bang SWD có tỷ lệ lỗi ghi ~1/1000 words.
* Hàm `dap_write_word()` ghi TAR+DRW, **luôn trả về true** (bỏ qua SWD ACK).
* **Bắt buộc** verify on-the-fly sau mỗi 256B chunk.

### 6.2 CSW Auto-Increment + flash_busy() → FLASH_CR LOCK
* CSW = `0x23000052` (AddrInc=single, bits 5:4 = 01).
* `flash_busy()` đọc `FLASH_SR` → TAR auto-increment sang `FLASH_CR`.
* Nếu DRW write tiếp theo lỡ ghi vào `FLASH_CR` → set LOCK → phần còn lại fail.
* **Fix:** Bỏ `flash_busy()` khỏi inner loop. SWD bit-bang (~144μs/word) chậm hơn nhiều so với flash write time (~16μs) → BSY luôn clear trước word tiếp theo.

### 6.3 FLASH_SR Sticky Error Bits
* Các cờ lỗi (PGSERR, PGPERR, WRPERR...) KHÔNG tự reset — dính vĩnh viễn.
* Phải ghi `0xF3` vào `FLASH_SR` (write-1-to-clear) trước **mỗi sector erase**.
* Không cần clear trong `programBlock()` inner loop.

### 6.4 Sector Erase Race Condition
* Sau khi set STRT, cần delay 1ms để BSY kịp assert.
* Nếu đọc BSY ngay → BSY=0 (chưa kịp set) → tưởng xong → ghi flash lên sector chưa xóa xong.

### 6.5 OPTCR đọc ra 0x00000000 dưới RDP Level 1
* Đây là **bình thường**, không phải giá trị thật.
* Không được dùng giá trị đọc được để sửa đổi → **hardcode `0x0FFFAAEE`** khi ghi.

### 6.6 SPRMOD (Bit 31 OPTCR) — PCROP Trap
* `0xFFFF << 16` → bit 31 = 1 → kích hoạt PCROP (không phải WRP).
* **Mask đúng: `0x0FFF << 16`** (chỉ bits 27:16 = nWRP, giữ bit 31 = 0).

### 6.7 STM32F4 không có OBL_LAUNCH
* Khác F1/F0/G0, F4 **không** có bit OBL_LAUNCH trong FLASH_CR.
* Option Bytes chỉ reload khi **power-on reset** (không phải SYSRESETREQ).
* **PHẢI rút điện hoàn toàn**, nhấn nút Reset KHÔNG ĐỦ.

### 6.8 5V vs 3.3V Target Power
* Cấp 5V cho STM32 → VIH = 3.5V, ESP32-C3 output = 3.3V → dưới ngưỡng → SWD read fail.
* STM32 output SWDIO @ 5V → có thể damage ESP32-C3 GPIO (max 3.6V).
* **PHẢI dùng 3.3V** từ chân 3V3 của ESP32-C3.

### 6.9 GPIO2/FSPIQ Conflict
* GPIO2 = mặc định FSPIQ (SPI2 MISO) trên ESP32-C3.
* Nếu dùng GPIO2 cho SWDIO → SD card SPI activity phá tín hiệu SWD.
* **Fix:** Dùng GPIO0 cho SWDIO (không có FSPI function).

---

## 7. 📈 So Sánh Nhanh F4 vs F1

| Đặc điểm | STM32F4 | STM32F1 |
| :--- | :--- | :--- |
| Flash Controller Base | `0x40023C00` | `0x40022000` |
| KEYR | `0x40023C04` | `0x40022004` |
| SR | `0x40023C0C` | `0x4002200C` |
| CR | `0x40023C10` | `0x40022010` |
| Option Bytes | OPTCR `0x40023C14` | `0x1FFFF800` (trực tiếp) |
| STRT bit | CR bit **16** | CR bit **6** |
| LOCK bit | CR bit **31** | CR bit **7** |
| Đơn vị ghi | **Word (32-bit)** | **Half-word (16-bit)** |
| Xóa | Sector (16KB–128KB) | Page (1KB hoặc 2KB) |
| Flash Size Register | `0x1FFF7A22` | `0x1FFFF7E0` |
| RDP byte | OPTCR bits [15:8] | `0x1FFFF800` |
| OBL_LAUNCH | **Không có** (phải power cycle) | Có |
| RDP Unlock | Ghi `0x0FFFAAEE` vào OPTCR | Ghi `0x00A5` vào `0x1FFFF800` |
