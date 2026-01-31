/**
 * @file oled_ui.cpp
 * @brief Implementation cua OledUI
 */

#include "oled_ui.h"

// ============================================================
// CONSTRUCTOR
// ============================================================

OledUI::OledUI() {
    _disp = nullptr;
    _pinUp = _pinDown = _pinOk = 0;
    _menuTitle = nullptr;
    _menuItems = nullptr;
    _menuCount = 0;
    _menuIdx = 0;
    _menuTop = 0;
    _tabs = nullptr;
    _tabCount = 0;
    _tabIdx = 0;
    _lastDebounce = 0;
    _holdStart = 0;
    _holding = false;
    // Combo tracking
    _comboFirstBtnTime = 0;
    _comboLastSwitch = 0;
    _comboWaiting = false;
    _comboFirstSwitch = true;
    // Tab button tracking
    _tabLastDebounce = 0;
    _tabHoldStart = 0;
    _tabHolding = false;
    // Tab items
    for (int i = 0; i < UI_MAX_TABS; i++) {
        _tabItemCounts[i] = 0;
        _tabItemIdx[i] = 0;
        _tabDirty[i] = false;
    }
    // Tab header
    _headerMode = UI_HEADER_ALWAYS;
    _headerStyle = UI_HEADER_TABS;
    _headerTimeout = UI_HEADER_TIMEOUT;
    _headerShowTime = 0;
    // Tab callback
    _tabDrawCallback = nullptr;
    _tabNeedRedraw = false;
}

// ============================================================
// INIT
// ============================================================

void OledUI::begin(Adafruit_SH110X* disp, uint8_t pinUp, uint8_t pinDown, uint8_t pinOk) {
    _disp = disp;
    _pinUp = pinUp;
    _pinDown = pinDown;
    _pinOk = pinOk;

    pinMode(_pinUp, INPUT_PULLUP);
    pinMode(_pinDown, INPUT_PULLUP);
    pinMode(_pinOk, INPUT_PULLUP);
}

// ============================================================
// MENU
// ============================================================

void OledUI::menuCreate(const char* title, const char** items, int count) {
    _menuTitle = title;
    _menuItems = items;
    _menuCount = (count > UI_MAX_ITEMS) ? UI_MAX_ITEMS : count;
    _menuIdx = 0;
    _menuTop = 0;
    _menuDraw();
}

void OledUI::_menuDraw() {
    if (!_disp) return;

    _disp->clearDisplay();
    _disp->setTextSize(1);
    _disp->setTextColor(SH110X_WHITE);

    int yStart = 0;
    int maxLines = UI_MAX_VISIBLE;

    // Header
    if (_menuTitle) {
        _disp->fillRect(0, 0, UI_SCREEN_W, 10, SH110X_WHITE);
        _disp->setTextColor(SH110X_BLACK, SH110X_WHITE);
        int tw = strlen(_menuTitle) * UI_CHAR_W;
        _disp->setCursor((UI_SCREEN_W - tw) / 2, 1);
        _disp->print(_menuTitle);
        _disp->setTextColor(SH110X_WHITE);
        yStart = 12;
        maxLines = 6;
    }

    // Items
    for (int i = 0; i < maxLines; i++) {
        int idx = _menuTop + i;
        if (idx >= _menuCount) break;

        int yPos = yStart + i * 9;

        if (idx == _menuIdx) {
            _disp->fillRect(0, yPos - 1, UI_SCREEN_W, 9, SH110X_WHITE);
            _disp->setTextColor(SH110X_BLACK, SH110X_WHITE);
        } else {
            _disp->setTextColor(SH110X_WHITE, SH110X_BLACK);
        }

        _disp->setCursor(4, yPos);
        _disp->print(_menuItems[idx]);
    }

    // Position indicator
    if (_menuCount > maxLines) {
        char pos[24];
        snprintf(pos, sizeof(pos), "%d/%d", _menuIdx + 1, _menuCount);
        int tw = strlen(pos) * UI_CHAR_W;
        _disp->fillRect(UI_SCREEN_W - tw - 4, UI_SCREEN_H - 9, tw + 4, 9, SH110X_BLACK);
        _disp->setTextColor(SH110X_WHITE);
        _disp->setCursor(UI_SCREEN_W - tw - 2, UI_SCREEN_H - 8);
        _disp->print(pos);
    }

    _disp->display();
}

