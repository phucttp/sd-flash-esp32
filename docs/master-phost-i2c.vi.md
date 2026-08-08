# Giao thức I²C Master ↔ Phost

Hợp đồng giao tiếp giữa **master ESP32-C3** và các **phost slave**
(thiết bị burn firmware, nạp xuống chip đích qua SWD/UART).
Master chạy bus I²C; mỗi phost là một slave trên bus đó.

- **Phiên bản giao thức:** 1
- **Transport:** I²C, địa chỉ 7-bit, **Fast mode 400 kHz**
- **Vai trò bus:** Master = I²C master (chỉ master mới initiate), Phost = I²C slave (chỉ trả lời)
- **Số phost tối đa trên 1 bus:** 10
- **Mô hình đồng thời:** Song song (master gửi lệnh, phost chạy job nền, master poll status)

PC không bao giờ nói chuyện trực tiếp với phost. PC → Master dùng WebSocket
(xem [master-api.md](master-api.md)); Master → Phost dùng giao thức này.

```
PC ──WS──► Master ──I²C──► Phost ──SWD/UART──► Chip đích
                            ▲
                            └──WiFi──► FW Library Server (phost tự kéo bản riêng)
```

---

## 1. Topology Bus

| Thuộc tính | Giá trị |
|---|---|
| Tốc độ | 400 kHz (Fast mode) |
| Điện trở kéo lên | 4.7 kΩ trên SDA + SCL, kéo lên 3.3 V |
| Số device tối đa | 10 phost (địa chỉ 0x10–0x19) |
| Đi dây | Ưu tiên daisy-chain, tổng chiều dài < 50 cm |
| Ngân sách điện dung bus | ≤ 400 pF (10 phost ≈ 100 pF, an toàn) |
| Clock stretching | Bật ở phía phost (cho phép tối đa 10 ms) |

> **Lưu ý cho bus dài / jig sản xuất:** nếu layout buộc phải dài > 50 cm,
> hoặc đi dây qua switch / pogo-pin (thường thấy ở gang-flash jig — nơi
> điện dung ký sinh tích tụ rất nhanh), nên thêm IC buffer I²C
> (PCA9517 / PCA9515) giữa master và chuỗi phost. Trường hợp tải ký sinh
> nghiêm trọng — đo rise time của SDA trên oscilloscope; nếu > 1 µs ở
> 400 kHz tức là bus đang ở ngưỡng — chia thành các đoạn bằng I²C
> multiplexer (TCA9548A) để mỗi đoạn chỉ chứa 2–4 phost. Phương án cuối:
> hạ xuống 100 kHz.

### Address Map

```
0x00–0x07   I²C reserved
0x08–0x0F   dành riêng cho master sử dụng nội bộ
0x10–0x19   10 slot phost (PHOST_01 … PHOST_10)
0x1A–0x77   trống, mở rộng sau / sensor on-board nếu cần
0x78–0x7F   I²C reserved
```

Mỗi phost lưu địa chỉ của chính nó trong NVS. Việc provisioning (qua tab
**Phost Setup** trong FlashPorter) sẽ gán slot trống đầu tiên trong `0x10–0x19`.

---

## 2. Frame Format

