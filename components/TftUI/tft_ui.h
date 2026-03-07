/**
 * @file tft_ui.h
 * @brief Thu vien UI cho man hinh TFT ST7735 (SPI, 128x160, 16-bit color)
 * @author TTP27
 * @version 1.0.0
 *
 * Port tu OledUI (SH1106G 128x64 mono) sang ST7735 (128x160 color).
 * Giu nguyen API, them mau sac + backlight control.
 *
 * ============================================================
 * THU VIEN CAN TAI:
 * ============================================================
 *
 * ESP-IDF component:
 *   - Adafruit_GFX
 *   - Adafruit_ST7735
 *
 * ============================================================
 * PHAN CUNG:
 * ============================================================
 *
 * - Man hinh: ST7735 128x160 SPI
 * - 3 nut bam: UP, DOWN, OK (INPUT_PULLUP, active LOW)
 * - Backlight: GPIO dieu khien qua MOSFET (HIGH = ON)
 *
 * ============================================================
 * SU DUNG:
 * ============================================================
 *
 *   #include "tft_ui.h"
 *
 *   #define TFT_MOSI 34
 *   #define TFT_SCLK 33
 *   #define TFT_CS   -1
 *   #define TFT_RST  35
 *   #define TFT_DC   36
 *   #define TFT_BL   37
 *
 *   Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
 *   TftUI ui;
 *
 *   void setup() {
 *       tft.initR(INITR_BLACKTAB);
 *       tft.setRotation(0);
 *       ui.begin(&tft, PIN_UP, PIN_DOWN, PIN_OK, TFT_BL);
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
 */

#ifndef __TFT_UI_H__
#define __TFT_UI_H__

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

// ============================================================
// SCREEN CONSTANTS
// ============================================================

#define UI_SCREEN_W         160
#define UI_SCREEN_H         128
#define UI_CHAR_W           6
#define UI_CHAR_H           8
#define UI_LINE_H           12    // Chieu cao moi dong item
#define UI_HEADER_H         14    // Chieu cao header
#define UI_MAX_VISIBLE      9     // So dong toi da (voi header) — landscape
#define UI_MAX_VISIBLE_NH   10    // So dong toi da (khong header) — landscape
#define UI_MAX_ITEMS        32
#define UI_MAX_TABS         6

// ============================================================
// COLOR THEME (RGB565) — Thay doi o day de custom
// ============================================================

#define UI_COL_BG           ST77XX_BLACK    // 0x0000  Nen
#define UI_COL_TEXT         ST77XX_WHITE    // 0xFFFF  Chu chinh
#define UI_COL_HEADER_BG   0x001F          // Blue    Nen header
#define UI_COL_HEADER_FG   ST77XX_WHITE    //         Chu header
#define UI_COL_SEL_BG      0x07FF          // Cyan    Nen item duoc chon
#define UI_COL_SEL_FG      ST77XX_BLACK    //         Chu item duoc chon
#define UI_COL_HINT        0x7BEF          // Gray    Chu hint/mo
#define UI_COL_BAR_FILL    0x07E0          // Green   Thanh progress
#define UI_COL_BAR_FRAME   ST77XX_WHITE    //         Vien progress
#define UI_COL_BTN_BG      0x001F          // Blue    Nen nut dialog
#define UI_COL_BTN_FG      ST77XX_WHITE    //         Chu nut dialog
#define UI_COL_BTN_SEL_BG  0x07FF          // Cyan    Nen nut duoc chon
#define UI_COL_BTN_SEL_FG  ST77XX_BLACK    //         Chu nut duoc chon

// ============================================================
// TIMING & ACTION CODES
// ============================================================

#define UI_DEBOUNCE_MS      200
#define UI_FAST_SCROLL_MS   80
#define UI_HOLD_MS          500
#define UI_COMBO_WINDOW_MS  100
#define UI_FAST_TAB_MS      300

#define UI_NO_ACTION        (-1)
#define UI_BACK             (-2)
#define UI_TAB_SWITCHED     (-3)
#define UI_ITEM_CHANGED     (-4)
#define UI_EXIT             (-5)

// Tab header visibility modes
#define UI_HEADER_ALWAYS    0
#define UI_HEADER_NEVER     1
#define UI_HEADER_AUTO      2

// Tab header styles
#define UI_HEADER_TABS      0     // Hien tat ca tabs
#define UI_HEADER_SINGLE    1     // Chi hien ten tab hien tai

#define UI_HEADER_TIMEOUT   2000

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

