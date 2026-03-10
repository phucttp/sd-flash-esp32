# Plan: Refactor CHIVI Project to Reference Standard

**Date**: 2026-03-07 (updated)
**Reference**: `C:\Users\DELL\Downloads\Flashing_Device_Firmware-main`
**Target**: `Muti-CHIVI-TFT`

---

## 1. So sanh kien truc

| Aspect | Reference (Flashing_Device) | CHIVI hien tai |
|--------|----------------------------|----------------|
| Build system | PlatformIO | ESP-IDF CMake |
| Framework | Arduino | Arduino as IDF component |
| UI | LVGL 8.3.11 + SquareLine Studio | Custom TftUI (Adafruit_GFX) |
| Display lib | TFT_eSPI | Adafruit_ST7735 |
| Flasher arch | OOP: Factory + Adafruit_DAP inheritance | Procedural C-style functions |
| Module layout | `src/modules/prog/{esp32,stm32}/` | `main/modules/prog/{esp32,stm32}/` |
| SWD library | `lib/arm_dap/` (Adafruit_DAP) | `components/Adafruit_DAP/` |
| Logging | VHI_LOGGING_PRINT (custom) | ESP_LOG (IDF native) |
| Board def | `boards/*.json` (PlatformIO) | `sdkconfig` (IDF) |
| MCU | ESP32-S3 | ESP32-S3 |
| Display | ST7735 160x128 | ST7735 160x128 |
| Storage | N/A | USB drive (TinyUSB MSC+CDC) + SD card |

---

## 2. Trang thai hien tai

### DA HOAN THANH

- **Module layout** (`main/modules/`): DONE — cau truc domain-based da duoc ap dung
  ```
  main/modules/
  ├── prog/           (flash engines)
  │   ├── prog_common.*
  │   ├── esp32/      (prog_esp32, prog_esp32_crypto)
  │   └── stm32/      (prog_stm32_f1, prog_stm32_f4)
  ├── storage/        (usb_drive, sd_card)
  ├── ui/             (app_actions, ui_state)
  └── utils/          (file_utils, flash_log)
  ```
- **Pin centralization** (`pin_config.h`): DONE
- **OLED components excluded**: DONE (Adafruit_SH110X, SSD1306, OledUI excluded in CMakeLists)

### CHUA LAM

- **Flasher OOP conversion**: code van la procedural C-style free functions
- **Component cleanup**: SAM files + examples van con trong Adafruit_DAP
- **prog_esp32_crypto.cpp** CHUA co trong `main/CMakeLists.txt` SRCS (co the la bug)

---

## 3. Khuyen nghi refactor

### 3.1. GIU NGUYEN (khong doi)

- **Build system**: Giu ESP-IDF CMake — can cho USB Host (`usb_host_vcp`) trong roadmap
- **Logging**: Giu ESP_LOG — native IDF, tot hon VHI_LOGGING_PRINT
- **Pin config**: Giu `pin_config.h` — reference phan tan pin trong main.h, CHIVI da tot hon
- **Components dir**: Giu `components/` (IDF convention) thay vi `lib/` (PlatformIO)
- **Module layout**: DA XONG, giu nguyen

### 3.2. REFACTOR: Flasher Architecture → OOP (QUAN TRONG NHAT)

**Hien tai** (procedural):
```
main/modules/prog/
├── prog_common.h/cpp          ← C functions: swd_probe_idcode(), host_system_restart()
├── esp32/
│   ├── prog_esp32.h/cpp       ← C functions: flasher_begin_session(), flasher_chip_erase()
│   └── prog_esp32_crypto.h/cpp ← C function: decrypt_firmware()
└── stm32/
    ├── prog_stm32_f1.h/cpp    ← C functions: flasher_swd_stm32f1_flash_firmware()
    └── prog_stm32_f4.h/cpp    ← C functions: flasher_swd_stm32f4_flash_firmware()
```
- app_actions.cpp routing bang if/else IDCODE check, goi truc tiep tung ham

**Reference architecture** (OOP):
```
ProgSTM32Core : public Adafruit_DAP     ← base class ke thua DAP
├── ProgSTM32F1 : public ProgSTM32Core  ← override select/erase/programBlock/programFlash
└── ProgSTM32F4 : public ProgSTM32Core  ← override select/erase/programBlock/programFlash

ProgSTM32 (Factory)                     ← tao F1 hoac F4 theo STM32Series enum
```
- Reference KHONG co `ProgBase` abstract interface chung cho ESP32+STM32
- Reference chi co OOP cho STM32; ESP32 van la file rieng (prog_esp32.cpp)
- Factory chi dispatch F1/F4, KHONG dispatch ESP32 vs STM32

