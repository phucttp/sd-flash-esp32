/**
 * @file oled_ui.h
 * @brief Thu vien UI cho man hinh OLED (SH1106G/SSD1306)
 * @author TTP27
 * @version 1.0.0
 *
 * ============================================================
 * THU VIEN CAN TAI:
 * ============================================================
 *
 * PlatformIO (them vao platformio.ini):
 *   lib_deps =
 *       adafruit/Adafruit GFX Library
 *       adafruit/Adafruit SH110X
 *       adafruit/Adafruit BusIO
 *
 * Arduino IDE (Library Manager):
 *   1. Adafruit GFX Library
 *   2. Adafruit SH110X (cho SH1106G)
 *   3. Adafruit BusIO
 *
 * ============================================================
 * PHAN CUNG MAC DINH:
 * ============================================================
 *
 * - Man hinh: SH1106G 128x64 I2C (0x3C)
 * - 3 nut bam: UP, DOWN, OK (INPUT_PULLUP, active LOW)
 *
 * ============================================================
 * SU DUNG:
 * ============================================================
 *
 *   #include "oled_ui/oled_ui.h"
 *
 *   OledUI ui;
 *   Adafruit_SH1106G display(128, 64, &Wire, -1);
 *
 *   void setup() {
 *       display.begin(0x3C, true);
 *       ui.begin(&display, PIN_UP, PIN_DOWN, PIN_OK);
 *
 *       const char* items[] = {"Item 1", "Item 2", "Item 3"};
 *       ui.menuCreate("MENU", items, 3);
 *   }
 *
 *   void loop() {
 *       int sel = ui.menuUpdate();
 *       if (sel >= 0) {
 *           // Xu ly item duoc chon
 *       }
 *   }
 *
 */

#ifndef __OLED_UI_H__
#define __OLED_UI_H__

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// ============================================================
// CONSTANTS
// ============================================================

#define UI_SCREEN_W         128
#define UI_SCREEN_H         64
#define UI_CHAR_W           6
#define UI_CHAR_H           8
#define UI_MAX_VISIBLE      7
#define UI_MAX_ITEMS        32
#define UI_MAX_TABS         6

#define UI_DEBOUNCE_MS      200
#define UI_FAST_SCROLL_MS   80
#define UI_HOLD_MS          500
#define UI_COMBO_WINDOW_MS  100   // Cua so cho nut thu 2
#define UI_FAST_TAB_MS      300   // Delay khi fast switch tab

#define UI_NO_ACTION        (-1)
#define UI_BACK             (-2)
#define UI_TAB_SWITCHED     (-3)  // Tab da chuyen
#define UI_ITEM_CHANGED     (-4)  // Item trong tab thay doi
#define UI_EXIT             (-5)  // Thoat (OK pressed)

// Tab header visibility modes
#define UI_HEADER_ALWAYS    0     // Luon hien header
#define UI_HEADER_NEVER     1     // Luon an header
#define UI_HEADER_AUTO      2     // Tu dong an/hien

// Tab header styles
#define UI_HEADER_TABS      0     // Hien tat ca tabs (mac dinh)
#define UI_HEADER_SINGLE    1     // Chi hien ten tab hien tai

#define UI_HEADER_TIMEOUT   2000  // Mac dinh 2 giay

// ============================================================
// CALLBACK TYPES
// ============================================================

/**
 * @brief Callback ve noi dung tab
 * @param tab       Index tab hien tai
 * @param itemIdx   Index item dang chon (trong tab)
 * @param yStart    Vi tri Y bat dau ve content
 */
typedef void (*TabDrawCallback)(int tab, int itemIdx, int yStart);

// ============================================================
// CLASS
// ============================================================

class OledUI {
public:
    OledUI();

    /**
     * @brief Khoi tao UI
     * @param disp  Con tro man hinh Adafruit_SH110X
     * @param pinUp   Pin nut UP
     * @param pinDown Pin nut DOWN
     * @param pinOk   Pin nut OK
     */
    void begin(Adafruit_SH110X* disp, uint8_t pinUp, uint8_t pinDown, uint8_t pinOk);

    // === MENU ===

    /**
     * @brief Tao menu
     * @param title Tieu de (NULL = khong header)
     * @param items Mang chuoi
     * @param count So luong
     */
    void menuCreate(const char* title, const char** items, int count);

    /**
     * @brief Cap nhat menu (goi trong loop)
     * @return Index duoc chon, hoac UI_NO_ACTION
     */
    int menuUpdate();

