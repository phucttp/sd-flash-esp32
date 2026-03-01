# 🚀 Đặc Tả Engine Nạp STM32F1 qua SWD (Universal Flasher)

> **Phiên bản:** 1.0 — 2026-02-28
> **Tham chiếu:** `docs/stm32f4-swd-engine-spec.md` (cấu trúc tương đương cho F4)


Tài liệu này mô tả chi tiết các địa chỉ thanh ghi, cờ điều khiển, luồng logic và các bẫy phần cứng để giao tiếp với bộ điều khiển Flash (FPEC — Flash Program/Erase Controller) của STM32F1 thông qua giao thức SWD.

---

## 1. 🗄️ Bảng Định Nghĩa Thanh Ghi (Registers & Bits)

### Thanh Ghi FPEC (Base: `0x40022000`)

| Tên Thanh Ghi | Địa Chỉ (Hex) | Chức năng |
| :--- | :--- | :--- |
| **FLASH_ACR** | `0x40022000` | Access Control — wait states, prefetch buffer. |
| **FLASH_KEYR** | `0x40022004` | Nhập khóa mở FPEC (hạ cờ LOCK). |
| **FLASH_OPTKEYR** | `0x40022008` | Nhập khóa mở vùng Option Bytes (cho phép ghi/xóa OB). |
| **FLASH_SR** | `0x4002200C` | Trạng thái (Status Register). |
| **FLASH_CR** | `0x40022010` | Điều khiển (Control Register). |
| **FLASH_AR** | `0x40022014` | Địa chỉ Page cần xóa (F1-specific, F4 không có). |
| **FLASH_OBR** | `0x4002201C` | Option Byte Register — đọc trạng thái RDP, USER config. |
| **FLASH_WRPR** | `0x40022020` | Write Protection Register — mirror WRP bits từ OB. |

### Thanh Ghi Debug / System (Cortex-M3 — Chung với F4)

| Tên Thanh Ghi | Địa Chỉ (Hex) | Chức năng |
| :--- | :--- | :--- |
| **DHCSR** | `0xE000EDF0` | Debug Halting Control & Status — dừng/chạy CPU. |
| **DCRSR** | `0xE000EDF4` | Chọn register CPU để đọc/ghi qua DCRDR. |
| **DCRDR** | `0xE000EDF8` | Dữ liệu đọc/ghi register CPU. |
| **DEMCR** | `0xE000EDFC` | Debug Exception Monitor — VC_CORERESET. |
| **AIRCR** | `0xE000ED0C` | Application Interrupt & Reset Control — System Reset. |
| **DBGMCU_IDCODE** | `0xE0042000` | Chip identification — DEV_ID + REV_ID. |

### Thanh Ghi Bổ Sung

| Tên | Địa Chỉ (Hex) | Chức năng |
| :--- | :--- | :--- |
| **Flash Size** | `0x1FFFF7E0` | 16-bit, đơn vị KB. VD: `0x0080` = 128KB. |
| **Option Bytes** | `0x1FFFF800` | Vùng nhớ Option Bytes (16 bytes, memory-mapped). |

### 🔑 Các Giá Trị Khóa (Keys)

* **KEY1 (Flash):** `0x45670123`
* **KEY2 (Flash):** `0xCDEF89AB`
* **RDP_UNLOCK_CODE:** `0x00A5` (ghi vào `0x1FFFF800` để phá khóa RDP)
* **DHCSR_HALT:** `0xA05F0003` (Key + C_DEBUGEN + C_HALT)
* **DEMCR_VC_CORERESET:** `0x00000001`
* **SYSRESETREQ:** `0x05FA0004` (ghi vào AIRCR)

> ⚠️ **F1 dùng chung KEY1/KEY2 cho cả FLASH_KEYR lẫn FLASH_OPTKEYR** (khác F4 dùng OPTKEY1/OPTKEY2 riêng).

### 🚩 Các Cờ (Bits) Trong `FLASH_SR` (Trạng thái)

| Bit | Tên | Mô tả |
| :--- | :--- | :--- |
| 0 | **BSY** | Busy — 1 khi Flash đang xóa/ghi. **KHÔNG ghi CR khi BSY=1.** |
| 2 | **PGERR** | Programming Error — sai trình tự ghi (VD: ghi khi chưa set PG). |
| 4 | **WRPRTERR** | Write Protection Error — cố ghi/xóa vùng bị WRP khóa. |
| 5 | **EOP** | End of Operation — hoàn thành thao tác (write-1-to-clear). |

