# I²C Master ↔ Phost — Kinh Nghiệm Debug

> Tổng hợp các lỗi & cách fix khi làm cho bus I²C master ↔ phost ổn định lúc
> nạp/xóa firmware (SWD bit-bang) chạy song song nhiều slave.
> Platform: ESP32-C3 (RISC-V, **single-core**) | ESP-IDF v5.1.6 + Arduino (Wire).
> Bus: master = I²C master, mỗi phost = I²C slave (Arduino Wire slave), 0x10–0x19.

---

## Triệu chứng tổng quát

- Khi nạp/xóa (SWD), **thường 1–2 slave mất kết nối** I²C — master báo
  `[Wire.cpp:522] requestFrom(): i2cRead returned Error 263` (= `ESP_ERR_TIMEOUT`).
- Có timeout/watchdog rồi mà đôi khi **không tự kết nối lại**, chỉ **nhấn RST cứng**
  trên phost mới lên lại.
- Slave bị rớt thường **ngẫu nhiên** (con nào bị master poll trúng lúc bit-bang).

---

## 1. Nguyên nhân gốc: SWD bit-bang làm đói (starve) I²C slave trên single-core

**Mức độ:** CRITICAL — gốc rễ của hầu hết mọi triệu chứng.

ESP32-C3 **một core**. Khi phost bit-bang SWD nạp chip đích (~5s, mỗi chunk 256B
chỉ nhường `vTaskDelay(1)` = 1ms), task `gangf_disp` độc chiếm CPU0:
- **I²C slave callback task bị đói** → khi master đọc `READ_STATUS`, slave hardware
  không kịp điền TX buffer → **clock-stretch** (giữ SCL LOW). SCL là **dây chung** →
  một slave stretch làm **kẹt cả bus** → master `Error 263` trên *mọi* phost.
- **IDLE0 cũng bị đói** → `task_wdt` nổ (xem mục 5).

**Bằng chứng:** register dump lúc `task_wdt` nổ → `CPU 0: gangf_disp`, kẹt trong
`uart_write/console_write` (busy-wait xả log) hoặc trong `Adafruit_DAP` bit-bang.

**Lưu ý quan trọng:** comment cũ nói *"portENTER_CRITICAL trong DAP driver mask I2C
ISR"* là **SAI** — SWD dùng **direct register write** (`GPIO.out_w1ts`), không hề
disable interrupt. Vấn đề là **task starvation**, không phải ISR masking.

---

## 2. `esp_restart()` KHÔNG reset I²C peripheral trên C3 — chỉ HW RST mới clear

**Mức độ:** CRITICAL — lý do "reset mềm không cứu, nhấn RST cứng mới lên".

Sau khi SWD bit-bang làm wedge I²C peripheral, `esp_restart()` (software reset) chỉ
reset CPU + phần lớn digital core, **KHÔNG reset hoàn toàn I²C peripheral + GPIO**.
→ boot lại, `Wire.begin(slave)` gặp peripheral còn wedge →
`i2c_slave_check_line_state(): Bus Invalid State, Can't init sda=1, scl=1` → init FAIL
→ slave không bao giờ join lại. Chỉ **RST cứng** (nút/cấp lại nguồn) mới reset sạch.

**Cách clear bằng phần mềm (tương đương HW RST):**
`periph_module_reset(PERIPH_I2C0_MODULE)` + `gpio_reset_pin(SDA/SCL)` trước
`Wire.begin()`. (Trong bản ship dùng phương án retry — xem mục 4 — nhưng đây là
bài học cốt lõi: **esp_restart ≠ HW reset cho peripheral**.)

---

## 3. `Wire.end()` + `Wire.begin()` KHÔNG recover được slave wedge

**Mức độ:** HIGH.

Đừng kỳ vọng `Wire.end()` rồi `Wire.begin()` cứu được I²C slave đã wedge — nó fail
`Bus Invalid State, sda=1 scl=1` (cả 2 line đang HIGH/idle mà vẫn từ chối init).
Hệ quả: watchdog/recover dựa trên Wire re-init **vô dụng** sau SWD → đã chuyển
`gangf_i2c_slave_recover()` thành **no-op** (reboot mới là recovery thật).

---

## 4. `Wire.begin(slave)` fail lúc boot nếu bus chung đang bận → đừng HALT, hãy RETRY

**Mức độ:** HIGH — gây "một con không vào được bus luôn".

Khi phost boot (đặc biệt sau esp_restart hậu-flash), master đang poll → SDA/SCL bị
giữ vài ms đúng lúc đó → `i2c_slave_check_line_state` báo "Bus Invalid State" → init
fail. Code cũ `return` → **app_main thoát → phost chết vĩnh viễn** tới khi RST cứng.

**Fix (đã ship):** retry `Wire.begin()` ~20 lần, mỗi lần cách 100ms. Bus có khoảng
idle giữa các transaction ngắn của master → trong ~1s sẽ lọt vào một khoảng → init OK.

---

## 5. `task_wdt` nổ lúc flash — CẢNH BÁO vô hại, KHÔNG phải bug

**Mức độ:** LOW (gây hiểu nhầm).

