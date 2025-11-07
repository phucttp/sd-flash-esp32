// menu.cpp
#include "menu.h"

// --- CÁC BIẾN NỘI BỘ (STATIC) ---
static Adafruit_SSD1306* _display;
static const char** _menuItems;
static const char** _fw_ids;
static int _menuLength;
static int _currentIndex;

// (MỚI) Biến theo dõi scrolling
// _menuTopIndex là index của mục đang hiển thị ở dòng TRÊN CÙNG
static int _menuTopIndex; 
static const int _maxLines = 4; // Vì màn hình 32 / 8 = 4 dòng

static unsigned long _lastDebounce = 0;
static const unsigned long _debounceDelay = 200;
extern Adafruit_SSD1306 display;

// --- HÀM NỘI BỘ (STATIC) ---

// (CẬP NHẬT) Hàm drawMenu() giờ sẽ thông minh hơn
static void drawMenu() {
    _display->clearDisplay();
    _display->setTextSize(1);
    _display->setTextColor(SSD1306_WHITE);

    // Vòng lặp này chỉ chạy 4 lần (cho 4 dòng)
    for (int i = 0; i < _maxLines; i++) {
        
        // Tính toán index thật của mục menu
        int itemIndex = _menuTopIndex + i; 

        // Nếu itemIndex vượt quá menu, không vẽ nữa
        if (itemIndex >= _menuLength) {
            break; 
        }

        // Tính toán tọa độ Y cho dòng (luôn là 0, 8, 16, 24)
        int yPos = i * 8; 

        // Lấy nội dung text
        const char* itemText = _menuItems[itemIndex];

        // So sánh index thật với index đang chọn
        if (itemIndex == _currentIndex) {
            _display->fillRect(0, yPos, SCREEN_WIDTH, 8, SSD1306_WHITE);
            _display->setTextColor(SSD1306_BLACK, SSD1306_WHITE); // highlight
        } else {
            _display->setTextColor(SSD1306_WHITE, SSD1306_BLACK);
        }
        
        _display->setCursor(2, yPos);
        _display->println(itemText);
    }
    _display->display();
}

// --- ĐỊNH NGHĨA CÁC HÀM PUBLIC ---

// (CẬP NHẬT) menu_init
void menu_init(Adafruit_SSD1306& disp, const char* displayItems[], const char* idItems[], int len) {
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


// (CẬP NHẬT) menu_update với logic SCROLLING
int menu_update() {
    if (millis() - _lastDebounce > _debounceDelay) {
        
        bool menuChanged = false; // Dùng cờ để biết khi nào cần vẽ lại

        // Nút UP
        if (digitalRead(BTN_UP) == LOW) {
            _currentIndex--;
            if (_currentIndex < 0) {
                // (MỚI) Quay vòng từ 0 lên cuối
                _currentIndex = _menuLength - 1; 
                // Cập nhật top index để thấy 4 mục cuối
                _menuTopIndex = _menuLength - _maxLines;
                if (_menuTopIndex < 0) _menuTopIndex = 0; // Cho list < 4 mục
            }
            
            // (MỚI) Nếu index chọn < top index -> trượt menu xuống
            if (_currentIndex < _menuTopIndex) {
                _menuTopIndex = _currentIndex;
            }
            menuChanged = true;
        }
        // Nút DOWN
        else if (digitalRead(BTN_DOWN) == LOW) {
            _currentIndex++;
            if (_currentIndex >= _menuLength) {
                // (MỚI) Quay vòng từ cuối về 0
                _currentIndex = 0; 
                _menuTopIndex = 0; // Reset về đầu
            }

            // (MỚI) Nếu index chọn > (top + 3) -> trượt menu lên
            // Vd: top=0, maxLines=4. Mục 3 (0+4-1) là mục cuối.
            // Nếu _currentIndex > 3 (là 4), thì top=1 (4 - 4 + 1)
            if (_currentIndex >= (_menuTopIndex + _maxLines)) {
                _menuTopIndex = _currentIndex - _maxLines + 1;
            }
            menuChanged = true;
        }
        // Nút OK
        else if (digitalRead(BTN_OK) == LOW) {
            _lastDebounce = millis();
            return _currentIndex; // Trả về index
        }

        // (MỚI) Chỉ vẽ lại khi menu thay đổi
        if (menuChanged) {
            drawMenu();
            _lastDebounce = millis();
        }
    }
    return -1;
}

// (KHÔNG ĐỔI) menu_display_selection
void menu_display_selection(int index) {
    if (index < 0 || index >= _menuLength) return; 
    const char* item = _menuItems[index]; 

    _display->clearDisplay();
    _display->setTextSize(1);
    _display->setTextColor(SSD1306_WHITE);
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
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 10);
    display.println(line1);
    if (strlen(line2) > 0) {
        display.setCursor(0, 20);
        display.println(line2);
    }
    display.display();
}