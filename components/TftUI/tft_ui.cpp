/**
 * @file tft_ui.cpp
 * @brief Implementation cua TftUI — port tu OledUI sang ST7735 TFT 128x160
 *
 * OPTIMIZATION: Dung ky thuat "overdraw" thay vi fillScreen()
 *   - Moi element tu ve background cua minh → khong nhay den
 *   - Menu scroll chi ve 2 item thay doi → phan hoi tuc thi
 *   - showProgress/showSpinner chi ve phan thay doi
 */

#include "tft_ui.h"

// ============================================================
// CONSTRUCTOR
// ============================================================

TftUI::TftUI() {
    _disp = nullptr;
    _pinUp = _pinDown = _pinOk = 0;
    _pinBL = -1;
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
    _comboFirstBtnTime = 0;
    _comboLastSwitch = 0;
    _comboWaiting = false;
    _comboFirstSwitch = true;
    _tabLastDebounce = 0;
    _tabHoldStart = 0;
    _tabHolding = false;
    for (int i = 0; i < UI_MAX_TABS; i++) {
        _tabItemCounts[i] = 0;
        _tabItemIdx[i] = 0;
        _tabDirty[i] = false;
    }
    _headerMode = UI_HEADER_ALWAYS;
    _headerStyle = UI_HEADER_TABS;
    _headerTimeout = UI_HEADER_TIMEOUT;
    _headerShowTime = 0;
    _tabDrawCallback = nullptr;
    _tabNeedRedraw = false;
    _screenMode = 0;
    _listScrollActive = false;
}

// ============================================================
// INIT
// ============================================================

void TftUI::begin(Adafruit_ST7735* disp, uint8_t pinUp, uint8_t pinDown, uint8_t pinOk, int8_t pinBL) {
    _disp = disp;
    _pinUp = pinUp;
    _pinDown = pinDown;
    _pinOk = pinOk;
    _pinBL = pinBL;

    pinMode(_pinUp, INPUT_PULLUP);
    pinMode(_pinDown, INPUT_PULLUP);
    pinMode(_pinOk, INPUT_PULLUP);

    if (_pinBL >= 0) {
        pinMode(_pinBL, OUTPUT);
        backlightOn();
    }
}

void TftUI::backlightOn() {
    if (_pinBL >= 0) digitalWrite(_pinBL, HIGH);
}

void TftUI::backlightOff() {
    if (_pinBL >= 0) digitalWrite(_pinBL, LOW);
}

// ============================================================
// INTERNAL HELPERS
// ============================================================

// Fill tu yStart den cuoi man hinh
void TftUI::_fillContentArea(int yStart) {
    if (yStart < UI_SCREEN_H) {
        _disp->fillRect(0, yStart, UI_SCREEN_W, UI_SCREEN_H - yStart, UI_COL_BG);
    }
}

// Ve 1 item menu tai vi tri cua no (partial redraw)
void TftUI::_menuDrawItem(int idx) {
    if (!_disp || idx < 0 || idx >= _menuCount) return;

    int maxLines = _menuTitle ? UI_MAX_VISIBLE : UI_MAX_VISIBLE_NH;
    int visibleIdx = idx - _menuTop;
    if (visibleIdx < 0 || visibleIdx >= maxLines) return;

    int yStart = _menuTitle ? (UI_HEADER_H + 2) : 0;
    int yPos = yStart + visibleIdx * UI_LINE_H;

    _disp->setTextSize(1);

    if (idx == _menuIdx) {
        _disp->fillRect(0, yPos, UI_SCREEN_W, UI_LINE_H, UI_COL_SEL_BG);
        _disp->setTextColor(UI_COL_SEL_FG);
    } else {
        _disp->fillRect(0, yPos, UI_SCREEN_W, UI_LINE_H, UI_COL_BG);
        _disp->setTextColor(UI_COL_TEXT);
    }

    _disp->setCursor(4, yPos + 2);
    _disp->print(_menuItems[idx]);
}