int OledUI::menuUpdate() {
    bool up = _btnRead(_pinUp);
    bool down = _btnRead(_pinDown);
    bool ok = _btnRead(_pinOk);

    // Hold detection
    if (up || down) {
        if (!_holding) {
            _holdStart = millis();
            _holding = true;
        }
    } else {
        _holding = false;
        _holdStart = 0;
    }

    // Debounce
    if (millis() - _lastDebounce < _getDelay()) {
        return UI_NO_ACTION;
    }

    int maxLines = _menuTitle ? 6 : UI_MAX_VISIBLE;
    bool changed = false;

    if (up) {
        _menuIdx--;
        if (_menuIdx < 0) {
            _menuIdx = _menuCount - 1;
            _menuTop = _menuCount - maxLines;
            if (_menuTop < 0) _menuTop = 0;
        }
        if (_menuIdx < _menuTop) {
            _menuTop = _menuIdx;
        }
        changed = true;
    }
    else if (down) {
        _menuIdx++;
        if (_menuIdx >= _menuCount) {
            _menuIdx = 0;
            _menuTop = 0;
        }
        if (_menuIdx >= _menuTop + maxLines) {
            _menuTop = _menuIdx - maxLines + 1;
        }
        changed = true;
    }
    else if (ok) {
        waitRelease();
        return _menuIdx;
    }

    if (changed) {
        _menuDraw();
        _lastDebounce = millis();
    }

    return UI_NO_ACTION;
}

void OledUI::menuRedraw() {
    _menuDraw();
}

int OledUI::menuGetIndex() {
    return _menuIdx;
}

void OledUI::menuSetIndex(int idx) {
    if (idx >= 0 && idx < _menuCount) {
        _menuIdx = idx;
        int maxLines = _menuTitle ? 6 : UI_MAX_VISIBLE;
        if (_menuIdx < _menuTop) _menuTop = _menuIdx;
        if (_menuIdx >= _menuTop + maxLines) _menuTop = _menuIdx - maxLines + 1;
        _menuDraw();
    }
}

// ============================================================
// TABS
// ============================================================

void OledUI::tabsCreate(const char** tabs, int tabCount, const int* itemCounts) {
    _tabs = tabs;
    _tabCount = (tabCount > UI_MAX_TABS) ? UI_MAX_TABS : tabCount;
    _tabIdx = 0;

    // Reset all tab items
    for (int i = 0; i < UI_MAX_TABS; i++) {
        _tabItemCounts[i] = 0;
        _tabItemIdx[i] = 0;
        _tabDirty[i] = false;
    }

    // Set item counts if provided
    if (itemCounts) {
        for (int i = 0; i < _tabCount; i++) {
            _tabItemCounts[i] = itemCounts[i];
        }
    }

    // Reset button tracking
    _tabLastDebounce = 0;
    _tabHoldStart = 0;
    _tabHolding = false;
    _comboFirstBtnTime = 0;
    _comboLastSwitch = 0;
    _comboWaiting = false;
    _comboFirstSwitch = true;

    // Header - show initially if AUTO mode
    if (_headerMode == UI_HEADER_AUTO) {
        _headerShowTime = millis();
    }
}

void OledUI::tabsDraw() {
    if (!_disp || !_tabs) return;

    _disp->setTextSize(1);

    if (_headerStyle == UI_HEADER_SINGLE) {
        // SINGLE style: chi hien ten tab hien tai + chi so
        _disp->fillRect(0, 0, UI_SCREEN_W, 10, SH110X_WHITE);
        _disp->setTextColor(SH110X_BLACK, SH110X_WHITE);

        // Ten tab o giua
        int tw = strlen(_tabs[_tabIdx]) * UI_CHAR_W;
        _disp->setCursor((UI_SCREEN_W - tw) / 2, 1);
        _disp->print(_tabs[_tabIdx]);

        // Chi so tab o goc phai (1/3, 2/3, ...)
        char idx[16];
        snprintf(idx, sizeof(idx), "%d/%d", _tabIdx + 1, _tabCount);
        int iw = strlen(idx) * UI_CHAR_W;
        _disp->setCursor(UI_SCREEN_W - iw - 2, 1);
        _disp->print(idx);
    }
    else {
        // TABS style: hien tat ca tabs
        int tabW = UI_SCREEN_W / _tabCount;

        for (int i = 0; i < _tabCount; i++) {
            int x = i * tabW;

            if (i == _tabIdx) {
                _disp->fillRect(x, 0, tabW, 10, SH110X_WHITE);
                _disp->setTextColor(SH110X_BLACK, SH110X_WHITE);
            } else {
                _disp->drawRect(x, 0, tabW, 10, SH110X_WHITE);
                _disp->setTextColor(SH110X_WHITE, SH110X_BLACK);
            }

            int tw = strlen(_tabs[i]) * UI_CHAR_W;
            _disp->setCursor(x + (tabW - tw) / 2, 1);
            _disp->print(_tabs[i]);
        }
    }
}

