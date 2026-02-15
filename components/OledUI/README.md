# OledUI

A simple UI library for OLED displays (SH1106G/SSD1306) on ESP32/Arduino.

**Author:** TTP27
**Version:** 1.0.0

## Features

- **Menu** - Scrollable menu with selection highlight
- **Tabs** - Multi-tab interface with auto-hide header
- **Dialogs** - Confirm (Yes/No) and Alert dialogs
- **Input** - Number input and list selection
- **Draw Helpers** - Text, KeyValue, List, ProgressBar utilities

## Requirements

```ini
# PlatformIO
lib_deps =
    adafruit/Adafruit GFX Library
    adafruit/Adafruit SH110X
    adafruit/Adafruit BusIO
```

## Quick Start

```cpp
#include "oled_ui.h"

OledUI ui;
Adafruit_SH1106G display(128, 64, &Wire, -1);

void setup() {
    display.begin(0x3C, true);
    ui.begin(&display, PIN_UP, PIN_DOWN, PIN_OK);

    const char* items[] = {"Option 1", "Option 2", "Option 3"};
    ui.menuCreate("MENU", items, 3);
}

void loop() {
    int sel = ui.menuUpdate();
    if (sel >= 0) {
        // Handle selection
    }
}
```

## Documentation

- [Vietnamese Documentation](README_VI.md)

## License

MIT License