`E task_wdt: ... IDLE (CPU 0) did not reset ... CPU 0: gangf_disp` — do mục 1 (đói
CPU). Nhưng `CONFIG_ESP_TASK_WDT_PANIC is not set` → **chỉ in log + register dump,
KHÔNG reset, KHÔNG ảnh hưởng mạch**. Flash vẫn xong. Là *triệu chứng* của starvation,
không phải nguyên nhân.

> ⚠ **TUYỆT ĐỐI không bật `CONFIG_ESP_TASK_WDT_PANIC`** — sẽ reset GIỮA flash → hỏng.

---

## 6. Giải pháp ship: reboot-after-flash + replay kết quả qua RTC SRAM

**Mức độ:** đây là cơ chế chính.

Vì I²C wedge sau SWD chỉ clear được bằng reset peripheral (mục 2):
1. Flash/erase xong → lưu `{state, error, fw_id}` vào **RTC SRAM** (sống qua reboot,
   không cần ghi NVS).
2. `esp_restart()` → boot lại → I²C peripheral sạch → `Wire.begin(slave)` (retry, mục 4)
   thành công → slave rejoin bus.
3. `status_reporter` **replay** kết quả từ RTC → master đọc `DONE/ERROR` sau ~2-3s.
4. Master `OFFLINE_AFTER` debounce (30 nhịp) **nuốt êm** khoảng reboot — slot không
   nháy offline.

Erase nhanh hơn flash vì op ngắn hơn (~1s vs ~5s) → cửa sổ "mất tích" ngắn hơn.

---

## 7. master `bus_recover` false-trigger — "bug" lại có tác dụng phụ CÓ ÍCH

**Mức độ:** MEDIUM — bài học về side-effect.

`master_transact` đếm `s_consec_fail`; ≥6 fail liên tiếp → coi là "bus wedged" →
`do_9clk_recovery()` (9-clock + re-init Wire). Vì `try_once` gộp **mọi** lỗi
endTransmission (kể cả **addr-NACK của slot trống** 0x14–0x17) thành `ESP_ERR_TIMEOUT`,
nên với nhiều slot trống → **luôn đủ 6 fail → false "bus wedged" mỗi scan**.

- Thử fix: phân biệt `txerr==2` (addr NACK = slot trống, bus khỏe → `ESP_ERR_NOT_FOUND`,
  reset counter) vs `4/5` (bus error thật). → flash recover mượt hơn.
- **NHƯNG:** sau fix, slave **khó join lúc khởi động** hơn. Vì cái `do_9clk_recovery()`
  chạy thường xuyên (do false-trigger) **vô tình unstick** slave đang giữ SDA lúc boot.
  Bỏ nó đi → con kẹt không được kéo SDA về → không lên.
- **Quyết định:** **revert**, giữ hành vi cũ (churn nhưng startup ổn). 

→ **Bài học:** trước khi "sửa" một hành vi xấu, kiểm tra nó có đang vô tình gánh việc
khác không. Hướng đúng về sau: tách 9-clock-unstick theo **per-slave miss-streak**
(targeted) thay vì dựa vào global counter sai.

---

## 8. Off-bus during flash (`Wire.end()` trong `set_swd_active`) — ĐÃ THỬ, BỎ

Ý tưởng: gỡ slave khỏi bus lúc flash (Wire.end) để không jam bus chung. Nhược: Wire
re-init sau đó không tin cậy (mục 3) + mất live progress + master_ui báo sai (LED xanh,
progress giả). Đã bỏ, dùng reboot (mục 6).

---

## 9. Các điểm linh tinh

- **Hạ bus 400kHz → 100kHz:** dùng để loại trừ signal integrity. Bus chỉ tải lệnh +
  status (không tải bulk firmware — phost tự kéo qua WiFi) nên 100k thừa sức; giữ luôn
  cho margin tín hiệu.
- **"Giữa bị nhiều nhất" (0x11,0x12) gây hiểu nhầm signal-integrity:** thực ra là
  **race timing** (con nào bị master poll trúng lúc bit-bang thì wedge), không phải vị
  trí vật lý.
- **master_ui poll modal:** khi slave off-bus/đang bận, seed `last_state` = busy state
  để LED đúng màu ngay; hiển thị "busy..." tĩnh (KHÔNG animation — animation gây cảm
  giác chậm).

---

## Tóm tắt chuỗi fix đã ship

| Vùng | Fix |
|---|---|
| Phost | Sau flash/erase → lưu kết quả RTC SRAM → `esp_restart()` |
| Phost | Boot: retry `Wire.begin(slave)` ~20×100ms thay vì halt |
| Phost | `gangf_i2c_slave_recover()` → no-op (reboot mới là recovery) |
| Master | `bus_recover` đếm-mọi-fail (giữ 9-clock định kỳ giúp slave join lúc boot) |
| Cả 2 | Bus 100kHz |

**Một câu chốt:** SWD bit-bang single-core làm đói I²C slave → wedge; wedge chỉ clear
bằng reset peripheral (`esp_restart` + boot lại), KHÔNG bằng `Wire.end/begin`.