> ⚠️ **Sticky bits:** PGERR và WRPRTERR không tự reset — phải ghi 1 vào bit tương ứng để clear. Ghi `0x34` vào FLASH_SR để clear tất cả (`PGERR | WRPRTERR | EOP`).

### ⚙️ Các Cờ (Bits) Trong `FLASH_CR` (Điều khiển)

| Bit | Tên | Mô tả |
| :--- | :--- | :--- |
| 0 | **PG** | Programming — chế độ Ghi Flash (half-word). |
| 1 | **PER** | Page Erase — chế độ Xóa 1 Page. |
| 2 | **MER** | Mass Erase — chế độ Xóa toàn bộ Flash. |
| 4 | **OPTPG** | Option Byte Programming — chế độ Ghi Option Bytes. |
| 5 | **OPTER** | Option Byte Erase — chế độ Xóa Option Bytes. |
| 6 | **STRT** | Start — "bóp cò" thao tác Xóa (PER/MER/OPTER). |
| 7 | **LOCK** | Lock — 1 = FPEC bị khóa. Ghi KEY1+KEY2 vào KEYR để hạ. |
| 9 | **OPTWRE** | Option Write Enable — HW tự set khi unlock OPTKEYR thành công. |
| 12 | **EOPIE** | End of Operation Interrupt Enable. |
| 13 | **ERRIE** | Error Interrupt Enable. |

> 📝 **So sánh F4:** F1 không có PSIZE (program size), SNB (sector number), hay SPRMOD. F1 STRT = bit **6** (F4 = bit 16). F1 LOCK = bit **7** (F4 = bit 31).

### 🛡️ `FLASH_OBR` — Option Byte Register (Read-only)

| Bit(s) | Tên | Mô tả |
| :--- | :--- | :--- |
| 0 | **OPTERR** | Option Byte Error — complement check thất bại. |
| 1 | **RDPRT** | Read Protection — **1 = RDP Level 1 đang active.** |
| 9:2 | **USER** | Mirror 8 bit USER option byte. |

> 💡 **Ưu điểm so với F4:** Trên F1, `FLASH_OBR` nằm trên APB bus (peripheral) → **đọc được ngay cả khi RDP Level 1 đang active.** F4 không có thanh ghi tương đương dễ đọc.

### 📦 Option Bytes Memory Map (`0x1FFFF800` — 16 bytes)

| Địa chỉ | Nội dung (half-word) | Ghi chú |
| :--- | :--- | :--- |
| `0x1FFFF800` | `[nRDP : RDP]` | RDP byte + complement tự động |
| `0x1FFFF802` | `[nUSER : USER]` | User configuration |
| `0x1FFFF804` | `[nData0 : Data0]` | User data byte 0 |
| `0x1FFFF806` | `[nData1 : Data1]` | User data byte 1 |
| `0x1FFFF808` | `[nWRP0 : WRP0]` | Write Protection sector group 0 |
| `0x1FFFF80A` | `[nWRP1 : WRP1]` | Write Protection sector group 1 |
| `0x1FFFF80C` | `[nWRP2 : WRP2]` | Write Protection sector group 2 |
| `0x1FFFF80E` | `[nWRP3 : WRP3]` | Write Protection sector group 3 |

* **RDP = `0xA5`** → Level 0 (không bảo vệ) ✅
* **RDP ≠ `0xA5`** → Level 1 (chặn đọc Flash qua debug, cho phép ghi peripheral) ⚠️
* Hardware tự tính complement: `nRDP = ~RDP`. Chỉ cần ghi half-word, HW fill complement.

> ⚠️ **Khác F4:** F4 dùng thanh ghi `FLASH_OPTCR` (read/write). F1 dùng **vùng nhớ trực tiếp** tại `0x1FFFF800` — phải xóa toàn bộ OB trước khi ghi lại.

---

## 2. 📊 Bảng Page Flash STM32F1

### DBGMCU_IDCODE — Chip Identification (bits [11:0] = DEV_ID)