    /**
     * @brief Ve lai menu
     */
    void menuRedraw();

    /**
     * @brief Lay/set index hien tai
     */
    int menuGetIndex();
    void menuSetIndex(int idx);

    // === TABS ===

    /**
     * @brief Tao tabs voi so luong item moi tab
     * @param tabs       Mang ten tab
     * @param tabCount   So luong tab
     * @param itemCounts Mang so luong item moi tab (NULL = khong co items)
     *
     * Su dung:
     *   const char* tabs[] = {"Home", "Set", "Info"};
     *   int items[] = {5, 4, 3};  // Home: 5 items, Set: 4, Info: 3
     *   ui.tabsCreate(tabs, 3, items);
     */
    void tabsCreate(const char** tabs, int tabCount, const int* itemCounts = nullptr);

    /**
     * @brief Ve tabs header
     */
    void tabsDraw();

    /**
     * @brief Lay/set tab hien tai
     */
    int tabsGetCurrent();
    void tabsSetCurrent(int idx);

    /**
     * @brief Lay/set item index cua tab hien tai
     */
    int tabsGetItemIndex();
    void tabsSetItemIndex(int idx);

    /**
     * @brief Lay so luong items cua tab hien tai
     */
    int tabsGetItemCount();

    /**
     * @brief Chuyen tab (LEFT/RIGHT)
     */
    void tabsNext();
    void tabsPrev();

    /**
     * @brief Update tabs - xu ly tat ca nut nhan
     * @return UI_TAB_SWITCHED  - da chuyen tab
     *         UI_ITEM_CHANGED  - item thay doi (UP/DOWN)
     *         UI_EXIT          - nhan OK (thoat hoac chon item)
     *         UI_NO_ACTION     - khong co gi
     *
     * Su dung:
     *   while (true) {
     *       int result = ui.tabsUpdate();
     *       if (result == UI_TAB_SWITCHED || result == UI_ITEM_CHANGED) {
     *           // Ve lai giao dien
     *       }
     *       if (result == UI_EXIT) break;
     *   }
     */
    int tabsUpdate();

    /**
     * @brief Reset trang thai tabs
     */
    void tabsReset();

    /**
     * @brief Cau hinh header mode (visibility)
     * @param mode UI_HEADER_ALWAYS, UI_HEADER_NEVER, UI_HEADER_AUTO
     */
    void tabsSetHeaderMode(uint8_t mode);

    /**
     * @brief Cau hinh header style
     * @param style UI_HEADER_TABS (tat ca tabs) hoac UI_HEADER_SINGLE (chi ten tab)
     */
    void tabsSetHeaderStyle(uint8_t style);

    /**
     * @brief Set thoi gian tu dong an header (chi cho AUTO mode)
     * @param ms Milliseconds (mac dinh 2000ms)
     */
    void tabsSetHeaderTimeout(unsigned long ms);

    /**
     * @brief Kiem tra header dang visible khong
     */
    bool tabsIsHeaderVisible();

    /**
     * @brief Lay chieu cao header (0 neu an, 10 neu hien)
     */
    int tabsGetHeaderHeight();

    /**
     * @brief Set callback ve noi dung tab
     * @param callback Ham callback
     */
    void tabsSetDrawCallback(TabDrawCallback callback);

    /**
     * @brief Chay tabs loop (blocking)
     * Tu dong xu ly button, redraw, goi callback
     * Thoat khi nhan OK
     * @return Index tab cuoi cung khi thoat
     */
    int tabsRun();

    /**
     * @brief Ve lai tab ngay lap tuc (neu dang o tab do)
     * Neu khong o tab do thi mark dirty, se ve khi chuyen qua
     *
     * @param tab Index tab (-1 = tab hien tai)
     *
     * Su dung voi tabsRun():
     *   // Task khac update data
     *   void updateTask() {
     *       networkProgress = 75;
     *       ui.tabsForceRedraw(3);  // Ve lai tab 3 ngay (neu dang o tab 3)
     *   }
     */
    void tabsForceRedraw(int tab = -1);

    /**
     * @brief Danh dau tab can ve lai (chi mark, khong ve)
     * @param tab Index tab (-1 = tat ca tabs, -2 = tab hien tai)
     */
    void tabsMarkDirty(int tab = -2);

    /**
     * @brief Kiem tra tab co can ve lai khong
     * @param tab Index tab (-1 = tab hien tai)
     */
    bool tabsIsDirty(int tab = -1);