int OledUI::tabsGetCurrent() {
    return _tabIdx;
}

void OledUI::tabsSetCurrent(int idx) {
    if (idx >= 0 && idx < _tabCount) {
        _tabIdx = idx;
    }
}

int OledUI::tabsGetItemIndex() {
    return _tabItemIdx[_tabIdx];
}

void OledUI::tabsSetItemIndex(int idx) {
    if (idx >= 0 && idx < _tabItemCounts[_tabIdx]) {
        _tabItemIdx[_tabIdx] = idx;
    }
}

int OledUI::tabsGetItemCount() {
    return _tabItemCounts[_tabIdx];
}

void OledUI::tabsNext() {
    _tabIdx++;
    if (_tabIdx >= _tabCount) _tabIdx = 0;
}

void OledUI::tabsPrev() {
    _tabIdx--;
    if (_tabIdx < 0) _tabIdx = _tabCount - 1;
}

int OledUI::tabsUpdate() {
    bool btnUp = _btnRead(_pinUp);
    bool btnDown = _btnRead(_pinDown);
    bool btnOk = _btnRead(_pinOk);
    unsigned long now = millis();

    // ========================================
    // COMBO: UP + DOWN -> switch tab
    // ========================================
    if (btnUp && btnDown) {
        // Phat hien combo lan dau
        if (!_comboWaiting) {
            _comboFirstBtnTime = now;
            _comboWaiting = true;
            _comboFirstSwitch = true;
        }

        unsigned long holdTime = now - _comboFirstBtnTime;

        // Tinh delay: lan dau 0ms, giu > 500ms thi fast switch
        unsigned long switchDelay;
        if (_comboFirstSwitch) {
            switchDelay = 0;  // Chuyen ngay lan dau
        } else if (holdTime > UI_HOLD_MS) {
            switchDelay = UI_FAST_TAB_MS;  // Fast mode
        } else {
            switchDelay = 9999;  // Chua du 500ms, khong switch
        }

        if (now - _comboLastSwitch >= switchDelay) {
            tabsNext();
            _comboLastSwitch = now;
            _comboFirstSwitch = false;
            // Show header khi chuyen tab (AUTO mode)
            if (_headerMode == UI_HEADER_AUTO) {
                _headerShowTime = now;
            }
            return UI_TAB_SWITCHED;
        }
        return UI_NO_ACTION;
    }

    // ========================================
    // CHO COMBO: 1 nut duoc nhan, cho nut thu 2
    // ========================================
    if ((btnUp || btnDown) && !_comboWaiting) {
        _comboFirstBtnTime = now;
        _comboWaiting = true;
    }

    // Trong combo window, cho nut 2
    if (_comboWaiting && (btnUp || btnDown) && !(btnUp && btnDown)) {
        if (now - _comboFirstBtnTime < UI_COMBO_WINDOW_MS) {
            return UI_NO_ACTION;
        } else {
            _comboWaiting = false;
        }
    }

    // Tha het nut -> reset combo
    if (!btnUp && !btnDown) {
        _comboWaiting = false;
        _comboFirstBtnTime = 0;
        _comboFirstSwitch = true;
    }

    // ========================================
    // OK -> Thoat
    // ========================================
    if (btnOk) {
        waitRelease();
        return UI_EXIT;
    }

    // ========================================
    // XU LY NUT DON: UP hoac DOWN
    // ========================================
    bool singleUp = btnUp && !btnDown;
    bool singleDown = btnDown && !btnUp;
    int itemCount = _tabItemCounts[_tabIdx];

    // Khong co items thi khong xu ly
    if (itemCount == 0) {
        return UI_NO_ACTION;
    }

    // Hold detection
    if (singleUp || singleDown) {
        if (!_tabHolding) {
            _tabHoldStart = now;
            _tabHolding = true;
        }
    } else {
        _tabHolding = false;
        _tabHoldStart = 0;
    }

    // Tinh delay: binh thuong 200ms, giu > 500ms thi 80ms
    unsigned long debounceDelay = UI_DEBOUNCE_MS;
    if (_tabHolding && (now - _tabHoldStart > UI_HOLD_MS)) {
        debounceDelay = UI_FAST_SCROLL_MS;
    }

    // Debounce check
    if (now - _tabLastDebounce >= debounceDelay) {
        if (singleUp) {
            _tabItemIdx[_tabIdx]--;
            if (_tabItemIdx[_tabIdx] < 0) {
                _tabItemIdx[_tabIdx] = itemCount - 1;
            }
            _tabLastDebounce = now;
            return UI_ITEM_CHANGED;
        }
        else if (singleDown) {
            _tabItemIdx[_tabIdx]++;
            if (_tabItemIdx[_tabIdx] >= itemCount) {
                _tabItemIdx[_tabIdx] = 0;
            }
            _tabLastDebounce = now;
            return UI_ITEM_CHANGED;
        }
    }

    return UI_NO_ACTION;
}