| DEV_ID | Variant | Page Size | Flash Range | SWD IDCODE |
| :--- | :--- | :--- | :--- | :--- |
| `0x412` | Low-density | 1 KB | 16–32 KB | `0x1BA01477` |
| `0x410` | Medium-density | 1 KB | 64–128 KB | `0x1BA01477` |
| `0x414` | High-density | 2 KB | 256–512 KB | `0x1BA01477` |
| `0x418` | Connectivity line | 2 KB | 64–256 KB | `0x1BA01477` |
| `0x420` | Value line MD | 1 KB | 64–128 KB | `0x1BA01477` |
| `0x428` | Value line HD | 2 KB | 128–512 KB | `0x1BA01477` |
| `0x430` | XL-density | 2 KB | 768 KB–1 MB | `0x1BA01477` |

> 💡 **SWD IDCODE `0x1BA01477`** = Cortex-M3 (tất cả F1 dùng chung). Phân biệt chip bằng DEV_ID.

### Quy Tắc Xác Định Page Size

```
flash_size_kb = read_halfword(0x1FFFF7E0)
if (flash_size_kb <= 128)
    page_size = 1024    // 1 KB
else
    page_size = 2048    // 2 KB
num_pages = (flash_size_kb * 1024) / page_size
```

### Ví Dụ: Medium-density 64KB (DEV_ID `0x410`, 1KB pages)

| Page | Địa chỉ bắt đầu | Kích thước |
| :--- | :--- | :--- |
| 0 | `0x08000000` | 1 KB |
| 1 | `0x08000400` | 1 KB |
| 2 | `0x08000800` | 1 KB |
| ... | ... | ... |
| 63 | `0x0800FC00` | 1 KB |

### Ví Dụ: High-density 256KB (DEV_ID `0x414`, 2KB pages)

| Page | Địa chỉ bắt đầu | Kích thước |
| :--- | :--- | :--- |
| 0 | `0x08000000` | 2 KB |
| 1 | `0x08000800` | 2 KB |
| 2 | `0x08001000` | 2 KB |
| ... | ... | ... |
| 127 | `0x0803F800` | 2 KB |

> 📝 **So sánh F4:** F4 xóa theo Sector (16KB–128KB, không đều). F1 xóa theo Page (1KB hoặc 2KB, đều nhau) → đơn giản hơn, linh hoạt hơn.

---

## 3. 🔍 Luồng 0: Phát Hiện RDP (Detect RDP Level)

*Mục tiêu: Xác định chip đang RDP Level 0 hay Level 1 trước khi quyết định hành động.*

1. **Init SWD & Connect:**
   * Gửi chuỗi JTAG-to-SWD → Đọc `IDCODE` (`0x1BA01477` = Cortex-M3).
2. **Halt CPU:**
   * Ghi `0xA05F0003` vào `DHCSR` (`0xE000EDF0`).
   * Ghi `0x00000001` vào `DEMCR` (`0xE000EDFC`).
3. **Identify Chip:**
   * Đọc `DBGMCU_IDCODE` (`0xE0042000`) → lấy DEV_ID (bits [11:0]).
   * VD: `0x414` = High-density.
4. **Đọc RDP Status:**
   * Đọc `FLASH_OBR` (`0x4002201C`).
   * Check bit 1 (`RDPRT`):
     * `1` → **Level 1** (Flash bị chặn đọc qua debug).
     * `0` → **Level 0** (không bảo vệ).

> 💡 **Ưu điểm lớn so với F4:** Trên F1, peripheral registers (bus APB) **vẫn đọc được khi RDP Level 1 active.** Không cần suy luận gián tiếp — đọc `FLASH_OBR` trực tiếp cho kết quả chính xác.
>
> Trên F4, khi RDP1, **mọi** AHB-AP read trả về `0x00000000` (kể cả peripheral) → phải suy đoán từ OPTCR = 0 → Level 1.

5. **Kiểm chứng (tùy chọn):**
   * Đọc `0x08000000` (đầu Flash).
   * Nếu Level 0: trả về dữ liệu thật.
   * Nếu Level 1: trả về lỗi bus hoặc `0x00000000` → xác nhận.
6. **Disconnect & Deinit.**

> 💡 **detect_rdp() cũng đóng vai trò warm-up** (giống F4): chu kỳ connect→read→disconnect "đánh thức" debug interface, cải thiện tỷ lệ kết nối thành công cho phiên tiếp theo.

---

## 4. 🚧 Luồng 1: Phá Khóa Đọc (RDP Disable — Mass Erase)