// Ve position indicator (x/y) o goc duoi phai
void TftUI::_menuDrawIndicator() {
    if (!_disp) return;
    int maxLines = _menuTitle ? UI_MAX_VISIBLE : UI_MAX_VISIBLE_NH;

    // Xoa vung indicator
    _disp->fillRect(0, UI_SCREEN_H - 14, UI_SCREEN_W, 14, UI_COL_BG);

    if (_menuCount > maxLines) {
        char pos[24];
        snprintf(pos, sizeof(pos), "%d/%d", _menuIdx + 1, _menuCount);
        int tw = strlen(pos) * UI_CHAR_W;
        _disp->setTextSize(1);
        _disp->setTextColor(UI_COL_HINT);
        _disp->setCursor(UI_SCREEN_W - tw - 2, UI_SCREEN_H - 10);
        _disp->print(pos);
    }
}

// ============================================================
// MENU — OVERDRAW (khong fillScreen)
// ============================================================

void TftUI::menuCreate(const char* title, const char** items, int count) {
    _menuTitle = title;
    _menuItems = items;
    _menuCount = (count > UI_MAX_ITEMS) ? UI_MAX_ITEMS : count;
    _menuIdx = 0;
    _menuTop = 0;
    _menuDraw();
}

void TftUI::_menuDraw() {
    if (!_disp) return;

    // KHONG fillScreen — moi element tu ve background
    _disp->setTextSize(1);

    int yStart = 0;
    int maxLines = UI_MAX_VISIBLE_NH;

    // Header — overdraw
    if (_menuTitle) {
        _disp->fillRect(0, 0, UI_SCREEN_W, UI_HEADER_H, UI_COL_HEADER_BG);
        _disp->setTextColor(UI_COL_HEADER_FG);
        int tw = strlen(_menuTitle) * UI_CHAR_W;
        _disp->setCursor((UI_SCREEN_W - tw) / 2, 3);
        _disp->print(_menuTitle);
        yStart = UI_HEADER_H + 2;
        maxLines = UI_MAX_VISIBLE;
    }

    // Items — moi item ve background rieng (selected hoac normal)
    for (int i = 0; i < maxLines; i++) {
        int idx = _menuTop + i;
        int yPos = yStart + i * UI_LINE_H;

        if (idx < _menuCount) {
            if (idx == _menuIdx) {
                _disp->fillRect(0, yPos, UI_SCREEN_W, UI_LINE_H, UI_COL_SEL_BG);
                _disp->setTextColor(UI_COL_SEL_FG);
            } else {
                _disp->fillRect(0, yPos, UI_SCREEN_W, UI_LINE_H, UI_COL_BG);
                _disp->setTextColor(UI_COL_TEXT);
            }
            _disp->setCursor(4, yPos + 2);
            _disp->print(_menuItems[idx]);
        } else {
            // Empty slot — fill background
            _disp->fillRect(0, yPos, UI_SCREEN_W, UI_LINE_H, UI_COL_BG);
        }
    }

    // Clear vung con lai phia duoi
    int bottomY = yStart + maxLines * UI_LINE_H;
    _fillContentArea(bottomY);

    // Position indicator
    _menuDrawIndicator();
}

int TftUI::menuUpdate() {
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

    int maxLines = _menuTitle ? UI_MAX_VISIBLE : UI_MAX_VISIBLE_NH;
    int oldIdx = _menuIdx;
    int oldTop = _menuTop;
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
        if (_menuTop == oldTop) {
            // View khong scroll — chi ve lai 2 item thay doi
            _menuDrawItem(oldIdx);      // bo highlight cu
            _menuDrawItem(_menuIdx);    // highlight moi
            _menuDrawIndicator();       // cap nhat vi tri
        } else {
            // View scroll — ve lai toan bo (nhung van khong fillScreen)
            _menuDraw();
        }
        _lastDebounce = millis();
    }

    return UI_NO_ACTION;
}

void TftUI::menuRedraw() {
    _menuDraw();
}

int TftUI::menuGetIndex() {
    return _menuIdx;
}

void TftUI::menuSetIndex(int idx) {
    if (idx >= 0 && idx < _menuCount) {
        _menuIdx = idx;
        int maxLines = _menuTitle ? UI_MAX_VISIBLE : UI_MAX_VISIBLE_NH;
        if (_menuIdx < _menuTop) _menuTop = _menuIdx;
        if (_menuIdx >= _menuTop + maxLines) _menuTop = _menuIdx - maxLines + 1;
        _menuDraw();
    }
}

// ============================================================
// TABS
// ============================================================