void OledUI::tabsReset() {
    _comboFirstBtnTime = 0;
    _comboLastSwitch = 0;
    _comboWaiting = false;
    _comboFirstSwitch = true;
    _tabLastDebounce = 0;
    _tabHoldStart = 0;
    _tabHolding = false;
    _headerShowTime = 0;

    for (int i = 0; i < _tabCount; i++) {
        _tabItemIdx[i] = 0;
        _tabDirty[i] = false;
    }
}

void OledUI::tabsSetHeaderMode(uint8_t mode) {
    _headerMode = mode;
    if (mode == UI_HEADER_ALWAYS) {
        _headerShowTime = 0;  // Luon hien
    } else if (mode == UI_HEADER_AUTO) {
        _headerShowTime = millis();  // Bat dau tinh thoi gian
    }
}

void OledUI::tabsSetHeaderStyle(uint8_t style) {
    _headerStyle = style;
}

void OledUI::tabsSetHeaderTimeout(unsigned long ms) {
    _headerTimeout = ms;
}

bool OledUI::tabsIsHeaderVisible() {
    if (_headerMode == UI_HEADER_ALWAYS) {
        return true;
    }
    if (_headerMode == UI_HEADER_NEVER) {
        return false;
    }
    // AUTO mode
    if (_headerShowTime == 0) {
        return false;
    }
    return (millis() - _headerShowTime) < _headerTimeout;
}

int OledUI::tabsGetHeaderHeight() {
    return tabsIsHeaderVisible() ? 10 : 0;
}

void OledUI::tabsSetDrawCallback(TabDrawCallback callback) {
    _tabDrawCallback = callback;
}

int OledUI::tabsRun() {
    if (!_disp) return -1;

    bool needRedraw = true;
    bool lastHeaderVisible = tabsIsHeaderVisible();
    _tabNeedRedraw = false;

    // Clear all dirty flags on start
    tabsClearDirty(-1);

    while (true) {
        // Xu ly nut nhan
        int result = tabsUpdate();

        if (result == UI_TAB_SWITCHED) {
            needRedraw = true;
            // Check if switched tab was dirty
            if (_tabDirty[_tabIdx]) {
                _tabDirty[_tabIdx] = false;
            }
        }
        else if (result == UI_ITEM_CHANGED) {
            needRedraw = true;
        }
        else if (result == UI_EXIT) {
            break;
        }

        // Check header visibility change (AUTO mode)
        bool headerVisible = tabsIsHeaderVisible();
        if (headerVisible != lastHeaderVisible) {
            needRedraw = true;
            lastHeaderVisible = headerVisible;
        }

        // Check force redraw flag
        if (_tabNeedRedraw) {
            needRedraw = true;
            _tabNeedRedraw = false;
        }

        // Check dirty flag of current tab (for real-time data)
        if (_tabDirty[_tabIdx]) {
            needRedraw = true;
            _tabDirty[_tabIdx] = false;
        }

        // Ve giao dien
        if (needRedraw) {
            _disp->clearDisplay();

            // Ve header neu visible
            int headerH = tabsGetHeaderHeight();
            if (headerH > 0) {
                tabsDraw();
            }

            // Goi callback ve content
            if (_tabDrawCallback) {
                int yStart = 4 + headerH;
                _tabDrawCallback(_tabIdx, _tabItemIdx[_tabIdx], yStart);
            }

            _disp->display();
            needRedraw = false;
        }

        delay(10);
    }

    return _tabIdx;
}