*Mục tiêu: Đưa RDP từ Level 1 về Level 0. Chip tự động Mass Erase toàn bộ Flash.*

> 💡 **Khác biệt quan trọng với F4:** Trên F1, peripheral registers **đọc được** dưới RDP1 → dùng luồng chuẩn (đọc-kiểm-ghi), **KHÔNG cần blind write** như F4!

### Trình tự chi tiết

1. **Init SWD & Connect → Halt CPU.**
2. **Mở khóa FPEC:**
   * Ghi `0x45670123` (KEY1) vào `FLASH_KEYR` (`0x40022004`).
   * Ghi `0xCDEF89AB` (KEY2) vào `FLASH_KEYR` (`0x40022004`).
   * ✅ **Kiểm tra:** Đọc `FLASH_CR` (`0x40022010`), đảm bảo bit `LOCK` (bit 7) = **0**.
3. **Mở khóa Option Bytes:**
   * Ghi `0x45670123` (KEY1) vào `FLASH_OPTKEYR` (`0x40022008`).
   * Ghi `0xCDEF89AB` (KEY2) vào `FLASH_OPTKEYR` (`0x40022008`).
   * ✅ **Kiểm tra:** Đọc `FLASH_CR`, đảm bảo bit `OPTWRE` (bit 9) = **1**.
   * ❌ Nếu `OPTWRE` = 0 → unlock thất bại → dừng, báo lỗi.
4. **Xóa Vùng Option Bytes:**
   * Ghi `FLASH_CR` = `OPTER` (bit 5 = 1, các bit khác = 0).
   * Ghi `FLASH_CR` = `OPTER | STRT` (bit 5 + bit 6).
   * ⏳ **Chờ:** Đọc `FLASH_SR`, chờ `BSY` (bit 0) = 0.
   * Ghi `FLASH_CR` = 0 (dọn dẹp cờ).
5. **Ghi Mã Mở Khóa RDP:**
   * Ghi `FLASH_CR` = `OPTPG` (bit 4 = 1).
   * ⚡ **Ghi half-word `0x00A5` vào `0x1FFFF800`** (CSW 16-bit — xem mục 6.1).
   * ⏳ **Chờ BSY = 0** — quá trình Mass Erase tự động bắt đầu ngay tại đây!
     * Mass Erase mất **3–8 giây** tùy flash size.
     * Dùng `yield()` / `vTaskDelay()` trong vòng lặp BSY để ESP32 WDT không timeout.
   * Ghi `FLASH_CR` = 0 (dọn dẹp).
6. **System Reset:**
   * Ghi `0x05FA0004` vào `AIRCR` (`0xE000ED0C`).
   * ⏳ Đợi 1–2 giây cho chip reset hoàn tất.

> 💡 **F1 reload Option Bytes qua System Reset** (SYSRESETREQ đủ). Không cần rút điện như F4!

7. **Disconnect & Deinit.**

### Luồng 1b: Thẩm Định Sau Reset (Verify)

1. **Reconnect:** Init → Connect → Halt.
2. **Đọc `FLASH_OBR`:** Check bit `RDPRT` = 0 → Level 0 confirmed ✅
3. **Đọc Flash `0x08000000`:**
   * `0xFFFFFFFF` → Flash đã xóa sạch ✅
   * Khác → Mass Erase chưa hoàn tất hoặc bị ngắt quãng → chạy **Rescue Mass Erase**.

### Luồng 1c: Rescue Mass Erase (khi chip đã Level 0 nhưng Flash chưa sạch)

1. **Mở khóa Flash:** KEY1 + KEY2 → `FLASH_KEYR`. Kiểm tra LOCK = 0.
2. **Clear sticky errors:** Ghi `0x34` vào `FLASH_SR`.
3. **Kích hoạt Mass Erase:**
   * Ghi `FLASH_CR` = `MER` (bit 2).
   * Ghi `FLASH_CR` = `MER | STRT` (bit 2 + bit 6).
4. **Chờ BSY = 0** (timeout 15 giây).
5. **Ghi `FLASH_CR` = 0.** Kiểm tra `FLASH_SR` lỗi.

---

## 5. 🚀 Luồng 2: Nạp Firmware Mới

*Mục tiêu: Xóa các Page cần thiết, nạp firmware mới theo Half-word (16-bit), xác thực on-the-fly.*