class TftUI {
public:
    TftUI();

    /**
     * @brief Khoi tao UI
     * @param disp   Con tro man hinh Adafruit_ST7735
     * @param pinUp   Pin nut UP
     * @param pinDown Pin nut DOWN
     * @param pinOk   Pin nut OK
     * @param pinBL   Pin backlight (-1 = khong dung)
     */
    void begin(Adafruit_ST7735* disp, uint8_t pinUp, uint8_t pinDown, uint8_t pinOk, int8_t pinBL = -1);

    // === BACKLIGHT ===
    void backlightOn();
    void backlightOff();

    // === MENU ===
    void menuCreate(const char* title, const char** items, int count);
    int menuUpdate();
    void menuRedraw();
    int menuGetIndex();
    void menuSetIndex(int idx);

    // === TABS ===
    void tabsCreate(const char** tabs, int tabCount, const int* itemCounts = nullptr);
    void tabsDraw();
    int tabsGetCurrent();
    void tabsSetCurrent(int idx);
    int tabsGetItemIndex();
    void tabsSetItemIndex(int idx);
    int tabsGetItemCount();
    void tabsNext();
    void tabsPrev();
    int tabsUpdate();
    void tabsReset();

    // Tab header config
    void tabsSetHeaderMode(uint8_t mode);
    void tabsSetHeaderStyle(uint8_t style);
    void tabsSetHeaderTimeout(unsigned long ms);
    bool tabsIsHeaderVisible();
    int tabsGetHeaderHeight();

    // Tab callback & redraw
    void tabsSetDrawCallback(TabDrawCallback callback);
    int tabsRun();
    void tabsForceRedraw(int tab = -1);
    void tabsMarkDirty(int tab = -2);
    bool tabsIsDirty(int tab = -1);
    void tabsClearDirty(int tab = -1);

    // === MESSAGE ===
    void showMessage(const char* line1, const char* line2 = "");
    void showProgress(const char* title, int percent);
    void showSpinner(const char* title, int frame);

    // === DIALOG ===
    bool dialogConfirm(const char* title, const char* msg);
    void dialogAlert(const char* title, const char* msg);

    // === INPUT ===
    int inputNumber(const char* title, int val, int minV, int maxV);
    int inputSelect(const char* title, const char** opts, int count, int defIdx = 0);

    // === DRAW HELPERS ===
    void drawList(const char** items, int count, int selIdx, int yStart, int maxVisible = 5);
    void drawText(const char* text, int x, int y);
    void drawTextCenter(const char* text, int y);
    void drawHint(const char* text);
    void drawKeyValue(const char* key, const char* value, int y);
    void drawProgressBar(int percent, int y, int height = 12);

    // === UTILITIES ===
    void clear();
    void refresh();
    void waitRelease();
    bool btnPressed(uint8_t pin);

    // === DISPLAY ACCESS ===
    Adafruit_ST7735* getDisplay() { return _disp; }

private:
    Adafruit_ST7735* _disp;
    uint8_t _pinUp, _pinDown, _pinOk;
    int8_t _pinBL;

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
    int _tabItemCounts[UI_MAX_TABS];
    int _tabItemIdx[UI_MAX_TABS];

    // Tab header
    uint8_t _headerMode;
    uint8_t _headerStyle;
    unsigned long _headerTimeout;
    unsigned long _headerShowTime;

    // Tab callback
    TabDrawCallback _tabDrawCallback;
    bool _tabNeedRedraw;
    bool _tabDirty[UI_MAX_TABS];

    // Screen mode tracking — auto fillScreen on transition
    // 0=none, 1=message, 2=progress, 3=spinner, 4=dialog, 5=tabs
    uint8_t _screenMode;

    // Combo tracking for tabs
    unsigned long _comboFirstBtnTime;
    unsigned long _comboLastSwitch;
    bool _comboWaiting;
    bool _comboFirstSwitch;

    // Tab button tracking
    unsigned long _tabLastDebounce;
    unsigned long _tabHoldStart;
    bool _tabHolding;

    // Menu button tracking
    unsigned long _lastDebounce;
    unsigned long _holdStart;
    bool _holding;

    void _menuDraw();
    void _menuDrawItem(int idx);
    void _menuDrawIndicator();
    void _fillContentArea(int yStart);
    bool _btnRead(uint8_t pin);
    unsigned long _getDelay();

    // List scroll (auto-scroll long selected items)
    bool _listScrollActive;
};

#endif