void TftUI::tabsCreate(const char** tabs, int tabCount, const int* itemCounts) {
    _tabs = tabs;
    _tabCount = (tabCount > UI_MAX_TABS) ? UI_MAX_TABS : tabCount;
    _tabIdx = 0;

    for (int i = 0; i < UI_MAX_TABS; i++) {
        _tabItemCounts[i] = 0;
        _tabItemIdx[i] = 0;
        _tabDirty[i] = false;
    }

    if (itemCounts) {
        for (int i = 0; i < _tabCount; i++) {
            _tabItemCounts[i] = itemCounts[i];
        }
    }

    _tabLastDebounce = 0;
    _tabHoldStart = 0;
    _tabHolding = false;
    _comboFirstBtnTime = 0;
    _comboLastSwitch = 0;
    _comboWaiting = false;
    _comboFirstSwitch = true;

    if (_headerMode == UI_HEADER_AUTO) {
        _headerShowTime = millis();
    }
}

void TftUI::tabsDraw() {
    if (!_disp || !_tabs) return;

    _disp->setTextSize(1);

    if (_headerStyle == UI_HEADER_SINGLE) {
        _disp->fillRect(0, 0, UI_SCREEN_W, UI_HEADER_H, UI_COL_HEADER_BG);
        _disp->setTextColor(UI_COL_HEADER_FG);

        int tw = strlen(_tabs[_tabIdx]) * UI_CHAR_W;
        _disp->setCursor((UI_SCREEN_W - tw) / 2, 3);
        _disp->print(_tabs[_tabIdx]);

        char idx[16];
        snprintf(idx, sizeof(idx), "%d/%d", _tabIdx + 1, _tabCount);
        int iw = strlen(idx) * UI_CHAR_W;
        _disp->setCursor(UI_SCREEN_W - iw - 2, 3);
        _disp->print(idx);
    }
    else {
        int tabW = UI_SCREEN_W / _tabCount;

        for (int i = 0; i < _tabCount; i++) {
            int x = i * tabW;

            if (i == _tabIdx) {
                _disp->fillRect(x, 0, tabW, UI_HEADER_H, UI_COL_HEADER_BG);
                _disp->setTextColor(UI_COL_HEADER_FG);
            } else {
                _disp->fillRect(x, 0, tabW, UI_HEADER_H, UI_COL_BG);
                _disp->drawRect(x, 0, tabW, UI_HEADER_H, UI_COL_HINT);
                _disp->setTextColor(UI_COL_HINT);
            }

            int tw = strlen(_tabs[i]) * UI_CHAR_W;
            _disp->setCursor(x + (tabW - tw) / 2, 3);
            _disp->print(_tabs[i]);
        }
    }
}

int TftUI::tabsGetCurrent() { return _tabIdx; }

void TftUI::tabsSetCurrent(int idx) {
    if (idx >= 0 && idx < _tabCount) _tabIdx = idx;
}

int TftUI::tabsGetItemIndex() { return _tabItemIdx[_tabIdx]; }

void TftUI::tabsSetItemIndex(int idx) {
    if (idx >= 0 && idx < _tabItemCounts[_tabIdx]) _tabItemIdx[_tabIdx] = idx;
}

int TftUI::tabsGetItemCount() { return _tabItemCounts[_tabIdx]; }

void TftUI::tabsNext() {
    _tabIdx++;
    if (_tabIdx >= _tabCount) _tabIdx = 0;
}

void TftUI::tabsPrev() {
    _tabIdx--;
    if (_tabIdx < 0) _tabIdx = _tabCount - 1;
}