void OledUI::tabsMarkDirty(int tab) {
    if (tab == -2) {
        // Current tab
        _tabDirty[_tabIdx] = true;
    }
    else if (tab == -1) {
        // All tabs
        for (int i = 0; i < _tabCount; i++) {
            _tabDirty[i] = true;
        }
    }
    else if (tab >= 0 && tab < _tabCount) {
        // Specific tab
        _tabDirty[tab] = true;
    }
}

bool OledUI::tabsIsDirty(int tab) {
    if (tab == -1) {
        tab = _tabIdx;
    }
    if (tab >= 0 && tab < _tabCount) {
        return _tabDirty[tab];
    }
    return false;
}

void OledUI::tabsClearDirty(int tab) {
    if (tab == -1) {
        // All tabs
        for (int i = 0; i < _tabCount; i++) {
            _tabDirty[i] = false;
        }
    }
    else if (tab >= 0 && tab < _tabCount) {
        _tabDirty[tab] = false;
    }
}

void OledUI::tabsForceRedraw(int tab) {
    if (!_disp) return;

    // -1 = current tab
    if (tab == -1) {
        tab = _tabIdx;
    }

    // Neu tab chi dinh KHONG phai tab hien tai -> chi mark dirty
    if (tab != _tabIdx) {
        if (tab >= 0 && tab < _tabCount) {
            _tabDirty[tab] = true;
        }
        return;
    }

    // Tab hien tai -> ve ngay lap tuc
    _disp->clearDisplay();

    // Ve header neu visible
    int headerH = tabsGetHeaderHeight();
    if (headerH > 0) {
        tabsDraw();
    }

    // Goi callback ve content
    if (_tabDrawCallback) {
        int yStart = 4 + headerH;
        _tabDrawCallback(_tabIdx, _tabItemIdx[_tabIdx], yStart);
    }

    _disp->display();

    // Clear dirty flag
    _tabDirty[_tabIdx] = false;
}

// ============================================================
// MESSAGE
// ============================================================

void OledUI::showMessage(const char* line1, const char* line2) {
    if (!_disp) return;

    _disp->clearDisplay();
    _disp->setTextSize(1);
    _disp->setTextColor(SH110X_WHITE);

    _disp->setCursor(0, 20);
    _disp->print(line1);

    if (line2 && strlen(line2) > 0) {
        _disp->setCursor(0, 32);
        _disp->print(line2);
    }

    _disp->display();
}

void OledUI::showProgress(const char* title, int percent) {
    if (!_disp) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    _disp->clearDisplay();
    _disp->setTextSize(1);
    _disp->setTextColor(SH110X_WHITE);

    // Title
    _disp->setCursor(0, 10);
    _disp->print(title);

    // Bar
    int barX = 10;
    int barY = 30;
    int barW = UI_SCREEN_W - 20;
    int barH = 12;

    _disp->drawRect(barX, barY, barW, barH, SH110X_WHITE);
    int fillW = (barW - 4) * percent / 100;
    _disp->fillRect(barX + 2, barY + 2, fillW, barH - 4, SH110X_WHITE);

    // Percent text
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", percent);
    int tw = strlen(pct) * UI_CHAR_W;
    _disp->setCursor((UI_SCREEN_W - tw) / 2, barY + barH + 6);
    _disp->print(pct);

    _disp->display();
}

void OledUI::showSpinner(const char* title, int frame) {
    if (!_disp) return;

    const char spinner[] = "|/-\\";
    char c = spinner[frame % 4];

    _disp->clearDisplay();
    _disp->setTextSize(1);
    _disp->setTextColor(SH110X_WHITE);

    _disp->setCursor(0, 20);
    _disp->print(title);

    _disp->setTextSize(2);
    _disp->setCursor(UI_SCREEN_W / 2 - 6, 38);
    _disp->print(c);

    _disp->display();
}