> **Yêu cầu:** Chip PHẢI ở RDP Level 0. Nếu Level 1 → chạy Luồng 1 trước.

### Bước 1: Đọc File & Quản Lý RAM (Dual Strategy)

| Chiến lược | Điều kiện | Ưu điểm | Nhược điểm |
| :--- | :--- | :--- | :--- |
| **Full-RAM** | `malloc(file_size)` OK | Nhanh, đóng SD ngay | FW ≤ RAM trống (~200KB) |
| **Streaming** | Full-RAM fail, `malloc(32KB)` | FW bất kỳ kích thước | Chậm hơn, giữ SD mở |

* Pad file đến bội số **2 byte** bằng `0xFF` (alignment half-word).
* Full-RAM: đọc hết → đóng file → nạp từ RAM.
* Streaming: giữ file mở, đọc 32KB/lần trong lúc nạp.

### Bước 2: Warm-up & Kiểm Tra RDP

* Gọi `detect_rdp()` → warm-up debug interface + xác nhận Level 0.
* Nếu Level 1/2 → dừng, báo lỗi.

### Bước 3: Kết Nối SWD & Identify Chip

* Init → Connect (retry 10 lần) → Halt → Select.
* Đọc `DBGMCU_IDCODE` → xác định DEV_ID.
* Đọc flash size (`0x1FFFF7E0`) → tính page_size (1KB hoặc 2KB).
* Validate: firmware ≤ flash_size.

### Bước 4: Xóa Flash (Page Erase)

Với mỗi Page bị firmware phủ:

1. Mở khóa Flash (KEY1 + KEY2 → `FLASH_KEYR`). Kiểm tra `LOCK` = 0.
2. Clear sticky errors: ghi `0x34` vào `FLASH_SR`.
3. Ghi `FLASH_CR` = `PER` (bit 1) — phải set PER **trước** khi ghi AR.
4. Ghi **địa chỉ page** vào `FLASH_AR` (`0x40022014`).
5. Ghi `FLASH_CR` = `PER | STRT` (bit 1 + bit 6).
6. ⏳ Chờ `BSY` = 0.
7. Kiểm tra `FLASH_SR`: nếu `WRPRTERR` (bit 4) = 1 → page bị write-protect → lỗi.
8. Ghi `FLASH_CR` = 0 (dọn dẹp).

> 📝 **Khác F4:** F4 dùng sector number (SNB) trong `FLASH_CR`. F1 dùng thanh ghi `FLASH_AR` riêng để chỉ định page address.

### Bước 5: Nạp + Verify On-the-fly (Chunk 256 byte)

> **Tại sao 256B?** Cân bằng overhead (unlock/lock mỗi chunk) với khả năng retry khi lỗi. 256B = 128 half-words.

Với mỗi chunk 256B:

1. **Mở khóa Flash** (KEY1 + KEY2 nếu đã bị lock lại).
2. **Set PG:** Ghi `FLASH_CR` = `PG` (bit 0).
3. **⚡ Chuyển CSW sang 16-bit:**
   * Đọc CSW hiện tại: `csw = dap_read_reg(SWD_AP_CSW)`.
   * Set 16-bit: `dap_write_reg(SWD_AP_CSW, (csw & ~0x07) | 0x01)`.
   * **Chỉ làm 1 lần đầu chunk** (không toggle mỗi half-word!).
4. **Vòng lặp ghi 128 half-words:**
   ```
   for i = 0 to 127:
       data16 = buf[2*i] | (buf[2*i+1] << 8)          // Little-endian
       safe_data = data16 | (data16 << 16)              // ⚠️ Replicate (xem 6.1)
       dap_write_word(flash_addr + i*2, safe_data)
       // ⚠️ KHÔNG gọi flash_busy() ở đây! (xem 6.2)
   ```
5. **Restore CSW 32-bit:**
   * `dap_write_reg(SWD_AP_CSW, (csw & ~0x07) | 0x02)`.
6. **Dọn dẹp:** Ghi `FLASH_CR` = 0 (tắt PG).
7. **Delay:** `vTaskDelay(1)` — ổn định SWD bus.
8. **Verify chunk:**
   * Gọi `dap_read_block(flash_addr, verify_buf, 256)` (đọc 32-bit, CSW đã restore).
   * `memcmp(data, verify_buf, 256)`.