    /**
     * @brief Xoa dirty flag cua tab
     * @param tab Index tab (-1 = tat ca tabs)
     */
    void tabsClearDirty(int tab = -1);

    // === MESSAGE ===

    /**
     * @brief Hien thi thong bao
     */
    void showMessage(const char* line1, const char* line2 = "");

    /**
     * @brief Hien thi progress bar
     * @param title   Tieu de
     * @param percent 0-100
     */
    void showProgress(const char* title, int percent);

    /**
     * @brief Hien thi spinner
     * @param title Tieu de
     * @param frame Frame animation (0-3)
     */
    void showSpinner(const char* title, int frame);

    // === DIALOG ===

    /**
     * @brief Hien thi dialog Yes/No
     * @return true = Yes, false = No
     */
    bool dialogConfirm(const char* title, const char* msg);

    /**
     * @brief Hien thi dialog OK
     */
    void dialogAlert(const char* title, const char* msg);

    // === INPUT ===

    /**
     * @brief Nhap so
     */
    int inputNumber(const char* title, int val, int minV, int maxV);

    /**
     * @brief Chon tu list
     */
    int inputSelect(const char* title, const char** opts, int count, int defIdx = 0);

    // === DRAW HELPERS ===

    /**
     * @brief Ve danh sach items voi selection highlight
     * @param items     Mang chuoi
     * @param count     So luong items
     * @param selIdx    Index dang chon
     * @param yStart    Vi tri Y bat dau
     * @param maxVisible So dong toi da hien thi (mac dinh 5)
     */
    void drawList(const char** items, int count, int selIdx, int yStart, int maxVisible = 5);

    /**
     * @brief Ve text tai vi tri
     * @param text  Chuoi can ve
     * @param x     Vi tri X
     * @param y     Vi tri Y
     */
    void drawText(const char* text, int x, int y);

    /**
     * @brief Ve text can giua man hinh
     * @param text  Chuoi can ve
     * @param y     Vi tri Y
     */
    void drawTextCenter(const char* text, int y);

    /**
     * @brief Ve hint o cuoi man hinh
     * @param text  Chuoi hint
     */
    void drawHint(const char* text);

    /**
     * @brief Ve key-value pair
     * @param key   Ten
     * @param value Gia tri
     * @param y     Vi tri Y
     */
    void drawKeyValue(const char* key, const char* value, int y);

    /**
     * @brief Ve progress bar tai vi tri
     * @param percent   0-100
     * @param y         Vi tri Y
     * @param height    Chieu cao bar (mac dinh 10)
     */
    void drawProgressBar(int percent, int y, int height = 10);

    // === UTILITIES ===

    void clear();
    void refresh();
    void waitRelease();
    bool btnPressed(uint8_t pin);

private:
    Adafruit_SH110X* _disp;
    uint8_t _pinUp, _pinDown, _pinOk;

    // Menu
    const char* _menuTitle;
    const char** _menuItems;
    int _menuCount;
    int _menuIdx;
    int _menuTop;

    // Tabs
    const char** _tabs;
    int _tabCount;
    int _tabIdx;
    int _tabItemCounts[UI_MAX_TABS];   // So item moi tab
    int _tabItemIdx[UI_MAX_TABS];      // Item index moi tab

    // Tab header
    uint8_t _headerMode;               // ALWAYS, NEVER, AUTO
    uint8_t _headerStyle;              // TABS, SINGLE
    unsigned long _headerTimeout;      // Thoi gian an header (AUTO mode)
    unsigned long _headerShowTime;     // Thoi diem bat dau hien header

    // Tab callback
    TabDrawCallback _tabDrawCallback;
    bool _tabNeedRedraw;               // Flag for force redraw
    bool _tabDirty[UI_MAX_TABS];       // Dirty flag per tab

    // Combo tracking for tabs
    unsigned long _comboFirstBtnTime;
    unsigned long _comboLastSwitch;
    bool _comboWaiting;
    bool _comboFirstSwitch;

    // Tab button tracking (rieng biet voi menu)
    unsigned long _tabLastDebounce;
    unsigned long _tabHoldStart;
    bool _tabHolding;

    // Button
    unsigned long _lastDebounce;
    unsigned long _holdStart;
    bool _holding;

    void _menuDraw();
    bool _btnRead(uint8_t pin);
    unsigned long _getDelay();
};

#endif
