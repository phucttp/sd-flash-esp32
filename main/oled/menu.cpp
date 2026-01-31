// menu.cpp
#include "menu.h"

// --- CÁC BIẾN NỘI BỘ (STATIC) ---
// static Adafruit_SSD1306* _display;  // OLD
static Adafruit_SH1106G* _display;     // NEW: SH1106G
static const char** _menuItems;
static const char** _fw_ids;
static int _menuLength;
static int _currentIndex;

// (MỚI) Biến theo dõi scrolling
// _menuTopIndex là index của mục đang hiển thị ở dòng TRÊN CÙNG
static int _menuTopIndex; 
// static const int _maxLines = 4; // OLD: màn hình 32 / 8 = 4 dòng
static const int _maxLines = 8;    // NEW: SH1106G 64 / 8 = 8 dòng

static unsigned long _lastDebounce = 0;
static unsigned long _holdStartTime = 0;   // [NEW] Thời điểm bắt đầu giữ nút
static bool _isHolding = false;            // [NEW] Đang giữ nút?
static const unsigned long _debounceDelay = 200;      // Delay bình thường
static const unsigned long _fastScrollDelay = 80;     // Delay khi cuộn nhanh
static const unsigned long _holdThreshold = 500;      // Giữ 500ms = cuộn nhanh

// [V1] Gesture: UP+DOWN 3s -> Tools Menu
static unsigned long _comboStartTime = 0;  // Thời điểm bắt đầu giữ combo UP+DOWN
static bool _comboActive = false;          // Đang giữ combo?
static const unsigned long _comboThreshold = 800;  // 0.8 giây
// extern Adafruit_SSD1306 display;  // OLD
extern Adafruit_SH1106G display;      // NEW: SH1106G

// --- HÀM NỘI BỘ (STATIC) ---

// [FIX] drawMenu() với position indicator
static void drawMenu() {
    _display->clearDisplay();
    _display->setTextSize(1);
    _display->setTextColor(SH110X_WHITE);

    // Vẽ các mục menu (tối đa _maxLines dòng)
    for (int i = 0; i < _maxLines; i++) {
        int itemIndex = _menuTopIndex + i;
        if (itemIndex >= _menuLength) break;

        int yPos = i * 8;
        const char* itemText = _menuItems[itemIndex];

        // Highlight mục đang chọn
        if (itemIndex == _currentIndex) {
            _display->fillRect(0, yPos, SCREEN_WIDTH, 8, SH110X_WHITE);
            _display->setTextColor(SH110X_BLACK, SH110X_WHITE);
        } else {
            _display->setTextColor(SH110X_WHITE, SH110X_BLACK);
        }

        _display->setCursor(2, yPos);
        _display->println(itemText);
    }

    // [NEW] Vẽ position indicator góc dưới phải (nếu có nhiều hơn 1 trang)
    if (_menuLength > _maxLines) {
        char posStr[24];  // Đủ lớn cho "2147483647/2147483647"
        snprintf(posStr, sizeof(posStr), "%d/%d", _currentIndex + 1, _menuLength);

        // Tính vị trí X để căn phải
        int textWidth = strlen(posStr) * 6; // ~6 pixel/char ở textSize 1
        int xPos = SCREEN_WIDTH - textWidth - 2;
        int yPos = SCREEN_HEIGHT - 8; // Dòng cuối

        // Vẽ nền đen để dễ đọc
        _display->fillRect(xPos - 2, yPos, textWidth + 4, 8, SH110X_BLACK);
        _display->setTextColor(SH110X_WHITE);
        _display->setCursor(xPos, yPos);
        _display->print(posStr);
    }

    _display->display();
}

// --- ĐỊNH NGHĨA CÁC HÀM PUBLIC ---

// (CẬP NHẬT) menu_init
// void menu_init(Adafruit_SSD1306& disp, ...) // OLD
void menu_init(Adafruit_SH1106G& disp, const char* displayItems[], const char* idItems[], int len) {  // NEW: SH1106G
    _display = &disp;
    _menuItems = displayItems;
    _fw_ids = idItems;
    _menuLength = len;
    _currentIndex = 0;
    _lastDebounce = 0;
    _menuTopIndex = 0; // (MỚI) Khởi tạo
    
    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_OK, INPUT_PULLUP);

    drawMenu();
}

// (KHÔNG ĐỔI) Hàm lấy ID
const char* menu_get_id(int index) {
    if (index < 0 || index >= _menuLength) {
        return NULL;
    }
    return _fw_ids[index];
}