int TftUI::tabsUpdate() {
    bool btnUp = _btnRead(_pinUp);
    bool btnDown = _btnRead(_pinDown);
    bool btnOk = _btnRead(_pinOk);
    unsigned long now = millis();

    // COMBO: UP + DOWN -> switch tab
    if (btnUp && btnDown) {
        if (!_comboWaiting) {
            _comboFirstBtnTime = now;
            _comboWaiting = true;
            _comboFirstSwitch = true;
        }

        unsigned long holdTime = now - _comboFirstBtnTime;
        unsigned long switchDelay;
        if (_comboFirstSwitch) {
            switchDelay = 0;
        } else if (holdTime > UI_HOLD_MS) {
            switchDelay = UI_FAST_TAB_MS;
        } else {
            switchDelay = 9999;
        }

        if (now - _comboLastSwitch >= switchDelay) {
            tabsNext();
            _comboLastSwitch = now;
            _comboFirstSwitch = false;
            if (_headerMode == UI_HEADER_AUTO) {
                _headerShowTime = now;
            }
            return UI_TAB_SWITCHED;
        }
        return UI_NO_ACTION;
    }

    // CHO COMBO
    if ((btnUp || btnDown) && !_comboWaiting) {
        _comboFirstBtnTime = now;
        _comboWaiting = true;
    }

    if (_comboWaiting && (btnUp || btnDown) && !(btnUp && btnDown)) {
        if (now - _comboFirstBtnTime < UI_COMBO_WINDOW_MS) {
            return UI_NO_ACTION;
        } else {
            _comboWaiting = false;
        }
    }

    if (!btnUp && !btnDown) {
        _comboWaiting = false;
        _comboFirstBtnTime = 0;
        _comboFirstSwitch = true;
    }

    // OK -> Thoat
    if (btnOk) {
        waitRelease();
        return UI_EXIT;
    }

    // NUT DON: UP hoac DOWN
    bool singleUp = btnUp && !btnDown;
    bool singleDown = btnDown && !btnUp;
    int itemCount = _tabItemCounts[_tabIdx];

    if (itemCount == 0) return UI_NO_ACTION;

    if (singleUp || singleDown) {
        if (!_tabHolding) {
            _tabHoldStart = now;
            _tabHolding = true;
        }
    } else {
        _tabHolding = false;
        _tabHoldStart = 0;
    }

    unsigned long debounceDelay = UI_DEBOUNCE_MS;
    if (_tabHolding && (now - _tabHoldStart > UI_HOLD_MS)) {
        debounceDelay = UI_FAST_SCROLL_MS;
    }

    if (now - _tabLastDebounce >= debounceDelay) {
        if (singleUp) {
            _tabItemIdx[_tabIdx]--;
            if (_tabItemIdx[_tabIdx] < 0) _tabItemIdx[_tabIdx] = itemCount - 1;
            _tabLastDebounce = now;
            return UI_ITEM_CHANGED;
        }
        else if (singleDown) {
            _tabItemIdx[_tabIdx]++;
            if (_tabItemIdx[_tabIdx] >= itemCount) _tabItemIdx[_tabIdx] = 0;
            _tabLastDebounce = now;
            return UI_ITEM_CHANGED;
        }
    }

    return UI_NO_ACTION;
}