// ============================================================
// DIALOG
// ============================================================

bool OledUI::dialogConfirm(const char* title, const char* msg) {
    if (!_disp) return false;

    int selected = 0;  // 0 = Yes, 1 = No

    while (true) {
        _disp->clearDisplay();
        _disp->setTextSize(1);
        _disp->setTextColor(SH110X_WHITE);

        // Title
        _disp->fillRect(0, 0, UI_SCREEN_W, 10, SH110X_WHITE);
        _disp->setTextColor(SH110X_BLACK, SH110X_WHITE);
        int tw = strlen(title) * UI_CHAR_W;
        _disp->setCursor((UI_SCREEN_W - tw) / 2, 1);
        _disp->print(title);

        // Message
        _disp->setTextColor(SH110X_WHITE);
        _disp->setCursor(4, 20);
        _disp->print(msg);

        // Buttons
        int btnW = 50;
        int btnY = 45;

        // Yes
        if (selected == 0) {
            _disp->fillRect(10, btnY, btnW, 14, SH110X_WHITE);
            _disp->setTextColor(SH110X_BLACK, SH110X_WHITE);
        } else {
            _disp->drawRect(10, btnY, btnW, 14, SH110X_WHITE);
            _disp->setTextColor(SH110X_WHITE);
        }
        _disp->setCursor(25, btnY + 3);
        _disp->print("Yes");

        // No
        if (selected == 1) {
            _disp->fillRect(UI_SCREEN_W - 60, btnY, btnW, 14, SH110X_WHITE);
            _disp->setTextColor(SH110X_BLACK, SH110X_WHITE);
        } else {
            _disp->drawRect(UI_SCREEN_W - 60, btnY, btnW, 14, SH110X_WHITE);
            _disp->setTextColor(SH110X_WHITE);
        }
        _disp->setCursor(UI_SCREEN_W - 48, btnY + 3);
        _disp->print("No");

        _disp->display();

        // Input
        delay(150);
        if (_btnRead(_pinUp) || _btnRead(_pinDown)) {
            selected = 1 - selected;
        }
        if (_btnRead(_pinOk)) {
            waitRelease();
            return (selected == 0);
        }
    }
}

void OledUI::dialogAlert(const char* title, const char* msg) {
    if (!_disp) return;

    _disp->clearDisplay();
    _disp->setTextSize(1);
    _disp->setTextColor(SH110X_WHITE);

    // Title
    _disp->fillRect(0, 0, UI_SCREEN_W, 10, SH110X_WHITE);
    _disp->setTextColor(SH110X_BLACK, SH110X_WHITE);
    int tw = strlen(title) * UI_CHAR_W;
    _disp->setCursor((UI_SCREEN_W - tw) / 2, 1);
    _disp->print(title);

    // Message
    _disp->setTextColor(SH110X_WHITE);
    _disp->setCursor(4, 24);
    _disp->print(msg);

    // OK button
    int btnW = 40;
    int btnX = (UI_SCREEN_W - btnW) / 2;
    int btnY = 48;
    _disp->fillRect(btnX, btnY, btnW, 12, SH110X_WHITE);
    _disp->setTextColor(SH110X_BLACK, SH110X_WHITE);
    _disp->setCursor(btnX + 12, btnY + 2);
    _disp->print("OK");

    _disp->display();

    // Wait OK
    while (!_btnRead(_pinOk)) {
        delay(50);
    }
    waitRelease();
}

// ============================================================
// INPUT
// ============================================================

int OledUI::inputNumber(const char* title, int val, int minV, int maxV) {
    if (!_disp) return val;

    while (true) {
        _disp->clearDisplay();
        _disp->setTextSize(1);
        _disp->setTextColor(SH110X_WHITE);

        // Title
        _disp->setCursor(0, 5);
        _disp->print(title);

        // Value
        _disp->setTextSize(3);
        char buf[12];
        snprintf(buf, sizeof(buf), "%d", val);
        int tw = strlen(buf) * 18;
        _disp->setCursor((UI_SCREEN_W - tw) / 2, 25);
        _disp->print(buf);

        // Hint
        _disp->setTextSize(1);
        _disp->setCursor(0, 56);
        _disp->print("UP/DN:+/- OK:Done");

        _disp->display();

        delay(150);
        if (_btnRead(_pinUp)) {
            val++;
            if (val > maxV) val = minV;
        }
        if (_btnRead(_pinDown)) {
            val--;
            if (val < minV) val = maxV;
        }
        if (_btnRead(_pinOk)) {
            waitRelease();
            return val;
        }
    }
}