**De xuat** (thuc te, sat reference):
```
main/modules/prog/
├── prog_common.h/cpp              ← giu nguyen: IDCODE probe, pin init
├── esp32/
│   ├── prog_esp32.h/cpp           ← GIU procedural (giong reference)
│   └── prog_esp32_crypto.h/cpp
└── stm32/
    ├── prog_stm32.h/cpp           ← MOI: Factory class, dispatch F1/F4
    ├── prog_stm32_f1.h/cpp        ← CONVERT: class ProgSTM32F1 : public ProgSTM32Core
    ├── prog_stm32_f4.h/cpp        ← CONVERT: class ProgSTM32F4 : public ProgSTM32Core
    └── core/
        ├── prog_stm32_core.h/cpp  ← MOI: base class ke thua Adafruit_DAP
        └── prog_stm32_utils.h/cpp ← MOI: shared utilities (optional)
```

**Interface STM32 (theo reference)**:
```cpp
// prog_stm32_core.h
class ProgSTM32Core : public Adafruit_DAP {
public:
    ProgSTM32Core();
    virtual ~ProgSTM32Core() {}
    // Adafruit_DAP virtual methods override boi F1/F4:
    // select(), deselect(), erase(), program_start(),
    // programBlock(), programFlash(), protectBoot(), unprotectBoot()

    // CHIVI-specific (khong co trong reference):
    virtual bool detect_rdp(int* rdp_level) = 0;
    virtual bool rdp_disable_trigger() = 0;
    virtual bool flash_firmware(const char* path, progress_cb_t cb) = 0;
};

// prog_stm32.h — Factory
typedef enum { STM32F1, STM32F4 } STM32Series;

class ProgSTM32 {
private:
    ProgSTM32Core* _core;
public:
    ProgSTM32(int swclk, int swdio, int nreset, STM32Series series);
    virtual ~ProgSTM32();
    ProgSTM32Core* core() { return _core; }
};
```

**Loi ich**:
- Them MCU moi (G0, G4, F0) chi can tao class moi + them enum
- Factory pattern dispatch theo IDCODE (giong reference)
- app_actions.cpp goi `prog->core()->flash_firmware()` — khong can biet F1 hay F4
- Future `ProgESP32USB` cho USB Host mode de dang them

### 3.3. REFACTOR: UI → LVGL (TUY CHON, workload LON)

**Hien tai**: TftUI custom dung Adafruit_GFX — chay on, 3 tabs (FW, Tools, Info), DESC/HIST disabled
**Reference**: LVGL 8.3.11 + SquareLine Studio — visual editor, export C code tu dong

**Uu diem LVGL**:
- SquareLine Studio: keo tha UI, export code tu dong
- Widgets san: progress bar, spinner, list, dropdown, chart
- Theming: dark/light mode de
- Animation framework built-in
- Community support rong

**Nhuoc diem**:
- RAM overhead ~40-60KB (ESP32-S3 co PSRAM → khong van de)
- Flash overhead ~200KB
- Learning curve: LVGL flush callback, display driver, input driver
- Migration effort: ~2-3 ngay
- Reference LVGL code con rat don gian (1 screen, 1 image) — chua co menu/tab logic

**Ket luan**: NEN migrate neu UI can mo rong (them charts, animations, etc.). KHONG CAN neu TftUI hien tai du dung. Reference cung chua implement UI day du.

### 3.4. REFACTOR: Adafruit_DAP Component

**Reference** chi giu files can thiet trong `lib/arm_dap/`:
- `Adafruit_DAP.h/cpp` — core
- `dap.h/cpp` — SWD protocol
- `dap_config.h` — GPIO config

**CHIVI** co them unused files:
- `Adafruit_DAP_SAM.cpp` — SAM chip (khong dung)
- `examples/` directory — khong can trong production

**De xuat**: Xoa SAM + examples, giu STM32 + STM32F1 + core only.

---

## 4. Roadmap thuc hien

### Phase 1: Module Layout ✅ DA XONG

- [x] Tao `main/modules/` directory structure
- [x] Di chuyen files vao dung vi tri
- [x] Update `main/CMakeLists.txt` paths
- [x] Verify build passes

### Phase 2: OOP Flasher Architecture (4-6 gio)

**Buoc 2.1 — Tao base class + factory (1 gio)**
- [ ] Tao `main/modules/prog/stm32/core/prog_stm32_core.h/cpp`
  - Class `ProgSTM32Core : public Adafruit_DAP`
  - Khai bao virtual methods: `detect_rdp()`, `rdp_disable_trigger()`, `flash_firmware()`
- [ ] Tao `main/modules/prog/stm32/prog_stm32.h/cpp`
  - Factory class `ProgSTM32` voi `STM32Series` enum
  - Constructor: tao `ProgSTM32F1` hoac `ProgSTM32F4` theo series
- [ ] Update `main/CMakeLists.txt` — them prog_stm32_core.cpp, prog_stm32.cpp