void TftUI::tabsReset() {
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

void TftUI::tabsSetHeaderMode(uint8_t mode) {
    _headerMode = mode;
    if (mode == UI_HEADER_ALWAYS) _headerShowTime = 0;
    else if (mode == UI_HEADER_AUTO) _headerShowTime = millis();
}

void TftUI::tabsSetHeaderStyle(uint8_t style) { _headerStyle = style; }
void TftUI::tabsSetHeaderTimeout(unsigned long ms) { _headerTimeout = ms; }

bool TftUI::tabsIsHeaderVisible() {
    if (_headerMode == UI_HEADER_ALWAYS) return true;
    if (_headerMode == UI_HEADER_NEVER) return false;
    if (_headerShowTime == 0) return false;
    return (millis() - _headerShowTime) < _headerTimeout;
}

int TftUI::tabsGetHeaderHeight() {
    return tabsIsHeaderVisible() ? UI_HEADER_H : 0;
}

void TftUI::tabsSetDrawCallback(TabDrawCallback callback) {
    _tabDrawCallback = callback;
}

int TftUI::tabsRun() {
    if (!_disp) return -1;

    _screenMode = 5;
    bool needRedraw = true;
    bool lastHeaderVisible = tabsIsHeaderVisible();
    _tabNeedRedraw = false;
    tabsClearDirty(-1);

    while (true) {
        int result = tabsUpdate();

        if (result == UI_TAB_SWITCHED) {
            needRedraw = true;
            _tabDirty[_tabIdx] = false;
        }
        else if (result == UI_ITEM_CHANGED) {
            needRedraw = true;
        }
        else if (result == UI_EXIT) {
            break;
        }

        bool headerVisible = tabsIsHeaderVisible();
        if (headerVisible != lastHeaderVisible) {
            needRedraw = true;
            lastHeaderVisible = headerVisible;
        }

        if (_tabNeedRedraw) {
            needRedraw = true;
            _tabNeedRedraw = false;
        }

        if (_tabDirty[_tabIdx]) {
            needRedraw = true;
            _tabDirty[_tabIdx] = false;
        }

        // Auto-scroll: periodic redraw for long selected item text
        if (!needRedraw && _listScrollActive) {
            static unsigned long lastScrollRedraw = 0;
            unsigned long now = millis();
            if (now - lastScrollRedraw >= 300) {
                needRedraw = true;
                lastScrollRedraw = now;
            }
        }

        if (needRedraw) {
            // Header — overdraw (khong can clear rieng)
            int headerH = tabsGetHeaderHeight();
            if (headerH > 0) {
                tabsDraw();
            } else {
                // An header — clear vung header
                _disp->fillRect(0, 0, UI_SCREEN_W, UI_HEADER_H, UI_COL_BG);
            }

            // Content area — clear roi goi callback
            int yStart = (headerH > 0) ? (headerH + 4) : 4;
            _fillContentArea(headerH);

            if (_tabDrawCallback) {
                _tabDrawCallback(_tabIdx, _tabItemIdx[_tabIdx], yStart);
            }

            needRedraw = false;
        }

        delay(10);
    }

    return _tabIdx;
}

void TftUI::tabsMarkDirty(int tab) {
    if (tab == -2) _tabDirty[_tabIdx] = true;
    else if (tab == -1) {
        for (int i = 0; i < _tabCount; i++) _tabDirty[i] = true;
    }
    else if (tab >= 0 && tab < _tabCount) _tabDirty[tab] = true;
}

bool TftUI::tabsIsDirty(int tab) {
    if (tab == -1) tab = _tabIdx;
    if (tab >= 0 && tab < _tabCount) return _tabDirty[tab];
    return false;
}

void TftUI::tabsClearDirty(int tab) {
    if (tab == -1) {
        for (int i = 0; i < _tabCount; i++) _tabDirty[i] = false;
    }
    else if (tab >= 0 && tab < _tabCount) _tabDirty[tab] = false;
}

void TftUI::tabsForceRedraw(int tab) {
    if (!_disp) return;
    if (tab == -1) tab = _tabIdx;

    if (tab != _tabIdx) {
        if (tab >= 0 && tab < _tabCount) _tabDirty[tab] = true;
        return;
    }

    // Tab hien tai -> ve ngay (overdraw, khong fillScreen)
    int headerH = tabsGetHeaderHeight();
    if (headerH > 0) {
        tabsDraw();
    }

    _fillContentArea(headerH);

    if (_tabDrawCallback) {
        int yStart = (headerH > 0) ? (headerH + 4) : 4;
        _tabDrawCallback(_tabIdx, _tabItemIdx[_tabIdx], yStart);
    }

    _tabDirty[_tabIdx] = false;
}

// ============================================================
// MESSAGE — OVERDRAW
// ============================================================

void TftUI::showMessage(const char* line1, const char* line2) {
    if (!_disp) return;

    _disp->fillScreen(UI_COL_BG);
    _screenMode = 1;
    _disp->setTextSize(1);
    _disp->setTextColor(UI_COL_TEXT);

    if (line2 && strlen(line2) > 0) {
        int tw1 = strlen(line1) * UI_CHAR_W;
        _disp->setCursor((UI_SCREEN_W - tw1) / 2, 48);
        _disp->print(line1);

        int tw2 = strlen(line2) * UI_CHAR_W;
        _disp->setCursor((UI_SCREEN_W - tw2) / 2, 68);
        _disp->print(line2);
    } else {
        int tw = strlen(line1) * UI_CHAR_W;
        _disp->setCursor((UI_SCREEN_W - tw) / 2, 56);
        _disp->print(line1);
    }
}

void TftUI::showProgress(const char* title, int percent) {
    if (!_disp) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    // Clear full screen on first call after dialog/message/spinner
    if (_screenMode != 2) {
        _disp->fillScreen(UI_COL_BG);
        _screenMode = 2;
    }

    int barX = 10;
    int barY = 48;
    int barW = UI_SCREEN_W - 20;
    int barH = 16;
    int innerX = barX + 2;
    int innerY = barY + 2;
    int innerW = barW - 4;
    int innerH = barH - 4;

    _disp->setTextSize(1);

    // Title — chi clear dong title (khong clear toan bo area)
    _disp->fillRect(0, 18, UI_SCREEN_W, 14, UI_COL_BG);
    _disp->setTextColor(UI_COL_TEXT);
    int tw = strlen(title) * UI_CHAR_W;
    _disp->setCursor((UI_SCREEN_W - tw) / 2, 22);
    _disp->print(title);

    // Bar outline
    _disp->drawRect(barX, barY, barW, barH, UI_COL_BAR_FRAME);

    // Bar interior — fill green + black 1 pass (KHONG clear truoc)
    int fillW = innerW * percent / 100;
    if (fillW > 0) {
        _disp->fillRect(innerX, innerY, fillW, innerH, UI_COL_BAR_FILL);
    }
    int remainW = innerW - fillW;
    if (remainW > 0) {
        _disp->fillRect(innerX + fillW, innerY, remainW, innerH, UI_COL_BG);
    }

    // Percent text — chi clear vung text nho
    int pctY = barY + barH + 8;
    _disp->fillRect(UI_SCREEN_W / 2 - 18, pctY - 1, 36, 12, UI_COL_BG);
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", percent);
    int ptw = strlen(pct) * UI_CHAR_W;
    _disp->setTextColor(UI_COL_TEXT);
    _disp->setCursor((UI_SCREEN_W - ptw) / 2, pctY);
    _disp->print(pct);
}

void TftUI::showSpinner(const char* title, int frame) {
    if (!_disp) return;

    // Clear full screen on first call after dialog/message/progress
    if (_screenMode != 3) {
        _disp->fillScreen(UI_COL_BG);
        _screenMode = 3;
    }

    const char spinner[] = "|/-\\";
    char c = spinner[frame % 4];

    _disp->setTextSize(1);

    // Title area
    _disp->fillRect(0, 0, UI_SCREEN_W, 55, UI_COL_BG);
    _disp->setTextColor(UI_COL_TEXT);
    int tw = strlen(title) * UI_CHAR_W;
    _disp->setCursor((UI_SCREEN_W - tw) / 2, 35);
    _disp->print(title);

    // Spinner area (size 3 = 24px high, ~18px wide)
    _disp->fillRect(UI_SCREEN_W / 2 - 12, 60, 24, 28, UI_COL_BG);
    _disp->setTextSize(3);
    _disp->setCursor(UI_SCREEN_W / 2 - 9, 63);
    _disp->print(c);

    // Clear below spinner (chi lan dau)
    _fillContentArea(90);
}

// ============================================================
// DIALOG — OVERDRAW
// ============================================================

bool TftUI::dialogConfirm(const char* title, const char* msg) {
    if (!_disp) return false;

    int selected = 0;
    int btnW = 50;
    int btnY = 95;
    int btnH = 20;

    _disp->fillScreen(UI_COL_BG);
    _screenMode = 4;
    _disp->setTextSize(1);

    _disp->fillRect(0, 0, UI_SCREEN_W, UI_HEADER_H, UI_COL_HEADER_BG);
    _disp->setTextColor(UI_COL_HEADER_FG);
    int tw = strlen(title) * UI_CHAR_W;
    _disp->setCursor((UI_SCREEN_W - tw) / 2, 3);
    _disp->print(title);

    _disp->setTextColor(UI_COL_TEXT);
    _disp->setCursor(4, 40);
    _disp->print(msg);

    while (true) {
        // Chi ve lai buttons (phan thay doi)
        _disp->setTextSize(1);

        // Yes button
        if (selected == 0) {
            _disp->fillRect(10, btnY, btnW, btnH, UI_COL_BTN_SEL_BG);
            _disp->setTextColor(UI_COL_BTN_SEL_FG);
        } else {
            _disp->fillRect(10, btnY, btnW, btnH, UI_COL_BG);
            _disp->drawRect(10, btnY, btnW, btnH, UI_COL_HINT);
            _disp->setTextColor(UI_COL_HINT);
        }
        _disp->setCursor(24, btnY + 6);
        _disp->print("Yes");

        // No button
        if (selected == 1) {
            _disp->fillRect(UI_SCREEN_W - 60, btnY, btnW, btnH, UI_COL_BTN_SEL_BG);
            _disp->setTextColor(UI_COL_BTN_SEL_FG);
        } else {
            _disp->fillRect(UI_SCREEN_W - 60, btnY, btnW, btnH, UI_COL_BG);
            _disp->drawRect(UI_SCREEN_W - 60, btnY, btnW, btnH, UI_COL_HINT);
            _disp->setTextColor(UI_COL_HINT);
        }
        _disp->setCursor(UI_SCREEN_W - 47, btnY + 6);
        _disp->print("No");

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

void TftUI::dialogAlert(const char* title, const char* msg) {
    if (!_disp) return;

    _disp->fillScreen(UI_COL_BG);
    _screenMode = 4;
    _disp->setTextSize(1);

    _disp->fillRect(0, 0, UI_SCREEN_W, UI_HEADER_H, UI_COL_HEADER_BG);
    _disp->setTextColor(UI_COL_HEADER_FG);
    int tw = strlen(title) * UI_CHAR_W;
    _disp->setCursor((UI_SCREEN_W - tw) / 2, 3);
    _disp->print(title);

    _disp->setTextColor(UI_COL_TEXT);
    _disp->setCursor(4, 45);
    _disp->print(msg);

    int btnW = 50;
    int btnX = (UI_SCREEN_W - btnW) / 2;
    int btnY = 95;
    int btnH = 20;
    _disp->fillRect(btnX, btnY, btnW, btnH, UI_COL_BTN_SEL_BG);
    _disp->setTextColor(UI_COL_BTN_SEL_FG);
    _disp->setCursor(btnX + 17, btnY + 6);
    _disp->print("OK");

    while (!_btnRead(_pinOk)) delay(50);
    waitRelease();
}

// ============================================================
// INPUT — OVERDRAW
// ============================================================

int TftUI::inputNumber(const char* title, int val, int minV, int maxV) {
    if (!_disp) return val;

    // Ve 1 lan: title + range hint + bottom hint
    _disp->fillScreen(UI_COL_BG);
    _disp->setTextSize(1);
    _disp->setTextColor(UI_COL_TEXT);

    int ttw = strlen(title) * UI_CHAR_W;
    _disp->setCursor((UI_SCREEN_W - ttw) / 2, 12);
    _disp->print(title);

    _disp->setTextColor(UI_COL_HINT);
    char range[32];
    snprintf(range, sizeof(range), "[%d ~ %d]", minV, maxV);
    int rw = strlen(range) * UI_CHAR_W;
    _disp->setCursor((UI_SCREEN_W - rw) / 2, 80);
    _disp->print(range);

    _disp->setCursor(8, UI_SCREEN_H - 12);
    _disp->print("UP/DN:+/- OK:Done");

    while (true) {
        // Chi ve lai value (phan thay doi)
        _disp->fillRect(0, 35, UI_SCREEN_W, 35, UI_COL_BG);
        _disp->setTextSize(3);
        _disp->setTextColor(UI_COL_TEXT);
        char buf[12];
        snprintf(buf, sizeof(buf), "%d", val);
        int vw = strlen(buf) * 18;
        _disp->setCursor((UI_SCREEN_W - vw) / 2, 42);
        _disp->print(buf);

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

int TftUI::inputSelect(const char* title, const char** opts, int count, int defIdx) {
    menuCreate(title, opts, count);
    menuSetIndex(defIdx);

    while (true) {
        int sel = menuUpdate();
        if (sel >= 0) return sel;
        delay(10);
    }
}

// ============================================================
// DRAW HELPERS — OVERDRAW
// ============================================================

void TftUI::drawList(const char** items, int count, int selIdx, int yStart, int maxVisible) {
    if (!_disp || !items || count <= 0) return;

    _disp->setTextSize(1);
    _listScrollActive = false;

    int top = 0;
    if (selIdx >= maxVisible) {
        top = selIdx - maxVisible + 1;
    }

    int maxChars = (UI_SCREEN_W - 4) / UI_CHAR_W;  // ~25 chars visible

    for (int i = 0; i < maxVisible; i++) {
        int idx = top + i;
        int yPos = yStart + i * UI_LINE_H;

        if (idx < count) {
            bool selected = (idx == selIdx);
            if (selected) {
                _disp->fillRect(0, yPos, UI_SCREEN_W, UI_LINE_H, UI_COL_SEL_BG);
                _disp->setTextColor(UI_COL_SEL_FG);
            } else {
                _disp->fillRect(0, yPos, UI_SCREEN_W, UI_LINE_H, UI_COL_BG);
                _disp->setTextColor(UI_COL_TEXT);
            }

            int textLen = (int)strlen(items[idx]);
            _disp->setCursor(4, yPos + 2);

            if (selected && textLen > maxChars) {
                // Auto-scroll: pause at start/end, then scroll through
                _listScrollActive = true;
                int scrollRange = textLen - maxChars;
                int pauseSteps = 5;  // ~1.5s pause at each end
                int totalSteps = scrollRange + 2 * pauseSteps;
                int step = ((int)(millis() / 300)) % totalSteps;
                int offset;
                if (step < pauseSteps) offset = 0;
                else if (step < pauseSteps + scrollRange) offset = step - pauseSteps;
                else offset = scrollRange;

                char buf[32];
                snprintf(buf, maxChars + 1, "%s", items[idx] + offset);
                _disp->print(buf);
            } else {
                _disp->print(items[idx]);
            }
        } else {
            // Empty slot — fill de xoa content cu
            _disp->fillRect(0, yPos, UI_SCREEN_W, UI_LINE_H, UI_COL_BG);
        }
    }

    _disp->setTextColor(UI_COL_TEXT);
}

void TftUI::drawText(const char* text, int x, int y) {
    if (!_disp || !text) return;
    _disp->setTextSize(1);
    _disp->setTextColor(UI_COL_TEXT);
    _disp->setCursor(x, y);
    _disp->print(text);
}

void TftUI::drawTextCenter(const char* text, int y) {
    if (!_disp || !text) return;
    _disp->setTextSize(1);
    _disp->setTextColor(UI_COL_TEXT);
    int tw = strlen(text) * UI_CHAR_W;
    _disp->setCursor((UI_SCREEN_W - tw) / 2, y);
    _disp->print(text);
}

void TftUI::drawHint(const char* text) {
    if (!_disp || !text) return;
    // Clear hint line roi ve
    _disp->fillRect(0, UI_SCREEN_H - 12, UI_SCREEN_W, 12, UI_COL_BG);
    _disp->setTextSize(1);
    _disp->setTextColor(UI_COL_HINT);
    _disp->setCursor(4, UI_SCREEN_H - 10);
    _disp->print(text);
}

void TftUI::drawKeyValue(const char* key, const char* value, int y) {
    if (!_disp) return;
    // Clear dong roi ve
    _disp->fillRect(0, y, UI_SCREEN_W, UI_CHAR_H + 2, UI_COL_BG);
    _disp->setTextSize(1);
    _disp->setTextColor(UI_COL_TEXT);

    _disp->setCursor(4, y);
    if (key) _disp->print(key);

    if (value) {
        int vw = strlen(value) * UI_CHAR_W;
        _disp->setCursor(UI_SCREEN_W - vw - 4, y);
        _disp->print(value);
    }
}

void TftUI::drawProgressBar(int percent, int y, int height) {
    if (!_disp) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    int barX = 4;
    int barW = UI_SCREEN_W - 8;

    _disp->drawRect(barX, y, barW, height, UI_COL_BAR_FRAME);
    // Clear interior roi fill
    _disp->fillRect(barX + 1, y + 1, barW - 2, height - 2, UI_COL_BG);
    int fillW = (barW - 4) * percent / 100;
    if (fillW > 0) {
        _disp->fillRect(barX + 2, y + 2, fillW, height - 4, UI_COL_BAR_FILL);
    }
}

// ============================================================
// UTILITIES
// ============================================================

void TftUI::clear() {
    if (_disp) _disp->fillScreen(UI_COL_BG);
}

void TftUI::refresh() {
    // TFT ghi truc tiep — khong can flush
}

void TftUI::waitRelease() {
    while (_btnRead(_pinUp) || _btnRead(_pinDown) || _btnRead(_pinOk)) {
        delay(10);
    }
    delay(50);
}

bool TftUI::btnPressed(uint8_t pin) { return _btnRead(pin); }

bool TftUI::_btnRead(uint8_t pin) {
    return (digitalRead(pin) == LOW);
}

unsigned long TftUI::_getDelay() {
    if (_holding && (millis() - _holdStart > UI_HOLD_MS)) {
        return UI_FAST_SCROLL_MS;
    }
    return UI_DEBOUNCE_MS;
}