int OledUI::inputSelect(const char* title, const char** opts, int count, int defIdx) {
    menuCreate(title, opts, count);
    menuSetIndex(defIdx);

    while (true) {
        int sel = menuUpdate();
        if (sel >= 0) {
            return sel;
        }
        delay(10);
    }
}

// ============================================================
// DRAW HELPERS
// ============================================================

void OledUI::drawList(const char** items, int count, int selIdx, int yStart, int maxVisible) {
    if (!_disp || !items || count <= 0) return;

    _disp->setTextSize(1);

    // Tinh top index de scroll
    int top = 0;
    if (selIdx >= maxVisible) {
        top = selIdx - maxVisible + 1;
    }

    for (int i = 0; i < maxVisible && (top + i) < count; i++) {
        int idx = top + i;
        int yPos = yStart + i * 9;

        if (idx == selIdx) {
            _disp->fillRect(0, yPos - 1, UI_SCREEN_W, 9, SH110X_WHITE);
            _disp->setTextColor(SH110X_BLACK, SH110X_WHITE);
        } else {
            _disp->setTextColor(SH110X_WHITE, SH110X_BLACK);
        }

        _disp->setCursor(4, yPos);
        _disp->print(items[idx]);
    }

    // Reset color
    _disp->setTextColor(SH110X_WHITE);
}

void OledUI::drawText(const char* text, int x, int y) {
    if (!_disp || !text) return;

    _disp->setTextSize(1);
    _disp->setTextColor(SH110X_WHITE);
    _disp->setCursor(x, y);
    _disp->print(text);
}

void OledUI::drawTextCenter(const char* text, int y) {
    if (!_disp || !text) return;

    _disp->setTextSize(1);
    _disp->setTextColor(SH110X_WHITE);
    int tw = strlen(text) * UI_CHAR_W;
    _disp->setCursor((UI_SCREEN_W - tw) / 2, y);
    _disp->print(text);
}

void OledUI::drawHint(const char* text) {
    if (!_disp || !text) return;

    _disp->setTextSize(1);
    _disp->setTextColor(SH110X_WHITE);
    _disp->setCursor(4, UI_SCREEN_H - 8);
    _disp->print(text);
}

void OledUI::drawKeyValue(const char* key, const char* value, int y) {
    if (!_disp) return;

    _disp->setTextSize(1);
    _disp->setTextColor(SH110X_WHITE);

    // Key ben trai
    _disp->setCursor(4, y);
    if (key) _disp->print(key);

    // Value ben phai
    if (value) {
        int vw = strlen(value) * UI_CHAR_W;
        _disp->setCursor(UI_SCREEN_W - vw - 4, y);
        _disp->print(value);
    }
}

void OledUI::drawProgressBar(int percent, int y, int height) {
    if (!_disp) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    int barX = 4;
    int barW = UI_SCREEN_W - 8;

    _disp->drawRect(barX, y, barW, height, SH110X_WHITE);
    int fillW = (barW - 4) * percent / 100;
    if (fillW > 0) {
        _disp->fillRect(barX + 2, y + 2, fillW, height - 4, SH110X_WHITE);
    }
}

// ============================================================
// UTILITIES
// ============================================================

void OledUI::clear() {
    if (_disp) {
        _disp->clearDisplay();
        _disp->display();
    }
}

void OledUI::refresh() {
    if (_disp) {
        _disp->display();
    }
}

void OledUI::waitRelease() {
    while (_btnRead(_pinUp) || _btnRead(_pinDown) || _btnRead(_pinOk)) {
        delay(10);
    }
    delay(50);
}

bool OledUI::btnPressed(uint8_t pin) {
    return _btnRead(pin);
}

bool OledUI::_btnRead(uint8_t pin) {
    return (digitalRead(pin) == LOW);
}

unsigned long OledUI::_getDelay() {
    if (_holding && (millis() - _holdStart > UI_HOLD_MS)) {
        return UI_FAST_SCROLL_MS;
    }
    return UI_DEBOUNCE_MS;
}
