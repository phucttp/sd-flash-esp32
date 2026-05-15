"""Tab classes for FlashPorter Public Edition.

Each tab is a self-contained `ttk.Frame` subclass that:
  * receives a reference to MainApp (`app`) for shared state and managers
  * owns its build logic + tab-private helper methods
  * delegates cross-cutting concerns (log, save_settings) to MainApp

Tabs:
  * SettingsTab            — encryption/git/lib paths + account
  * (Phase 2) NetFlashTab  — master-slave NetFlash with mock backend
  * (Phase 3) FirmwareManagerTab — merged Add Firmware + Library & SD Card
"""
