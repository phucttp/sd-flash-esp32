# Hướng Dẫn Sử Dụng Thư Viện OledUI

Thư viện UI cho màn hình OLED (SH1106G/SSD1306) - Phiên bản 1.0.0

**Tác giả:** TTP27

---

## Mục Lục

1. [Cài Đặt](#1-cài-đặt)
2. [Khởi Tạo](#2-khởi-tạo)
3. [Menu](#3-menu)
4. [Tabs](#4-tabs)
5. [Message & Progress](#5-message--progress)
6. [Dialog](#6-dialog)
7. [Input](#7-input)
8. [Draw Helpers](#8-draw-helpers)
9. [Tiện Ích](#9-tiện-ích)
10. [Hằng Số](#10-hằng-số)
11. [Ví Dụ Đầy Đủ](#11-ví-dụ-đầy-đủ)

---

## 1. Cài Đặt

### Thư viện cần thiết

**PlatformIO** (thêm vào `platformio.ini`):
```ini
lib_deps =
    adafruit/Adafruit GFX Library
    adafruit/Adafruit SH110X
    adafruit/Adafruit BusIO
```

**Arduino IDE** (Library Manager):
1. Adafruit GFX Library
2. Adafruit SH110X (cho SH1106G)
3. Adafruit BusIO

### Phần cứng mặc định
- Màn hình: SH1106G 128x64 I2C (địa chỉ 0x3C)
- 3 nút bấm: UP, DOWN, OK (INPUT_PULLUP, active LOW)

---

## 2. Khởi Tạo

```cpp
#include "oled_ui.h"

// Khai báo đối tượng
OledUI ui;
Adafruit_SH1106G display(128, 64, &Wire, -1);

// Định nghĩa chân nút bấm
#define PIN_UP    32
#define PIN_DOWN  33
#define PIN_OK    25

void setup() {
    // Khởi tạo màn hình
    display.begin(0x3C, true);
    display.setRotation(2);  // Xoay nếu cần

    // Khởi tạo UI
    ui.begin(&display, PIN_UP, PIN_DOWN, PIN_OK);
}
```

### Hàm `begin()`
```cpp
void begin(Adafruit_SH110X* disp, uint8_t pinUp, uint8_t pinDown, uint8_t pinOk);
```

| Tham số | Mô tả |
|---------|-------|
| `disp` | Con trỏ màn hình Adafruit_SH110X |
| `pinUp` | Chân nút UP |
| `pinDown` | Chân nút DOWN |
| `pinOk` | Chân nút OK |

---

## 3. Menu

Menu là giao diện danh sách cho phép người dùng chọn một mục.

### 3.1. Tạo Menu

```cpp
const char* items[] = {"Tuy chon 1", "Tuy chon 2", "Tuy chon 3", "Cai dat"};
ui.menuCreate("MENU CHINH", items, 4);
```

**Tham số:**
| Tham số | Mô tả |
|---------|-------|
| `title` | Tiêu đề menu (NULL = không có header) |
| `items` | Mảng các chuỗi |
| `count` | Số lượng mục (tối đa 32) |

### 3.2. Cập Nhật Menu

```cpp
void loop() {
    int selected = ui.menuUpdate();

    if (selected >= 0) {
        // Người dùng đã chọn mục có index = selected
        switch (selected) {
            case 0: doOption1(); break;
            case 1: doOption2(); break;
            case 2: doOption3(); break;
            case 3: openSettings(); break;
        }

        // Vẽ lại menu sau khi xử lý
        ui.menuRedraw();
    }
}
```

**Giá trị trả về:**
| Giá trị | Ý nghĩa |
|---------|---------|
| `>= 0` | Index của mục được chọn |
| `UI_NO_ACTION` (-1) | Không có hành động |

### 3.3. Các Hàm Khác

```cpp
// Lấy index hiện tại
int currentIdx = ui.menuGetIndex();

// Đặt index (tự động cuộn và vẽ lại)
ui.menuSetIndex(2);

// Vẽ lại menu
ui.menuRedraw();
```

### 3.4. Tính Năng Tự Động

- **Cuộn tròn:** Ấn UP ở đầu → nhảy xuống cuối, ấn DOWN ở cuối → nhảy lên đầu
- **Cuộn nhanh:** Giữ nút > 500ms → cuộn nhanh (80ms/lần thay vì 200ms)
- **Hiển thị vị trí:** Nếu có nhiều mục, góc phải dưới hiển thị "1/10"

---

## 4. Tabs

Hệ thống tabs cho phép chia giao diện thành nhiều trang.

### 4.1. Tạo Tabs

**Tabs đơn giản (không có items):**
```cpp
const char* tabs[] = {"Home", "Set", "Info"};
ui.tabsCreate(tabs, 3);
```

**Tabs với số lượng items mỗi tab:**
```cpp
const char* tabs[] = {"Home", "Set", "Info"};
int itemCounts[] = {5, 4, 3};  // Home: 5 items, Set: 4 items, Info: 3 items
ui.tabsCreate(tabs, 3, itemCounts);
```

### 4.2. Cách Điều Khiển Tabs

| Thao tác | Hành động |
|----------|-----------|
| **UP + DOWN** (đồng thời) | Chuyển sang tab tiếp theo |
| **UP** (đơn) | Di chuyển lên trong danh sách items |
| **DOWN** (đơn) | Di chuyển xuống trong danh sách items |
| **OK** | Thoát khỏi tabs loop |

> **Lưu ý:** Khi nhấn 1 nút, thư viện sẽ chờ 100ms để xem có nút thứ 2 không (phát hiện combo).

### 4.3. Cập Nhật Tabs (Manual Loop)

```cpp
void loop() {
    int result = ui.tabsUpdate();

    switch (result) {
        case UI_TAB_SWITCHED:  // (-3) Đã chuyển tab
            redrawUI();
            break;

        case UI_ITEM_CHANGED:  // (-4) Item thay đổi
            redrawUI();
            break;

        case UI_EXIT:          // (-5) Nhấn OK
            // Thoát hoặc xử lý
            break;

        case UI_NO_ACTION:     // (-1) Không có gì
            break;
    }
}
```

### 4.4. Tabs với Callback (Recommended)

Cách tiếp cận được khuyến nghị - sử dụng callback để vẽ nội dung:

```cpp
// Callback vẽ nội dung tab
void drawTabContent(int tab, int itemIdx, int yStart) {
    switch (tab) {
        case 0:  // Tab Home
            ui.drawTextCenter("Welcome!", yStart);
            ui.drawKeyValue("Status", "Online", yStart + 12);
            break;

        case 1:  // Tab Settings
            const char* options[] = {"Brightness", "Volume", "Language"};
            ui.drawList(options, 3, itemIdx, yStart, 5);
            break;

        case 2:  // Tab Info
            ui.drawKeyValue("Version", "1.0.0", yStart);
            ui.drawKeyValue("Author", "TTP27", yStart + 10);
            break;
    }
}

void setup() {
    // ... khởi tạo ...

    const char* tabs[] = {"Home", "Set", "Info"};
    int items[] = {0, 3, 0};  // Set có 3 items, các tab khác không có
    ui.tabsCreate(tabs, 3, items);

    // Đặt callback
    ui.tabsSetDrawCallback(drawTabContent);
}

void loop() {
    // Chạy blocking loop - tự động xử lý buttons và redraw
    int lastTab = ui.tabsRun();

    // Khi thoát (nhấn OK), lastTab là index tab cuối cùng
}
```

### 4.5. Cấu Hình Header

**Chế độ hiển thị header:**
```cpp
ui.tabsSetHeaderMode(UI_HEADER_ALWAYS);  // Luôn hiện (mặc định)
ui.tabsSetHeaderMode(UI_HEADER_NEVER);   // Luôn ẩn
ui.tabsSetHeaderMode(UI_HEADER_AUTO);    // Tự động ẩn/hiện
```

**Kiểu header:**
```cpp
ui.tabsSetHeaderStyle(UI_HEADER_TABS);   // Hiển thị tất cả tabs (mặc định)
ui.tabsSetHeaderStyle(UI_HEADER_SINGLE); // Chỉ hiện tên tab hiện tại + "1/3"
```

**Thời gian tự động ẩn (chỉ cho AUTO mode):**
```cpp
ui.tabsSetHeaderTimeout(3000);  // Ẩn sau 3 giây (mặc định 2000ms)
```

**Kiểm tra và lấy thông tin:**
```cpp
bool visible = ui.tabsIsHeaderVisible();  // Header đang hiện?
int height = ui.tabsGetHeaderHeight();    // 10 nếu hiện, 0 nếu ẩn
```

### 4.6. Quản Lý Tab

```cpp
// Lấy/đặt tab hiện tại
int current = ui.tabsGetCurrent();
ui.tabsSetCurrent(1);

// Chuyển tab thủ công
ui.tabsNext();  // Tab tiếp theo
ui.tabsPrev();  // Tab trước

// Lấy/đặt item index của tab hiện tại
int itemIdx = ui.tabsGetItemIndex();
ui.tabsSetItemIndex(2);

// Lấy số lượng items của tab hiện tại
int count = ui.tabsGetItemCount();

// Reset tất cả trạng thái
ui.tabsReset();
```

### 4.7. Cập Nhật Realtime (Dirty Flag)

Khi dữ liệu thay đổi từ task khác (ví dụ: WiFi, sensor), bạn có thể đánh dấu tab cần vẽ lại:

```cpp
// Task cập nhật dữ liệu
void sensorTask() {
    sensorValue = readSensor();

    // Đánh dấu tab 2 cần vẽ lại
    ui.tabsMarkDirty(2);
}

// Hoặc vẽ lại ngay lập tức (nếu đang ở tab đó)
void updateDisplay() {
    networkProgress = 75;
    ui.tabsForceRedraw(3);  // Vẽ tab 3 ngay (nếu đang ở tab 3)
}
```

**Các hàm dirty flag:**
```cpp
ui.tabsMarkDirty(-2);     // Đánh dấu tab hiện tại
ui.tabsMarkDirty(-1);     // Đánh dấu tất cả tabs
ui.tabsMarkDirty(2);      // Đánh dấu tab cụ thể

bool dirty = ui.tabsIsDirty();   // Tab hiện tại dirty?
bool dirty = ui.tabsIsDirty(2);  // Tab 2 dirty?

ui.tabsClearDirty();      // Xóa flag tab hiện tại
ui.tabsClearDirty(-1);    // Xóa flag tất cả tabs
```

---

## 5. Message & Progress

### 5.1. Hiển Thị Thông Báo

```cpp
ui.showMessage("Dang ket noi...");
ui.showMessage("Loi!", "Khong tim thay file");
```

### 5.2. Hiển Thị Progress Bar

```cpp
for (int i = 0; i <= 100; i++) {
    ui.showProgress("Dang tai xuong...", i);
    delay(50);
}
```

### 5.3. Hiển Thị Spinner (Loading)

```cpp
int frame = 0;
while (loading) {
    ui.showSpinner("Dang xu ly...", frame++);
    delay(100);
}
```

Spinner hiển thị các ký tự: `| / - \` xoay vòng.

---

## 6. Dialog

### 6.1. Dialog Xác Nhận (Yes/No)

```cpp
bool confirmed = ui.dialogConfirm("XOA FILE", "Ban chac chan?");

if (confirmed) {
    // Người dùng chọn Yes
    deleteFile();
} else {
    // Người dùng chọn No
}
```

**Điều khiển:**
- UP/DOWN: Chuyển giữa Yes và No
- OK: Xác nhận lựa chọn

### 6.2. Dialog Thông Báo (Alert)

```cpp
ui.dialogAlert("THANH CONG", "File da duoc luu!");
// Chờ người dùng nhấn OK
```

---

## 7. Input

### 7.1. Nhập Số

```cpp
int value = ui.inputNumber("Nhap so luong:", 10, 1, 100);
// value: giá trị mặc định = 10
// Phạm vi: 1 đến 100
```

**Điều khiển:**
- UP: Tăng giá trị
- DOWN: Giảm giá trị
- OK: Xác nhận

**Tính năng:**
- Cuộn tròn: vượt max → về min, dưới min → lên max

### 7.2. Chọn Từ Danh Sách

```cpp
const char* options[] = {"Lua chon A", "Lua chon B", "Lua chon C"};
int selected = ui.inputSelect("Chon mot:", options, 3, 0);
// Mặc định chọn index 0
```

Sử dụng giao diện menu, trả về index được chọn.

---

## 8. Draw Helpers

Các hàm hỗ trợ vẽ trong callback hoặc giao diện tùy chỉnh.

### 8.1. Vẽ Danh Sách

```cpp
const char* items[] = {"Item 1", "Item 2", "Item 3", "Item 4", "Item 5"};
int selectedIdx = 2;
int yStart = 14;  // Bắt đầu từ Y = 14
int maxVisible = 5;  // Tối đa 5 dòng

ui.drawList(items, 5, selectedIdx, yStart, maxVisible);
```

- Item được chọn sẽ được highlight (nền trắng, chữ đen)
- Tự động cuộn nếu vượt quá maxVisible

### 8.2. Vẽ Text

```cpp
// Vẽ text tại vị trí
ui.drawText("Hello World", 10, 20);

// Vẽ text căn giữa
ui.drawTextCenter("Centered Text", 30);

// Vẽ hint ở cuối màn hình
ui.drawHint("UP/DN: Di chuyen  OK: Chon");
```

### 8.3. Vẽ Key-Value

```cpp
ui.drawKeyValue("Ten:", "ESP32", 14);
ui.drawKeyValue("IP:", "192.168.1.100", 24);
ui.drawKeyValue("RSSI:", "-65 dBm", 34);
```

Kết quả:
```
Ten:                ESP32
IP:         192.168.1.100
RSSI:            -65 dBm
```

### 8.4. Vẽ Progress Bar

```cpp
int y = 40;
int height = 10;  // Chiều cao thanh
ui.drawProgressBar(75, y, height);  // 75%
```

---

## 9. Tiện Ích

### 9.1. Xóa và Làm Mới Màn Hình

```cpp
ui.clear();    // Xóa và cập nhật màn hình
ui.refresh();  // Chỉ cập nhật (display.display())
```

### 9.2. Chờ Thả Nút

```cpp
ui.waitRelease();  // Chờ tất cả nút được thả
```

### 9.3. Kiểm Tra Nút Bấm

```cpp
if (ui.btnPressed(PIN_UP)) {
    // Nút UP đang được nhấn
}
```

---

## 10. Hằng Số

### Kích thước màn hình
```cpp
#define UI_SCREEN_W         128   // Chiều rộng
#define UI_SCREEN_H         64    // Chiều cao
#define UI_CHAR_W           6     // Chiều rộng ký tự
#define UI_CHAR_H           8     // Chiều cao ký tự
```

### Giới hạn
```cpp
#define UI_MAX_VISIBLE      7     // Số dòng tối đa hiển thị
#define UI_MAX_ITEMS        32    // Số mục menu tối đa
#define UI_MAX_TABS         6     // Số tabs tối đa
```

### Thời gian
```cpp
#define UI_DEBOUNCE_MS      200   // Debounce bình thường
#define UI_FAST_SCROLL_MS   80    // Cuộn nhanh (khi giữ)
#define UI_HOLD_MS          500   // Thời gian giữ để kích hoạt fast mode
#define UI_COMBO_WINDOW_MS  100   // Cửa sổ phát hiện combo
#define UI_FAST_TAB_MS      300   // Delay chuyển tab nhanh
#define UI_HEADER_TIMEOUT   2000  // Thời gian ẩn header (AUTO mode)
```

### Mã trả về
```cpp
#define UI_NO_ACTION        (-1)  // Không có hành động
#define UI_BACK             (-2)  // Quay lại (chưa dùng)
#define UI_TAB_SWITCHED     (-3)  // Đã chuyển tab
#define UI_ITEM_CHANGED     (-4)  // Item thay đổi
#define UI_EXIT             (-5)  // Thoát (nhấn OK)
```

### Header mode
```cpp
#define UI_HEADER_ALWAYS    0     // Luôn hiện
#define UI_HEADER_NEVER     1     // Luôn ẩn
#define UI_HEADER_AUTO      2     // Tự động
```

### Header style
```cpp
#define UI_HEADER_TABS      0     // Hiện tất cả tabs
#define UI_HEADER_SINGLE    1     // Chỉ hiện tab hiện tại
```

---

## 11. Ví Dụ Đầy Đủ

### 11.1. Menu Đơn Giản

```cpp
#include <Wire.h>
#include <Adafruit_SH110X.h>
#include "oled_ui.h"

#define PIN_UP    32
#define PIN_DOWN  33
#define PIN_OK    25

OledUI ui;
Adafruit_SH1106G display(128, 64, &Wire, -1);

void setup() {
    Serial.begin(115200);
    Wire.begin(21, 22);

    display.begin(0x3C, true);
    display.setRotation(2);
    display.clearDisplay();
    display.display();

    ui.begin(&display, PIN_UP, PIN_DOWN, PIN_OK);

    const char* menu[] = {
        "1. LED On/Off",
        "2. Wifi Config",
        "3. System Info",
        "4. Restart"
    };
    ui.menuCreate("MAIN MENU", menu, 4);
}

void loop() {
    int sel = ui.menuUpdate();

    if (sel >= 0) {
        switch (sel) {
            case 0:
                toggleLED();
                break;
            case 1:
                wifiSetup();
                break;
            case 2:
                showInfo();
                break;
            case 3:
                if (ui.dialogConfirm("RESTART", "Are you sure?")) {
                    ESP.restart();
                }
                break;
        }
        ui.menuRedraw();
    }
}

void toggleLED() {
    static bool on = false;
    on = !on;
    digitalWrite(LED_BUILTIN, on);
    ui.dialogAlert("LED", on ? "LED ON" : "LED OFF");
}

void wifiSetup() {
    ui.showMessage("Connecting...");
    delay(2000);
    ui.dialogAlert("WIFI", "Connected!");
}

void showInfo() {
    ui.showMessage("ESP32 OledUI", "Version 1.0.0");
    delay(2000);
}
```

### 11.2. Tabs với Callback

```cpp
#include <Wire.h>
#include <Adafruit_SH110X.h>
#include "oled_ui.h"

#define PIN_UP    32
#define PIN_DOWN  33
#define PIN_OK    25

OledUI ui;
Adafruit_SH1106G display(128, 64, &Wire, -1);

// Dữ liệu
const char* settingsItems[] = {"Brightness", "Volume", "Theme", "Language"};
int settingsValues[] = {80, 50, 0, 0};

void drawTabContent(int tab, int itemIdx, int yStart) {
    switch (tab) {
        case 0: {  // Home
            ui.drawTextCenter("ESP32 Monitor", yStart);

            char temp[20], humid[20];
            snprintf(temp, sizeof(temp), "%.1f C", 25.5);
            snprintf(humid, sizeof(humid), "%.1f %%", 65.0);

            ui.drawKeyValue("Temp:", temp, yStart + 14);
            ui.drawKeyValue("Humid:", humid, yStart + 24);
            ui.drawKeyValue("Uptime:", "12:34:56", yStart + 34);
            break;
        }

        case 1: {  // Settings
            ui.drawList(settingsItems, 4, itemIdx, yStart, 5);
            break;
        }

        case 2: {  // Info
            ui.drawKeyValue("Model:", "ESP32", yStart);
            ui.drawKeyValue("Flash:", "4MB", yStart + 10);
            ui.drawKeyValue("Heap:", "234KB", yStart + 20);
            ui.drawKeyValue("SDK:", "v5.1", yStart + 30);
            ui.drawHint("OK: Exit");
            break;
        }
    }
}

void setup() {
    Wire.begin(21, 22);
    display.begin(0x3C, true);
    display.setRotation(2);

    ui.begin(&display, PIN_UP, PIN_DOWN, PIN_OK);

    const char* tabs[] = {"Home", "Set", "Info"};
    int items[] = {0, 4, 0};  // Settings có 4 items

    ui.tabsCreate(tabs, 3, items);
    ui.tabsSetHeaderMode(UI_HEADER_AUTO);
    ui.tabsSetHeaderTimeout(3000);
    ui.tabsSetDrawCallback(drawTabContent);
}

void loop() {
    int result = ui.tabsRun();  // Blocking loop

    // Khi thoát, kiểm tra tab và xử lý
    int tab = ui.tabsGetCurrent();
    int item = ui.tabsGetItemIndex();

    if (tab == 1) {  // Settings
        // Mở input cho setting được chọn
        int newValue = ui.inputNumber(
            settingsItems[item],
            settingsValues[item],
            0, 100
        );
        settingsValues[item] = newValue;
    }

    // Quay lại tabs
    // Loop sẽ tự động chạy lại ui.tabsRun()
}
```

### 11.3. Progress với Animation

```cpp
void downloadFile() {
    ui.showMessage("Preparing...");
    delay(500);

    for (int i = 0; i <= 100; i++) {
        ui.showProgress("Downloading...", i);
        delay(30);
    }

    // Spinner trong khi xử lý
    for (int i = 0; i < 20; i++) {
        ui.showSpinner("Processing...", i);
        delay(100);
    }

    ui.dialogAlert("DONE", "Download complete!");
}
```

---

## Lời Kết

Thư viện OledUI cung cấp các thành phần UI cơ bản để xây dựng giao diện người dùng trên màn hình OLED một cách nhanh chóng và dễ dàng. Với hệ thống menu, tabs, dialog và các hàm vẽ hỗ trợ, bạn có thể tạo ra các ứng dụng embedded với giao diện thân thiện.

**Mẹo:**
1. Sử dụng `tabsRun()` với callback cho các ứng dụng phức tạp
2. Dùng `tabsForceRedraw()` để cập nhật realtime từ task khác
3. Kết hợp `dialogConfirm()` trước các thao tác quan trọng
4. Sử dụng `drawKeyValue()` để hiển thị thông tin dạng bảng

Nếu có thắc mắc, vui lòng liên hệ tác giả TTP27.