9. **Nếu mismatch → Retry tại chỗ (tối đa 3 lần).**
   * Ghi lại chunk → delay → verify.
   * Thực tế: ~3/72 chunks cần 1 retry (tỷ lệ `dap_write_word` fail ≈ 0.1%).
10. **Nếu retry 3 lần vẫn fail → Re-flash toàn bộ (tối đa 3 attempts).**
    * Erase lại → Nạp từ đầu.

### Bước 6: Hoàn Tất

* Ghi `FLASH_CR` = `LOCK` (bit 7) để khóa FPEC.
* Disconnect + Deinit.
* Ghi `0x05FA0004` vào `AIRCR` → Reset chip chạy firmware mới.

---

## 6. ⚠️ Bẫy & Lưu Ý Quan Trọng (Lessons Learned)

### 6.1 CSW 16-bit — Byte Lane Striping

Khi `CSW.SIZE = 16-bit` (0b001), vị trí data trong thanh ghi DRW (32-bit) **phụ thuộc địa chỉ**:

| Địa chỉ | Byte Lanes Active | Data trong DRW |
| :--- | :--- | :--- |
| `addr & 2 == 0` (VD: 0x08000000) | Lanes 0–1 | Bits [15:0] |
| `addr & 2 == 2` (VD: 0x08000002) | Lanes 2–3 | Bits [31:16] |

Nếu chỉ truyền `uint16_t` zero-extended (bits [15:0]) → **mọi half-word ở địa chỉ lẻ sẽ ghi `0x0000` thay vì data thật!**

**Fix — Replicate pattern:**
```cpp
// AN TOÀN cho mọi alignment
uint32_t safe = (uint32_t)data16 | ((uint32_t)data16 << 16);
dap_write_word(addr, safe);
```

Data nằm trên cả hai nửa DRW → AHB-AP tự chọn nửa đúng theo địa chỉ.

> ⚠️ **Đây là bug ẩn trong draft stm_cpp.txt** — `write_half_word()` truyền `data` trần → fail 50% half-words (tất cả addr lẻ). Verify sẽ bắt được nhưng retry vô ích vì bug nằm ở logic, không phải SWD timing.

### 6.2 flash_busy() Trong Inner Loop → CSW Auto-Increment Trap

**Hoàn toàn giống F4.** Thanh ghi F1 cũng nằm liền nhau:

```
FLASH_SR  = 0x4002200C
FLASH_CR  = 0x40022010   ← SR + 4 bytes
FLASH_AR  = 0x40022014   ← CR + 4 bytes
```

CSW có `ADDRINC_SINGLE` → đọc SR xong, TAR tự tăng lên CR. Nếu DRW write tiếp theo chưa set TAR mới → ghi nhầm vào `FLASH_CR` → set `LOCK` → cascade fail.

**Fix:** Bỏ `flash_busy()` khỏi inner write loop. SWD bit-bang (~144μs/transaction) chậm hơn nhiều so với F1 flash write time (~40–70μs) → BSY luôn clear trước khi half-word tiếp theo đến.

### 6.3 CSW Toggle — Tối Ưu Ở Level programBlock()

Draft `write_half_word()` toggle CSW **mỗi half-word** = 4 SWD transactions overhead × 128 = 512 transactions/chunk chỉ cho toggle.

**Fix:** Toggle CSW **1 lần ở đầu/cuối `programBlock()`**:
* Đầu chunk: set CSW 16-bit (1 transaction)
* Ghi 128 half-words
* Cuối chunk: restore CSW 32-bit (1 transaction)
* **Tiết kiệm 510 SWD transactions/chunk.**

### 6.4 F1 RDP Level 1 — Peripheral Bus Vẫn Truy Cập Được

| Vùng nhớ | F4 (RDP1) | F1 (RDP1) |
| :--- | :--- | :--- |
| Flash (0x08000000) | ❌ Trả về 0 | ❌ Bus error / 0 |
| SRAM (0x20000000) | ❌ Trả về 0 | ❓ Tùy variant |
| Peripheral (0x40000000) | ❌ Trả về 0 | ✅ **Đọc được** |
| PPB/CoreSight (0xE0000000) | ✅ Đọc được | ✅ Đọc được |

**Hệ quả cho F1:** Không cần blind write. Có thể đọc `FLASH_CR`, check `OPTWRE`, monitor `BSY` trong suốt quá trình RDP unlock. Đơn giản và đáng tin cậy hơn F4 rất nhiều.

