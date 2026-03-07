# Plan: Refactor CHIVI Project to Reference Standard

**Date**: 2026-03-07
**Reference**: `C:\Users\DELL\Downloads\Flashing_Device_Firmware-main`
**Target**: `Muti-CHIVI-TFT`

---

## 1. So sánh kiến trúc

| Aspect | Reference (Flashing_Device) | CHIVI hiện tại |
|--------|----------------------------|----------------|
| Build system | PlatformIO | ESP-IDF CMake |
| Framework | Arduino | Arduino as IDF component |
| UI | LVGL 8.3.11 + SquareLine Studio | Custom TftUI (Adafruit_GFX) |
| Display lib | TFT_eSPI | Adafruit_ST7735 |
| Flasher arch | OOP: Factory + Inheritance | Procedural C-style functions |
| Module layout | `src/modules/prog/{esp32,stm32}/` | `main/flasher/` flat files |
| SWD library | `lib/arm_dap/` (same Adafruit_DAP) | `components/Adafruit_DAP/` |
| Logging | VHI_LOGGING_PRINT (custom) | ESP_LOG (IDF native) |
| Board def | `boards/*.json` (PlatformIO) | `sdkconfig` (IDF) |

---

## 2. Khuyến nghị refactor

### 2.1. GIỮ NGUYÊN (không đổi)
- **Build system**: Giữ ESP-IDF CMake — cần cho USB Host (`usb_host_vcp`) trong roadmap
- **Logging**: Giữ ESP_LOG — native IDF, tốt hơn VHI_LOGGING_PRINT
- **Pin config**: Giữ `pin_config.h` — reference phân tán pin, CHIVI đã tốt hơn
- **Components dir**: Giữ `components/` (IDF convention) thay vì `lib/` (PlatformIO)

### 2.2. REFACTOR: Flasher Architecture → OOP (QUAN TRỌNG NHẤT)

**Hiện tại** (procedural):
```
main/flasher/
├── flasher_common.h/cpp        ← shared functions
├── flasher_esp.h/cpp           ← free functions: flasher_esp_begin(), flash_firmware()
├── flasher_swd_stm32f4.h/cpp   ← free functions: flasher_swd_f4_flash_firmware()
└── flasher_swd_stm32f1.h/cpp   ← free functions: flasher_swd_f1_flash_firmware()
```

**Đề xuất** (OOP theo reference):
```
main/modules/prog/
├── prog_base.h                  ← Abstract base class (interface)
├── esp32/
│   ├── prog_esp32.h
│   └── prog_esp32.cpp           ← Class ProgESP32 : public ProgBase
├── stm32/
│   ├── prog_stm32.h             ← Factory class (dispatch F1/F4)
│   ├── prog_stm32.cpp
│   ├── prog_stm32_f1.h
│   ├── prog_stm32_f1.cpp        ← Class ProgSTM32F1 : public ProgSTM32Core
│   ├── prog_stm32_f4.h
│   ├── prog_stm32_f4.cpp        ← Class ProgSTM32F4 : public ProgSTM32Core
│   └── core/
│       ├── prog_stm32_core.h    ← Base class extends Adafruit_DAP
│       └── prog_stm32_core.cpp
└── esp32_usb/                   ← FUTURE: USB Host VCP flasher
    ├── prog_esp32_usb.h
    └── prog_esp32_usb.cpp       ← Class ProgESP32USB : public ProgBase
```

**Interface chung**:
```cpp
class ProgBase {
public:
    virtual bool connect() = 0;
    virtual bool flash(const char* path, progress_cb_t cb) = 0;
    virtual bool erase() = 0;
    virtual bool verify() = 0;
    virtual void disconnect() = 0;
    virtual ~ProgBase() {}
};
```

**Lợi ích**:
- Thêm `ProgESP32USB` cho USB Host mode dễ dàng — chỉ implement interface
- Factory pattern dispatch theo IDCODE (giống reference)
- app_actions.cpp chỉ cần gọi `prog->flash()` — không cần biết target type

### 2.3. REFACTOR: UI → LVGL (TÙY CHỌN, workload LỚN)

**Hiện tại**: TftUI custom dùng Adafruit_GFX — chạy ổn nhưng khó mở rộng
**Reference**: LVGL 8.3.11 + SquareLine Studio — visual editor, rich widgets

**Ưu điểm LVGL**:
- SquareLine Studio: kéo thả UI, export code tự động
- Widgets sẵn: progress bar, spinner, list, dropdown, chart
- Theming: dark/light mode dễ
- Animation framework built-in
- Community support rộng