**Buoc 2.2 — Migrate F4 engine (1.5-2 gio)**
- [ ] Convert `prog_stm32_f4.cpp` tu free functions → `ProgSTM32F4 : public ProgSTM32Core`
  - Di chuyen static DAP instance vao class member
  - Override: `select()`, `deselect()`, `erase()`, `program_start()`, `programBlock()`, `programFlash()`
  - Implement: `detect_rdp()`, `rdp_disable_trigger()`, `flash_firmware()`
- [ ] Update header `prog_stm32_f4.h`
- [ ] Build test — chua can hardware test

**Buoc 2.3 — Migrate F1 engine (1.5-2 gio)**
- [ ] Convert `prog_stm32_f1.cpp` tu free functions → `ProgSTM32F1 : public ProgSTM32Core`
  - Tuong tu F4 nhung voi page erase, half-word programming
  - Override cac methods tuong ung
- [ ] Update header `prog_stm32_f1.h`
- [ ] Build test

**Buoc 2.4 — Update app_actions dispatcher (1 gio)**
- [ ] Thay IDCODE if/else block bang `ProgSTM32` factory
  - Tao `ProgSTM32(swclk, swdio, nreset, series)` dua tren IDCODE
  - Goi `prog->core()->flash_firmware()` thay vi `flasher_swd_stm32f4_flash_firmware()`
  - Goi `prog->core()->detect_rdp()` thay vi `flasher_swd_stm32f4_detect_rdp()`
- [ ] ESP32 flasher: GIU nguyen procedural (giong reference)
- [ ] Build + test tren hardware (F1 + F4 targets)

### Phase 3: Cleanup Components (30 phut)

- [ ] Remove `Adafruit_DAP_SAM.cpp` + `examples/` tu components/Adafruit_DAP
- [ ] Update component CMakeLists
- [ ] Fix: them `prog_esp32_crypto.cpp` vao `main/CMakeLists.txt` SRCS (neu can)
- [ ] Verify build

### Phase 4: USB Host VCP Flasher (FUTURE — khi co hardware)

- [ ] Tao `main/modules/prog/esp32_usb/prog_esp32_usb.h/cpp`
  - Co the implement ProgBase interface hoac giu procedural
- [ ] Custom port layer cho esp-serial-flasher qua VCP
- [ ] USB mode switch logic (Device ↔ Host)
- [ ] Test tren board target thuc te

### Phase 5: LVGL Migration (OPTIONAL — 2-3 ngay)

- [ ] Add LVGL 8.3 component
- [ ] Setup display driver (flush callback cho ST7735)
- [ ] Setup input driver (3 buttons → LVGL encoder)
- [ ] Design screens in SquareLine Studio
- [ ] Replace TftUI calls in main.cpp
- [ ] Remove TftUI + Adafruit_GFX components

---

## 5. So sanh Reference vs CHIVI — tinh nang

| Tinh nang | Reference | CHIVI | Ghi chu |
|-----------|-----------|-------|---------|
| STM32 F1 flash | Co (class) | Co (procedural) | Logic tuong duong |
| STM32 F4 flash | Co (class) | Co (procedural) | Logic tuong duong |
| ESP32 UART flash | Stub (chua implement) | Day du | CHIVI vuot troi |
| RDP detect/disable | Khong thay trong code | Day du (blind write + retry) | CHIVI vuot troi |
| On-the-fly verify | Khong ro | Day du (256B chunk verify) | CHIVI vuot troi |
| Device whitelist (F1) | Co | Co | Tuong duong |
| Factory pattern | Co (ProgSTM32) | Chua | Can refactor |
| OOP class hierarchy | Co | Chua | Can refactor |
| LVGL UI | Co (1 screen, basic) | Khong (TftUI custom) | Reference cung chua day du |
| USB drive storage | Khong | Co (TinyUSB MSC+CDC) | CHIVI vuot troi |
| SD card | Khong | Co | CHIVI vuot troi |
| Flash history | Khong | Co (disabled) | CHIVI vuot troi |
| WiFi sync/NetFlash | Khong | Khong (chua port) | Chua co o ca hai |
| UI tabs | 1 screen | 3 tabs (FW, Tools, Info) | CHIVI vuot troi |
| Boot combo brute-force | Khong | Co | CHIVI vuot troi |
| AES-128-CBC crypto | Khong | Co | CHIVI vuot troi |

**Ket luan**: CHIVI da co NHIEU tinh nang hon reference. Refactor chi can lay **pattern OOP** (factory + class hierarchy), KHONG can lay logic flash — logic cua CHIVI da tot hon.

---

## 6. Unresolved Questions

1. **LVGL migration**: Co muon migrate UI sang LVGL khong, hay giu TftUI? Reference LVGL cung chua implement day du.
2. **Timeline**: Phase 2 (OOP) truoc hay Phase 4 (USB Host VCP) truoc?
3. **ESP32 flasher OOP**: Reference giu ESP32 procedural. Co muon wrap ESP32 vao class khong, hay giu procedural cho don gian?
4. **prog_esp32_crypto.cpp**: File ton tai nhung KHONG co trong CMakeLists SRCS — la bug hay crypto chua can?