Mỗi giao dịch I²C đi 1 chiều (ghi xuống phost HOẶC đọc từ phost).
Không có length prefix trên dây — cả 2 bên đều biết size theo command
dựa vào bảng tra cứu cố định (xem [§4](#4-command-reference)).

### Master → Phost (write transaction)

```
[CMD_CODE][PAYLOAD...][CRC8]
    1B       0–30B      1B
```

### Phost → Master (read transaction)

```
[STATUS_BYTE][DATA...][CRC8]
     1B        0–30B    1B
```

Giá trị `STATUS_BYTE`:

| Code | Ý nghĩa |
|---|---|
| 0x00 | OK — data đi kèm phía sau, CRC hợp lệ |
| 0x01 | BUSY — phost đang bận, không nhận lệnh mới được |
| 0x02 | INVALID_CMD — mã command lạ |
| 0x03 | CRC_FAIL — command nhận trước đó CRC sai, đã bỏ |
| 0x04 | ERROR — payload chứa chi tiết lỗi (xem [§6](#6-error-codes)) |

### CRC8

- Polynomial: `0x07` (CRC-8/CCITT)
- Initial value: `0x00`
- Phủ toàn bộ byte trong frame **trừ** chính byte CRC
- Frame có CRC sai bị bỏ thầm lặng (master phải timeout và retry)

### Tại sao chọn size cố định theo command (không dùng length-prefix)?

Master I²C phải chỉ định đọc bao nhiêu byte từ slave **trước khi** read
bắt đầu. Length-prefix sẽ ép phải làm 2 read transaction riêng biệt (1
đọc length, 1 đọc body) → gấp đôi overhead. Size cố định theo command
cho phép master đọc tất cả trong 1 transaction ngay khi biết mã command
mình vừa gửi.

---

## 3. Quy tắc Encoding

- **Endianness:** little-endian cho mọi số nhiều byte
- **Strings:** ASCII, đệm null phía sau, độ dài slot cố định (không bắt buộc null-terminator)
- **Booleans:** 1 byte, 0 = false, khác 0 = true
- **Byte reserved:** phải gửi là `0x00`, bên nhận bỏ qua khi đọc

---

## 4. Command Reference

| Code | Tên | Cmd size | Resp size | Mục đích |
|------|-----|----------|-----------|----------|
| 0x01 | `PING` | 2 | 2 | Kiểm tra phost còn sống, dùng trong discovery |
| 0x02 | `GET_INFO` | 2 | 18 | Thông tin tĩnh (model, fw version, uptime) |
| 0x03 | `READ_STATUS` | 2 | 34 | Snapshot trạng thái hiện tại (command poll thường xuyên nhất) |
| 0x10 | `FLASH_FW` | 3 | 2 | Bắt đầu flash `fw_id` (job chạy nền) |
| 0x11 | `ERASE_TARGET` | 2 | 2 | Erase flash của chip đích |
| 0x12 | `REBOOT_TARGET` | 2 | 2 | Pulse chân reset của chip đích |
| 0x13 | `ABORT` | 2 | 2 | Hủy job đang chạy |
| 0x20 | `SET_LABEL` | 18 | 2 | Đặt label người-đọc-được 16 ký tự (lưu NVS) |
| 0x21 | `SET_TARGET_TYPE` | 3 | 2 | Báo cho phost biết chip đích đang đấu là loại gì |
| 0x30 | `SYNC_LIBRARY` | 2 | 2 | Yêu cầu phost kéo manifest qua WiFi |
| 0x31 | `GET_LIBRARY` | 2 | 34 | Liệt kê FW có sẵn trên phost (trang đầu) |
| 0x32 | `GET_LIBRARY_NEXT` | 3 | 34 | Trang tiếp theo của list FW (`offset` byte) |

Cmd/Resp size đã bao gồm byte CRC8 ở cuối.

### 4.1 PING (0x01)

```
Cmd:  [0x01][CRC]                          → 2 bytes
Resp: [STATUS=0x00][CRC]                   → 2 bytes
```

Master dùng lệnh này để scan địa chỉ lúc khởi động và để phục hồi sau
thời gian im lặng dài.

### 4.2 GET_INFO (0x02)

```
Cmd:  [0x02][CRC]                          → 2 bytes
Resp: [STATUS][model:1][fw_major:1][fw_minor:1][fw_patch:1]
      [uptime_sec:4][serial:8][CRC]        → 18 bytes
```

Code `model`:

| Code | Model |
|---|---|
| 0x01 | PHOST_V1 (ESP32-C3 standalone) |
| 0x02 | PHOST_V2 (dự kiến) |

### 4.3 READ_STATUS (0x03)

```
Cmd:  [0x03][CRC]                          → 2 bytes
Resp: [STATUS][PhostStatus:32][CRC]        → 34 bytes
```

`PhostStatus` là 32 byte — xem [§5](#5-phoststatus-struct).
Đây là lệnh "ngựa kéo" của giao thức; master gọi nó 1 Hz lúc idle / 5 Hz
khi active, trên từng phost.

### 4.4 FLASH_FW (0x10)

```
Cmd:  [0x10][fw_id:1][CRC]                 → 3 bytes
Resp: [STATUS][CRC]                        → 2 bytes
```

- `fw_id` chỉ tới một entry trong thư viện FW local của phost
  (xem `GET_LIBRARY`).
- Nếu `fw_id` không có trong thư viện, response trả `STATUS = 0x04 ERROR`
  và `READ_STATUS` ngay sau đó sẽ báo `error_code = 0x20 FW_NOT_FOUND`.
- Nếu `STATUS = 0x00`, job đã được **nhận và bắt đầu chạy**. Master phải
  poll `READ_STATUS` để theo dõi tiến độ.

### 4.5 ERASE_TARGET (0x11) / REBOOT_TARGET (0x12) / ABORT (0x13)

Cùng dạng với `FLASH_FW` nhưng không có payload:

```
Cmd:  [CODE][CRC]                          → 2 bytes
Resp: [STATUS][CRC]                        → 2 bytes
```

Tất cả đều fire-and-forget — chúng đẩy việc vào queue, master poll để
biết hoàn tất. `ABORT` hủy bất kỳ job đang chạy nào (flash, erase, sync).
Idempotent khi phost đang idle.

### 4.6 SET_LABEL (0x20)

```
Cmd:  [0x20][label:16][CRC]                → 18 bytes
Resp: [STATUS][CRC]                        → 2 bytes
```

Label 16 byte ASCII, đệm null nếu ngắn hơn. Lưu xuống NVS; tồn tại qua
reboot. Hiển thị trong card UI NetFlash.

### 4.7 SET_TARGET_TYPE (0x21)

```
Cmd:  [0x21][target:1][CRC]                → 3 bytes
Resp: [STATUS][CRC]                        → 2 bytes
```

Code `target`:

| Code | Target |
|---|---|
| 0x00 | NONE / autodetect |
| 0x01 | STM32F1 |
| 0x02 | STM32F4 |
| 0x10 | ESP32 |
| 0x11 | ESP32-C3 |
| 0x12 | ESP32-S3 |

Lưu NVS. Phost dùng nó để chọn engine giao thức đúng khi `FLASH_FW` chạy.

### 4.8 SYNC_LIBRARY (0x30)

```
Cmd:  [0x30][CRC]                          → 2 bytes
Resp: [STATUS][CRC]                        → 2 bytes
```

Yêu cầu phost bắt đầu sync theo manifest qua WiFi của chính nó. Trong
lúc sync, `READ_STATUS.state` sẽ trả `SYNCING` và `bytes_done`/`bytes_total`
phản ánh tiến độ download.

### 4.9 GET_LIBRARY (0x31) / GET_LIBRARY_NEXT (0x32)

```
Cmd 0x31: [0x31][CRC]                      → 2 bytes
Cmd 0x32: [0x32][offset:1][CRC]            → 3 bytes
Resp:     [STATUS][count:1][total:1]
          [entries: 6 × (fw_id:1 + name:4)][CRC]
                                           → 34 bytes
```

- `total` = tổng số FW trên phost này
- `count` = số entry trong response này (≤ 6)
- Mỗi entry: 1 byte `fw_id` + tên ngắn 4 ký tự (ví dụ `"F001"`)
- Với thư viện > 6 entry, master gọi `GET_LIBRARY_NEXT` với
  `offset = 6, 12, …` cho tới khi `count = 0`

Tên 4 ký tự chỉ là gợi ý hiển thị; tên đầy đủ nằm trong manifest trên FW
server. Master có thể bỏ qua tên ngắn và lookup metadata đầy đủ qua tầng
WebSocket của PC.

---

## 5. Struct PhostStatus

Trả về bởi `READ_STATUS`. Đúng 32 byte little-endian.

```c
struct PhostStatus {            // offset
    uint8_t  state;             // 0  — xem enum state phía dưới
    uint8_t  progress;          // 1  — 0..100
    uint8_t  current_fw_id;     // 2  — 0xFF nếu không có fw đang chạy
    uint8_t  target_chip;       // 3  — cùng code với SET_TARGET_TYPE
    uint32_t bytes_done;        // 4
    uint32_t bytes_total;       // 8
    uint16_t event_seq;         // 12 — tăng mỗi khi state đổi
    uint8_t  error_code;        // 14 — xem §6
    int8_t   wifi_rssi;         // 15 — dBm, 0 nếu không có wifi
    uint8_t  lib_count;         // 16 — số FW đang lưu local
    uint8_t  flags;             // 17 — bit flag (xem dưới)
    uint8_t  label[8];          // 18 — 8 ký tự đầu của label (đã cắt)
    uint8_t  reserved[6];       // 26 — phải = 0
};                              // tổng: 32 byte
```

### enum state

| Giá trị | Tên | Ý nghĩa |
|---|---|---|
| 0 | `IDLE` | Không có job nào đang chạy |
| 1 | `FLASHING` | Đang ghi FW xuống chip đích |
| 2 | `VERIFYING` | Đang đọc lại + so khớp |
| 3 | `ERASING` | Đang erase chip đích |
| 4 | `SYNCING` | Đang kéo thư viện FW qua WiFi |
| 5 | `DONE` | Job vừa rồi OK, chờ lệnh kế tiếp |
| 6 | `ERROR` | Job vừa rồi fail, xem `error_code` |
| 7 | `BUSY_BOOT` | Phost còn đang khởi động sau reset |

### bit của flags

| Bit | Tên |
|---|---|
| 0 | `WIFI_CONNECTED` |
| 1 | `TARGET_DETECTED` |
| 2 | `LIBRARY_SYNCED` (sync gần nhất OK) |
| 3 | `STORAGE_LOW` (< 10% trống) |
| 4–7 | reserved |

### Cách dùng event_seq

Phost tăng `event_seq` (mod 65536) **mỗi khi `state` hoặc `error_code`
đổi**. Master cache `last_seq[addr]` và chỉ forward event lên PC khi
thấy delta — tránh spam WebSocket với mấy update kiểu "progress 47%,
vẫn đang flash".

Thay đổi progress đơn thuần **không** tăng `event_seq` — vẫn quan sát
được ở chu kỳ poll tiếp theo nhưng không cần broadcast như event.

---

## 6. Error Codes

Giá trị của `PhostStatus.error_code` sau khi job fail (`state = ERROR`).

| Code | Tên | Ý nghĩa |
|---|---|---|
| 0x00 | `OK` | Không lỗi |
| 0x10 | `TARGET_NOT_DETECTED` | Không thấy chip đích phản hồi qua SWD/UART |
| 0x11 | `TARGET_RDP_LOCKED` | STM32 đang bật read protection |
| 0x12 | `TARGET_WRITE_FAIL` | Ghi flash bị từ chối |
| 0x13 | `TARGET_VERIFY_FAIL` | Readback không khớp |
| 0x20 | `FW_NOT_FOUND` | `fw_id` không có trong thư viện local |
| 0x21 | `FW_CORRUPT` | FW local fail check CRC |
| 0x30 | `WIFI_DISCONNECTED` | Mất WiFi trong lúc sync |
| 0x31 | `SYNC_SERVER_UNREACHABLE` | Manifest URL không phản hồi |
| 0x32 | `SYNC_STORAGE_FULL` | Flash ngoài hết chỗ |
| 0x40 | `ABORT_BY_USER` | Job bị hủy bằng lệnh `ABORT` |
| 0xFF | `UNKNOWN_ERROR` | Bắt-tất-cả |

---

## 7. Concurrency & Polling

### Cadence poll

| Trạng thái bus | Tần suất poll mỗi phost |
|---|---|
| Tất cả idle | 1 Hz |
| Có phost ở trạng thái active (FLASHING / VERIFYING / SYNCING / ERASING) | 5 Hz cho phost đó, 1 Hz cho phost idle còn lại |

Một transaction trên 10 phost ở 400 kHz mất ≈ 0.9 ms. Worst case (10
phost cùng flash, 5 Hz mỗi cái) = 45 ms/s = **4.5 % bus utilization**.
Còn dư rất nhiều cho việc poll lại khi CRC fail.

### Mô hình job song song

```
t=0     master gửi FLASH_FW cho phost 0x10           (~1 ms)
t=1ms   master gửi FLASH_FW cho phost 0x11           (~1 ms)
…
t=10ms  master gửi FLASH_FW cho phost 0x19
        cả 10 phost giờ đang flash chip đích song song
        (mỗi phost chạy engine SWD/UART trên CPU riêng của nó)
t=200ms master poll 0x10  → progress 18 %
t=400ms master poll 0x11  → progress 22 %
…
t=30s   phost đầu tiên báo DONE
t=32s   phost cuối cùng báo DONE
```

Bus I²C chỉ tải **lệnh và status** — không bao giờ tải bulk data
firmware. Phost tự kéo bản FW qua WiFi (xem `SYNC_LIBRARY`). Đó là lý
do song song chạy được dù bus electrical là half-duplex một-master.

### Mode "Flash One" tùy chọn

Master CÓ THỂ expose mode tuần tự: chỉ 1 phost chạy 1 lúc (phost kế
tiếp đợi cái trước `DONE`). Hữu ích cho:

- Debug (cô lập nhiễu từ 1 thiết bị)
- Setup hạn chế nguồn (tránh kéo 10× dòng SWD đồng thời)

Đây là lựa chọn scheduling phía master; giao thức trên từng phost không
đổi.

### Stagger dòng điện

Nếu PSU bench không gánh nổi 10 dòng flash đồng thời, master CÓ THỂ
stagger việc gửi `FLASH_FW` cách nhau ~50 ms thay vì back-to-back. Job
vẫn overlap (song song), chỉ là dời thời điểm start.

### Stagger sync WiFi

Trigger `SYNC_LIBRARY` cho cả 10 phost cùng lúc sẽ khiến 10 ESP32-C3
mở download HTTP(S) đồng thời chống vào cùng 1 AP WiFi. Đa số router
2.4 GHz dân dụng cap số session đồng thời rất thấp dưới 10 — kết quả:
mất gói, download dở dang, và lỗi `SYNC_SERVER_UNREACHABLE` linh tinh.

Master BẮT BUỘC phải stagger lệnh sync. Pattern khuyến nghị:

- Bắt đầu sync **2 phost 1 lúc**
- Đợi cả 2 báo `state ≠ SYNCING` (hoặc 30 s timeout) rồi mới start cặp
  tiếp theo
- Tổng thời gian thực tế cho 10 phost / thư viện 5 MB ≈ 60–90 s

Với AP yếu hơn hoặc thư viện lớn hơn, hạ xuống stagger 1-cái-1-lúc.
Master CÓ THỂ expose tùy chọn này qua UI setting `sync_concurrency`
(mặc định 2).

---

## 8. Flow Discovery

Lúc master boot:

```
for addr in 0x10..0x19:
    gửi PING(addr) với timeout 5 ms
    nếu ACK:
        gửi GET_INFO(addr)
        đánh dấu slave online, cache info
    nếu không:
        đánh dấu slave offline
```

Tổng cold scan: 10 × ~5 ms = **50 ms** worst case.

Sau scan ban đầu, master vẫn PING các địa chỉ offline mỗi 5 giây để phát
hiện phost mới lên online (ví dụ vừa provision, vừa cấp nguồn lại).

---

## 9. Reliability

### Retry

Nếu transaction fail (NACK, CRC sai, timeout), master retry:

- Tối đa 3 lần / command
- Backoff 10 ms giữa các lần
- Lần thứ 3 fail: đánh dấu phost offline, push event `slave_offline`
  lên PC, quay lại chế độ PING re-discovery nền mỗi 5 s

### Timeout

| Operation | Timeout |
|---|---|
| `PING` | 5 ms |
| Mọi command khác | 50 ms (cho phép phost stretch clock khi ghi NVS) |
| Full transaction kể cả retry | 200 ms |

### Tương tác với watchdog

Firmware phost BẮT BUỘC feed watchdog trong các tác vụ dài (flash, sync).
ISR I²C dùng clock stretching để dời response trong lúc main loop bận
flash chip đích — giữ stretch ≤ 10 ms / transaction để master còn margin
với timeout 50 ms.

### Thanh ghi timeout I²C của ESP-IDF

Driver I²C master của ESP-IDF có thanh ghi timeout hardware
(`I2C_TIME_OUT_REG`) với mặc định nhỏ hơn 10 ms trên vài biến thể chip
— quá ngắn, sẽ báo lỗi giả trong lúc phost đang stretch clock một cách
hợp pháp khi ghi NVS. Firmware master BẮT BUỘC nâng nó lên ít nhất 20 ms
(2× margin so với budget stretch 10 ms):

```c
// ESP-IDF v5.x, driver i2c_master mới
i2c_master_bus_config_t bus_cfg = {
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .i2c_port = I2C_NUM_0,
    .scl_io_num = MASTER_SCL_GPIO,
    .sda_io_num = MASTER_SDA_GPIO,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = false,  // ta dùng pull-up ngoài 4.7 kΩ
};
i2c_master_bus_handle_t bus;
ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

// Nâng timeout per-device khi add từng phost
i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address  = 0x10,
    .scl_speed_hz    = 400000,
    .scl_wait_us     = 20000,   // 20 ms — cao hơn budget stretch 10 ms
};
```

Triệu chứng khi timeout quá nhỏ: thỉnh thoảng có `ESP_ERR_TIMEOUT` từ
driver master, chỉ xảy ra ở các command có ghi NVS (`SET_LABEL`,
`SET_TARGET_TYPE`, `SET_ADDRESS` lúc provisioning).

---

## 10. Provisioning Phost Mới

Khi 1 phost mới (factory state, chưa có địa chỉ) được nối vào bus, tab
**Phost Setup** trong FlashPorter sẽ hướng dẫn người dùng:

1. PC gửi lệnh `provision` xuống master qua WebSocket
2. Master tìm slot trống đầu tiên trong `0x10..0x19`
3. Master dùng **kênh provisioning riêng** (out-of-band, không dùng các
   địa chỉ I²C bus thường) — thường là địa chỉ mặc định `0x08` mà phost
   factory đang lắng nghe
4. Master gửi `SET_ADDRESS(target_addr)` qua kênh provisioning
5. Phost ghi địa chỉ mới xuống NVS, reboot
6. Phost rejoin bus tại địa chỉ mới, phản hồi `PING` từ master
7. Master push event `slave_added` lên PC

Chi tiết giao thức provisioning (địa chỉ factory, command set-address)
được tài liệu hóa trong source firmware, không phải trong spec này —
chúng không bao giờ cần thiết trong vận hành bình thường.

> **⚠ Quy tắc một-phost lúc provisioning:** địa chỉ factory `0x08` được
> mọi phost chưa provision chia sẻ. Nếu 2 phost factory cùng được cấp
> nguồn trên cùng bus, cả 2 sẽ ACK `0x08` và việc gán sẽ va chạm
> (arbitration I²C có thể chọn nhầm device, hoặc cả 2 ghi cùng địa chỉ
> NVS). UI Phost Setup BẮT BUỘC phải hướng dẫn operator provision đúng
> 1 phost vật lý 1 lúc. Các cách chấp nhận được, ưu tiên giảm dần:
>
> 1. **Switch cấp nguồn riêng từng socket trên jig** — chỉ 1 socket được
>    cấp điện tại bước Provision (tốt nhất, được hardware bảo đảm).
> 2. **Quy tắc workflow của operator** — UI hiển thị "cắm ĐÚNG 1 phost
>    mới, rồi nhấn Provision" kèm dialog xác nhận; phụ thuộc kỷ luật
>    operator.
> 3. **Heuristic phát hiện** — master gửi 1 sentinel-write tới `0x08`
>    và đọc lại ngay; nếu đọc trả về dữ liệu không đồng nhất qua 3 lần
>    thử, abort với lỗi `PROVISION_COLLISION` và hướng dẫn operator.
>
> Quy tắc này không áp dụng cho vận hành bình thường — các phost đã
> provision dùng địa chỉ unique trong `0x10–0x19`.

---

## 11. Versioning

Giao thức này là phiên bản **1**. `GET_INFO.fw_major` cho biết phiên
bản giao thức mà firmware phost đang nói. Master BẮT BUỘC từ chối dùng
phost có `fw_major` cao hơn chính mình (forward incompatibility chỉ
opt-in).

Thêm mã command mới trong cùng major version là cho phép — phost trả
`STATUS = 0x02 INVALID_CMD` cho code lạ, master coi đây là non-fatal.

---

## 12. Ví dụ Session

### Flash cùng FW cho tất cả phost

```
PC:                                                    Master:                                              Phost 0x10:
flash {addrs: ["0x10","0x11"], fw_id: 2}
                                                       FLASH_FW(0x10, 2)                                   ACK
                                                       FLASH_FW(0x11, 2)                                   …
                                                       (loop poll mỗi 200 ms)
                                                       READ_STATUS(0x10) → state=FLASHING progress=15
                                                                                                            …
slave_progress {addr:"0x10", progress:15}
                                                       …                                                    state=DONE event_seq=12
slave_result {addr:"0x10", ok:true}
```

### Phost offline giữa lúc flash

```
master poll 0x11 → không ACK
master retry × 3 → tất cả fail
master đánh dấu 0x11 offline → push event slave_offline
master tiếp tục poll các phost khác
master re-ping 0x11 mỗi 5 s
… 30 s sau 0x11 quay lại → push event slave_online
```

### Sync library

```
PC: sync {addrs: ["all"], manifest_url: "https://..."}
master loop: SYNC_LIBRARY(0x10), SYNC_LIBRARY(0x11), …
master poll READ_STATUS từng phost @ 5 Hz, theo dõi bytes_done/bytes_total
forward thành event sync_progress lên PC
```

---

## 13. Mục để mở

- **Mã hóa `index.txt`:** thư viện FW phía PC trên Git đã mã hóa AES-128-CBC
  (xem [git_sync.py](../toolAddFirmware/FlashPorter_Public/modules/git_sync.py)).
  Firmware phost sẽ cần cùng key/IV để đọc manifest. Chiến lược phân
  phối key: lưu xuống NVS lúc provisioning (master push key qua I²C bằng
  command `SET_AES_KEY` — chưa specify, ứng viên cho protocol v2).
- **Giao thức provisioning** (địa chỉ factory, command set-address) —
  sẽ specify trong doc follow-up khi khung firmware xong.
- **Metric sức khỏe bus** — bit error rate, số lần retry per phost — có
  thể expose qua command mới `GET_BUS_STATS` cho mục đích chẩn đoán.