**Nhược điểm**:
- RAM overhead ~40-60KB (ESP32-S3 có 8MB PSRAM → không vấn đề)
- Flash overhead ~200KB
- Learning curve: LVGL flush callback, display driver, input driver
- Migration effort: ~2-3 ngày

**Kết luận**: NÊN migrate nếu UI cần mở rộng (thêm charts, animations, etc.). KHÔNG CẦN nếu UI hiện tại đủ dùng.

### 2.4. REFACTOR: Module Layout

**Hiện tại** (flat):
```
main/
├── main.cpp
├── app_actions/
├── flasher/
├── sd_card/
├── usb_drive/
├── metadata_parser/
├── file_utils/
├── ui_state/
└── flash_log/
```

**Đề xuất** (grouped by domain, theo reference):
```
main/
├── main.cpp
├── pin_config.h
├── firmware_types.h
├── modules/
│   ├── prog/                    ← Flashing engines (OOP)
│   │   ├── prog_base.h
│   │   ├── esp32/
│   │   ├── stm32/
│   │   └── esp32_usb/          ← FUTURE
│   ├── storage/                 ← SD card + USB drive + metadata
│   │   ├── sd_card.h/cpp
│   │   ├── usb_drive.h/cpp
│   │   └── metadata_parser.h/cpp
│   ├── ui/                      ← UI logic (hoặc LVGL screens)
│   │   ├── ui_state.h/cpp
│   │   └── app_actions.h/cpp   ← UI action dispatcher
│   └── utils/                   ← Shared utilities
│       ├── file_utils.h/cpp
│       └── flash_log.h/cpp
```

### 2.5. REFACTOR: Adafruit_DAP Component

**Reference** chỉ giữ files cần thiết trong `lib/arm_dap/`:
- `Adafruit_DAP.h/cpp` — core
- `dap.h/cpp` — SWD protocol
- `dap_config.h` — GPIO config

**CHIVI** có thêm unused files:
- `Adafruit_DAP_SAM.cpp` — SAM chip (không dùng)
- `examples/` directory — không cần trong production

**Đề xuất**: Xóa SAM + examples, giữ STM32 + STM32F1 + core only.

---

## 3. Roadmap thực hiện

### Phase 1: Module Layout (1-2 giờ)
- [ ] Tạo `main/modules/` directory structure
- [ ] Di chuyển files vào đúng vị trí
- [ ] Update `main/CMakeLists.txt` paths
- [ ] Verify build passes

### Phase 2: OOP Flasher Architecture (4-6 giờ)
- [ ] Tạo `ProgBase` abstract interface
- [ ] Tạo `ProgSTM32Core` base class (wrap Adafruit_DAP)
- [ ] Migrate `flasher_swd_stm32f1.cpp` → `ProgSTM32F1` class
- [ ] Migrate `flasher_swd_stm32f4.cpp` → `ProgSTM32F4` class
- [ ] Tạo `ProgSTM32` factory class
- [ ] Migrate `flasher_esp.cpp` → `ProgESP32` class
- [ ] Update `app_actions.cpp` to use ProgBase interface
- [ ] Verify build + test trên hardware

### Phase 3: Cleanup Components (30 phút)
- [ ] Remove `Adafruit_DAP_SAM.cpp` + examples
- [ ] Update component CMakeLists
- [ ] Verify build

### Phase 4: USB Host VCP Flasher (FUTURE — khi có hardware)
- [ ] Tạo `ProgESP32USB` class implementing ProgBase
- [ ] Custom port layer cho esp-serial-flasher qua VCP
- [ ] USB mode switch logic (Device ↔ Host)
- [ ] Test trên board target thực tế

### Phase 5: LVGL Migration (OPTIONAL — 2-3 ngày)
- [ ] Add LVGL 8.3 component
- [ ] Setup display driver (flush callback cho ST7735)
- [ ] Setup input driver (3 buttons → LVGL encoder)
- [ ] Design screens in SquareLine Studio
- [ ] Replace TftUI calls in main.cpp
- [ ] Remove TftUI + Adafruit_GFX components

---

## 4. Unresolved Questions

1. **LVGL migration**: Có muốn migrate UI sang LVGL không, hay giữ TftUI?
2. **PlatformIO**: Có muốn switch build system sang PlatformIO không? (tui recommend KHÔNG — IDF cần cho USB Host)
3. **Timeline**: Phase 1-3 trước hay Phase 4 (USB Host) trước?