> ⚠️ **Cần kiểm chứng trên hardware thực** — behavior có thể khác theo F1 variant (F100 vs F103 vs F105).

### 6.5 dap_write_word() Silent Fail (~0.1%)

Giống hệt F4:
* ESP32-C3 bit-bang SWD: ~1/1000 writes fail silently.
* `dap_write_word()` luôn return `true` (bỏ qua SWD ACK).
* **Bắt buộc** verify on-the-fly sau mỗi 256B chunk.

### 6.6 FLASH_SR Sticky Error Bits

* `PGERR` (bit 2) và `WRPRTERR` (bit 4) là sticky — một khi set, **mọi** thao tác tiếp theo fail.
* **Phải ghi `0x34`** vào `FLASH_SR` (write-1-to-clear) trước **mỗi page erase**.
* Không cần clear trong inner write loop.

### 6.7 F1 Option Byte Reload — Không Cần Power Cycle

* F1 reload Option Bytes trên **mọi system reset** (kể cả SYSRESETREQ).
* Khác F4: F4 **chỉ** reload OB khi **power-on reset** (SYSRESETREQ không đủ → phải rút điện).
* Ưu điểm F1: RDP unlock flow đơn giản hơn, không yêu cầu user intervention.

### 6.8 Page Size Phụ Thuộc Flash Density

* ≤ 128KB → 1KB pages. > 128KB → 2KB pages.
* **PHẢI đọc flash size register** (`0x1FFFF7E0`) để xác định page size.
* Hardcode page size = bug tiềm ẩn khi đổi chip target.

### 6.9 FLASH_AR — Thanh Ghi Địa Chỉ Riêng (F1-specific)

* F1 dùng `FLASH_AR` (`0x40022014`) để chỉ định page cần xóa.
* **Phải ghi FLASH_AR TRƯỚC khi set STRT** trong FLASH_CR.
* F4 không có thanh ghi này — F4 encode sector number trực tiếp trong CR (bits SNB).

### 6.10 Shared Traps Với F4 (Áp Dụng Cho Mọi SWD Engine)

* **GPIO2/FSPIQ conflict:** GPIO2 = default FSPIQ trên ESP32-C3. SD card SPI phá SWD. → Dùng GPIO0 cho SWDIO.
* **5V vs 3.3V:** Cấp 5V cho target → VIH mismatch + có thể hư GPIO ESP32-C3. → Luôn dùng 3.3V.
* **detect_rdp() warm-up:** Chu kỳ connect-read-disconnect trước phiên chính cải thiện tỷ lệ thành công.
* **SYSRESETREQ giữa retry:** Nếu key sequence fail, flash controller kẹt state machine → phải reset trước khi thử lại.

---

## 7. 📈 So Sánh Nhanh F1 vs F4

| Đặc điểm | STM32F1 | STM32F4 |
| :--- | :--- | :--- |
| CPU Core | Cortex-M3 | Cortex-M4 |
| SWD IDCODE | `0x1BA01477` | `0x2BA01477` |
| Flash Controller Base | `0x40022000` | `0x40023C00` |
| KEYR | `0x40022004` | `0x40023C04` |
| SR | `0x4002200C` | `0x40023C0C` |
| CR | `0x40022010` | `0x40023C10` |
| Option Bytes | Memory-mapped `0x1FFFF800` | Register `OPTCR` (`0x40023C14`) |
| STRT bit | CR bit **6** | CR bit **16** |
| LOCK bit | CR bit **7** | CR bit **31** |
| Đơn vị ghi Flash | **Half-word (16-bit)** | **Word (32-bit)** |
| CSW SIZE khi ghi | `0x01` (16-bit) | `0x02` (32-bit) |
| Xóa Flash | **Page** (1KB hoặc 2KB) | **Sector** (16KB–128KB) |
| Địa chỉ xóa | Thanh ghi `FLASH_AR` riêng | Encode trong CR (SNB bits) |
| Flash Size Register | `0x1FFFF7E0` (16-bit) | `0x1FFF7A22` (16-bit) |
| RDP check | `FLASH_OBR` bit 1 (RDPRT) | `OPTCR` bits [15:8] |
| RDP unlock value | `0x00A5` vào `0x1FFFF800` | `0x0FFFAAEE` vào `OPTCR` |
| Peripheral read dưới RDP1 | ✅ **Đọc được** | ❌ Trả về 0 |
| RDP unlock approach | **Chuẩn** (đọc-kiểm-ghi) | **Blind write** (không đọc) |
| OB reload | SYSRESETREQ đủ | **Phải power cycle** |
| SPRMOD / PCROP trap | Không có | Có (bit 31 OPTCR) |
| Option Key | **Dùng chung** KEY1/KEY2 | OPTKEY1/OPTKEY2 riêng |
| Mass Erase sau RDP | Tự động khi ghi 0xA5 | Tự động khi OPTSTRT |