// [V1] menu_update với FAST SCROLL và COMBO GESTURE
int menu_update() {
    bool btnUp = (digitalRead(BTN_UP) == LOW);
    bool btnDown = (digitalRead(BTN_DOWN) == LOW);
    bool btnOk = (digitalRead(BTN_OK) == LOW);

    // ============================================================
    // [V1] COMBO GESTURE: UP + DOWN cùng lúc trong 3 giây -> Tools Menu
    // ============================================================
    if (btnUp && btnDown) {
        if (!_comboActive) {
            // Bắt đầu tracking combo
            _comboStartTime = millis();
            _comboActive = true;
        } else {
            // Đang giữ combo - tính thời gian
            unsigned long elapsed = millis() - _comboStartTime;

            // Hiển thị progress bar
            int progress = (elapsed * 100) / _comboThreshold;
            if (progress > 100) progress = 100;

            // Vẽ màn hình progress
            _display->clearDisplay();
            _display->setTextSize(1);
            _display->setTextColor(SH110X_WHITE);

            // Title
            _display->setCursor(20, 10);
            _display->println("TOOLS MENU");
            _display->setCursor(15, 22);
            _display->println("Hold to unlock...");

            // Progress bar
            int barX = 14;
            int barY = 38;
            int barW = 100;
            int barH = 12;
            _display->drawRect(barX, barY, barW, barH, SH110X_WHITE);
            _display->fillRect(barX + 2, barY + 2, (barW - 4) * progress / 100, barH - 4, SH110X_WHITE);

            // Countdown text
            int remaining = (_comboThreshold - elapsed) / 1000 + 1;
            if (remaining < 1) remaining = 0;
            char countStr[16];
            snprintf(countStr, sizeof(countStr), "%d sec", remaining);
            _display->setCursor(50, 54);
            _display->println(countStr);

            _display->display();

            // Đủ 3 giây -> Mở Tools Menu
            if (elapsed >= _comboThreshold) {
                _comboActive = false;
                _comboStartTime = 0;
                return MENU_TOOLS_GESTURE;
            }
        }
        return MENU_NO_ACTION;  // Đang giữ combo, chưa xử lý gì khác
    } else {
        // Thả combo
        if (_comboActive) {
            _comboActive = false;
            _comboStartTime = 0;
            drawMenu();  // Vẽ lại menu bình thường
        }
    }

    // ============================================================
    // XỬ LÝ BÌNH THƯỜNG: Cuộn menu
    // ============================================================

    // --- Xử lý Hold Detection (cho fast scroll) ---
    if (btnUp || btnDown) {
        if (!_isHolding) {
            _holdStartTime = millis();
            _isHolding = true;
        }
    } else {
        _isHolding = false;
        _holdStartTime = 0;
    }

    // --- Tính delay dựa trên thời gian giữ ---
    unsigned long currentDelay = _debounceDelay;
    if (_isHolding && (millis() - _holdStartTime > _holdThreshold)) {
        currentDelay = _fastScrollDelay;
    }

    // --- Kiểm tra debounce ---
    if (millis() - _lastDebounce < currentDelay) {
        return MENU_NO_ACTION;
    }

    bool menuChanged = false;

    // Nút UP (chỉ UP, không phải combo)
    if (btnUp && !btnDown) {
        _currentIndex--;
        if (_currentIndex < 0) {
            _currentIndex = _menuLength - 1;
            _menuTopIndex = _menuLength - _maxLines;
            if (_menuTopIndex < 0) _menuTopIndex = 0;
        }
        if (_currentIndex < _menuTopIndex) {
            _menuTopIndex = _currentIndex;
        }
        menuChanged = true;
    }
    // Nút DOWN (chỉ DOWN, không phải combo)
    else if (btnDown && !btnUp) {
        _currentIndex++;
        if (_currentIndex >= _menuLength) {
            _currentIndex = 0;
            _menuTopIndex = 0;
        }
        if (_currentIndex >= (_menuTopIndex + _maxLines)) {
            _menuTopIndex = _currentIndex - _maxLines + 1;
        }
        menuChanged = true;
    }
    // Nút OK
    else if (btnOk) {
        _lastDebounce = millis();
        _isHolding = false;
        return _currentIndex;
    }

    if (menuChanged) {
        drawMenu();
        _lastDebounce = millis();
    }

    return MENU_NO_ACTION;
}

// (KHÔNG ĐỔI) menu_display_selection
void menu_display_selection(int index) {
    if (index < 0 || index >= _menuLength) return; 
    const char* item = _menuItems[index]; 

    _display->clearDisplay();
    _display->setTextSize(1);
    _display->setTextColor(SH110X_WHITE);   // NEW: SH110X
    _display->setCursor(10, 10);
    _display->println("Selected:");
    _display->setCursor(10, 20);
    _display->println(item);
    _display->display();
}

// (CẬP NHẬT) menu_redisplay
void menu_redisplay() {
    // (MỚI) Cần đảm bảo _menuTopIndex đúng trước khi vẽ lại
    // Nếu _currentIndex nằm ngoài cửa sổ, reset nó
    if (_currentIndex < _menuTopIndex || _currentIndex >= (_menuTopIndex + _maxLines)) {
        // Đặt _currentIndex ở dòng thứ 2 (cho đẹp)
        _menuTopIndex = _currentIndex - 1; 
        if (_menuTopIndex < 0) _menuTopIndex = 0;
        // Đảm bảo không cuộn lố
        if (_menuTopIndex > _menuLength - _maxLines) {
            _menuTopIndex = _menuLength - _maxLines;
            if (_menuTopIndex < 0) _menuTopIndex = 0;
        }
    }
    drawMenu();
}

/**
 * @brief Hàm helper để hiển thị thông báo nhanh ra OLED
 */
void oled_show_message(const char* line1, const char* line2) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);   // NEW: SH110X
    display.setCursor(0, 10);
    display.println(line1);
    if (strlen(line2) > 0) {
        display.setCursor(0, 20);
        display.println(line2);
    }
    display.display();
}