---

## 8. 🏗️ Ghi Chú Triển Khai — Class Design

### 8.1 Kế Thừa Từ `Adafruit_DAP` (Base Class)

```
Adafruit_DAP (base — SWD protocol, dap_write_word, dap_read_block...)
├── Adafruit_DAP_STM32    ← F4 (đã có)
├── Adafruit_DAP_STM32F1  ← F1 (class mới)
├── Adafruit_DAP_SAM
└── Adafruit_DAP_nRF5x
```

**Lý do KHÔNG kế thừa từ `Adafruit_DAP_STM32` (F4):**
* Register addresses conflict (F1 base `0x40022xxx` vs F4 `0x40023Cxx`).
* CR bit layout hoàn toàn khác (STRT, LOCK, PG, SER vs PER...).
* Program size: F1 = 16-bit, F4 = 32-bit — logic `programBlock()` khác nhau.
* Tận dụng code F4 ít (chỉ `verifyFlash()` giống, còn lại khác hết).

### 8.2 Virtual Functions Bắt Buộc Override

Class `Adafruit_DAP` khai báo các **pure virtual functions** — phải implement hết, không compile được nếu thiếu:

| Function | Mô tả triển khai F1 |
| :--- | :--- |
| `getTypeID()` | Return `DAP_TYPEID_STM32` (hoặc define mới). |
| `select(uint32_t *id)` | Đọc `DBGMCU_IDCODE`, whitelist DEV_ID F1 (`0x410`, `0x412`, `0x414`...). |
| `deselect()` | Cleanup — lock flash, clear state. |
| `erase()` | Mass Erase: MER + STRT, chờ BSY. |
| `program_start(addr, size)` | Tính pages cần xóa → erase_page() loop. |
| `programBlock(addr, buf, size)` | CSW 16-bit → half-word loop → restore CSW 32-bit. |
| `programFlash(addr, buf, count, verify)` | Wrapper: programBlock() + verify on-the-fly. |
| `protectBoot()` | Set WRP bits trong Option Bytes. |
| `unprotectBoot()` | Clear WRP bits / Mass unlock WRP. |

### 8.3 Hàm Mới F1-Specific

| Hàm | Chức năng |
| :--- | :--- |
| `flash_unlock()` | KEY1+KEY2 → KEYR, **kiểm tra** LOCK = 0 (F4 không verify). |
| `flash_lock()` | Set LOCK bit trong CR. |
| `flash_busy()` | Đọc SR bit BSY. **Chỉ dùng ngoài inner loop.** |
| `flash_clear_errors()` | Ghi `0x34` vào SR để clear sticky bits. |
| `erase_page(addr)` | FLASH_AR ← addr, CR = PER\|STRT, chờ BSY. |
| `write_half_word(addr, data)` | CSW 16-bit + replicate data + DRW write. |
| `unlockRDP()` | Full OB erase + ghi 0xA5 flow. |
| `detect_rdp()` | Đọc FLASH_OBR bit RDPRT. |

### 8.4 File Mới Cần Tạo (Không sửa thư viện gốc)

```
components/Adafruit_DAP/
├── Adafruit_DAP.h              ← thêm #include "Adafruit_DAP_STM32F1.h"
├── Adafruit_DAP.cpp            ← KHÔNG SỬA
├── Adafruit_DAP_STM32.h        ← KHÔNG SỬA (F4)
├── Adafruit_DAP_STM32.cpp      ← KHÔNG SỬA (F4)
├── Adafruit_DAP_STM32F1.h      ← MỚI
└── Adafruit_DAP_STM32F1.cpp    ← MỚI
```

> ⚠️ Chỉ sửa `Adafruit_DAP.h` để thêm 1 dòng `#include`. Toàn bộ logic F1 nằm trong 2 file mới.
