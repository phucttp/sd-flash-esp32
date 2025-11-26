# Project Roadmap - ESP32 Multi-Flasher

## Current Status

**Version:** 1.0.0
**Status:** Production-ready MVP
**Last Updated:** 2025-11-27

---

## Implemented Features (v1.0.0)

### Core Functionality ✅
- [x] Offline firmware flashing from SD card
- [x] OLED menu interface with 3-button navigation
- [x] JSON-based firmware catalog (index.txt)
- [x] Multi-segment flash (bootloader + partition + app)
- [x] MD5 verification post-flash
- [x] Target boot control (EN/BOOT GPIO)
- [x] System restart after operations (memory management)

### Online Sync ✅
- [x] WiFi connection via WiFiManager
- [x] Captive portal for WiFi/server configuration
- [x] Remote firmware download (HTTP/HTTPS)
- [x] AES-128-CBC encrypted firmware decryption
- [x] Smart sync (compare local vs remote index)
- [x] Force clean mode (long-press to delete all local)
- [x] Configuration files on SD (/config/*.txt)

### Advanced Features ✅
- [x] UART monitor mode (view Target logs)
- [x] Chip erase command
- [x] Progress feedback on OLED
- [x] ESP-IDF logging system integration
- [x] Arduino framework compatibility
- [x] FAT32 SD card support (8.3 filename format)

### Hardware Support ✅
- [x] ESP32-C3 primary target
- [x] SSD1306 OLED (128x32, I2C)
- [x] SD card (SPI mode)
- [x] Button debouncing with long-press detection

---

## Known Limitations

### Hardware Constraints
- **RAM:** Limited by ESP32-C3 (~400KB total, ~200KB usable)
- **SD Card:** FAT32 only, 8.3 filename format mandatory
- **WiFi:** 2.4GHz only (no 5GHz support)
- **Display:** 128x32 pixels limits visible menu items (~2 at a time)

### Software Limitations
- **No Multi-threading:** Single-threaded Arduino loop, all operations blocking
- **No Persistent State:** System restarts after each operation
- **No Error Recovery:** Flash interruption requires manual restart
- **No Logging to SD:** All logs via UART only (volatile)
- **No Secure Boot:** Host firmware unencrypted on flash
- **AES Keys in Plaintext:** Stored on SD card (physical access risk)

### FlashPorter Tool Status
- **Status:** Mentioned in README, implementation details unknown
- **Alternative:** Manual SD card preparation via JSON editing

---

## Roadmap

### v1.1 - Stability & UX (Short-term)

**Priority:** High
**Timeline:** 1-2 months

#### Bug Fixes & Improvements
- [ ] Add watchdog timer for hang detection
- [ ] Implement flash operation retry logic (3 attempts)
- [ ] Add low battery detection (if battery-powered)
- [ ] Improve error messages (more descriptive OLED feedback)
- [ ] Add progress bar animation (not just percentage)

#### User Experience
- [ ] Boot splash screen with version info
- [ ] Sound feedback (buzzer on success/error) - optional
- [ ] LED status indicator (flashing during flash/sync)
- [ ] Confirmation dialog for destructive operations (Erase, Force Clean)
- [ ] Last flashed firmware indicator in menu

#### Documentation
- [ ] Detailed troubleshooting guide with photos
- [ ] Video tutorial for setup and usage
- [ ] FlashPorter tool documentation and binary release
- [ ] Schematic diagram for hardware connections
- [ ] BOM (Bill of Materials) for production

---

### v1.2 - Advanced Features (Mid-term)

**Priority:** Medium
**Timeline:** 3-4 months

#### Firmware Management
- [ ] Firmware version comparison (auto-suggest updates)
- [ ] Rollback feature (keep previous firmware)
- [ ] Batch flash mode (flash multiple targets sequentially)
- [ ] Custom flash addresses (not just 0x1000/0x8000/0x10000)
- [ ] Support for additional partitions (NVS, SPIFFS, etc.)

#### Logging & Diagnostics
- [ ] Log to SD card (persistent error logs)
- [ ] Export flash log to file (timestamp, operations, errors)
- [ ] Target chip detection (ESP32/C3/S3 auto-detect)
- [ ] Flash size detection and verification
- [ ] Monitor mode with log filtering (error/warning only)

#### Security Enhancements
- [ ] Secure boot for Host ESP32-C3
- [ ] Flash encryption for Host firmware
- [ ] HTTPS certificate validation for downloads
- [ ] Encrypted AES keys (store in NVS, not SD plaintext)
- [ ] Firmware signature verification (digital signatures)
- [ ] Access control (PIN/password for operations)

---

### v1.3 - Scalability (Long-term)

**Priority:** Low
**Timeline:** 6-12 months

#### Multi-Target Support
- [ ] Flash multiple targets in parallel (via UART multiplexing)
- [ ] Support for non-ESP targets (STM32, RP2040 via SWD/JTAG)
- [ ] Target auto-detection (probe connected chips)
- [ ] Per-target configuration profiles

#### Advanced UI
- [ ] Larger OLED (128x64) support
- [ ] Touch screen interface (optional)
- [ ] Web UI (WiFi AP mode, control via browser)
- [ ] Mobile app integration (BLE control)
- [ ] Multi-language support (EN/VN/etc.)

#### Network Features
- [ ] MQTT for remote control/monitoring
- [ ] Cloud firmware repository integration
- [ ] Scheduled sync (auto-update at specific times)
- [ ] Firmware update notifications (push alerts)
- [ ] Multi-device fleet management

#### Production Features
- [ ] Factory test mode (automated testing sequence)
- [ ] Serial number tracking (log which firmware flashed to which device)
- [ ] QR code scanning for firmware selection
- [ ] Barcode reader integration
- [ ] Production statistics (total flashed, success rate)

---

## Technical Debt

### Code Quality
- [ ] Refactor Vietnamese comments to English (consistency)
- [ ] Standardize header guards (`#pragma once` everywhere)
- [ ] Add Doxygen documentation to all functions
- [ ] Implement unit tests (Google Test framework)
- [ ] Static analysis integration (Clang-Tidy, Cppcheck)
- [ ] Code formatting with clang-format (auto-format on commit)

### Architecture Improvements
- [ ] Separate hardware abstraction layer (HAL)
- [ ] Implement dependency injection (reduce global state)
- [ ] Use FreeRTOS tasks (concurrent menu + background sync)
- [ ] Implement event-driven architecture (message queues)
- [ ] Add plugin system (loadable modules for new features)

### Build System
- [ ] CI/CD pipeline (GitHub Actions for auto-build)
- [ ] Automated testing on hardware (ESP32-C3 test rig)
- [ ] Binary release automation (GitHub Releases)
- [ ] Over-the-Air (OTA) updates for Host firmware
- [ ] Multi-target build support (C3/S3/S2)

---

## Feature Requests (Community)

### Requested but Not Prioritized
- [ ] Bluetooth firmware upload (BLE instead of WiFi)
- [ ] USB mass storage mode (SD card accessible as USB drive)
- [ ] Voice feedback (text-to-speech for status)
- [ ] Remote desktop sharing (VNC for OLED screen)
- [ ] Custom firmware post-processing (auto-patch binaries)

---

## Breaking Changes (Future Versions)

### v2.0 (Major Refactor)
**Tentative Timeline:** 12-18 months
**Breaking Changes:**
- Move from Arduino to pure ESP-IDF (performance)
- New JSON schema for index.txt (backward incompatible)
- Remove restart-after-operation (task-based architecture)
- Require ESP32-S3 minimum (more RAM, USB OTG)
- Switch to LittleFS (deprecate FAT32)

**Migration Path:**
- v1.x will remain supported for 12 months
- Migration tool provided for index.txt conversion
- Dual-boot support (v1.x fallback)

---

## Research & Exploration

### Under Investigation
- [ ] RP2040 as Host (cheaper than ESP32, PIO for UART)
- [ ] E-ink display (low power, always-on)
- [ ] Solar charging integration (field deployment)
- [ ] LoRaWAN for remote firmware delivery (long-range)
- [ ] FPGA-based flash acceleration (parallel UART)

### Proof of Concept Needed
- [ ] Encrypted SD card filesystem (full-disk encryption)
- [ ] AI-powered firmware analysis (detect compatibility issues)
- [ ] Blockchain for firmware provenance (audit trail)
- [ ] Quantum-resistant encryption (future-proofing)

---

## Performance Targets

### v1.1 Goals
- Boot time: <2 seconds (current: ~2-3s)
- Flash 1MB firmware: <20 seconds (current: ~30s)
- Sync 10 firmwares: <60 seconds (current: varies)
- Menu response: <30ms (current: <50ms)

### v1.2 Goals
- Parallel flash 4 targets: <30 seconds total
- Web UI response time: <100ms per action
- Log to SD: <10ms per entry (non-blocking)

---

## Success Metrics

### v1.0 Baseline
- Flash success rate: >95% (target: >99%)
- User satisfaction: Not measured (target: survey in v1.1)
- Bug reports: 0 critical, 2 minor (tracked on GitHub)
- Adoption: Unknown (no telemetry)

### v1.1 Targets
- Flash success rate: >99%
- Zero critical bugs in production
- 50+ active users (GitHub stars/forks)
- <5% support request rate

### v1.2 Targets
- 1000+ firmware flashes logged
- 10+ production deployments
- 100+ GitHub stars
- 3+ community contributors

---

## Dependencies & Blockers

### External Dependencies
- **ESP-IDF:** Track v5.2.x releases (security patches)
- **ArduinoJson:** Monitor for breaking changes
- **Adafruit Libraries:** Pin to stable versions
- **WiFiManager:** Community-maintained, potential deprecation risk

### Potential Blockers
- **ESP32-C3 Supply:** Chip shortages may delay production
- **SD Card Compatibility:** Some cards fail FAT32 mount (test matrix needed)
- **WiFiManager Portal:** Occasional captive portal detection failures on iOS
- **AES Performance:** Large firmware downloads may timeout (need streaming optimization)

---

## Community Engagement

### Contribution Areas
- [ ] Create CONTRIBUTING.md guide
- [ ] Issue templates for bug reports/feature requests
- [ ] Code of Conduct (adopt Contributor Covenant)
- [ ] Set up Discussions forum (GitHub Discussions)
- [ ] Monthly community calls (if interest grows)

### Documentation Needs
- [ ] Beginner tutorial (step-by-step with photos)
- [ ] Advanced configuration guide (custom AES, HTTPS certs)
- [ ] API reference for module integration
- [ ] Architecture deep-dive (this doc + more)
- [ ] Porting guide (adapt for other ESP32 variants)

---

## Funding & Resources

### Current Status
- **Funding:** Self-funded (TTP27)
- **Hardware:** Personal development boards
- **Hosting:** GitHub (free tier)

### Future Needs
- PCB manufacturing (custom flasher board design)
- Cloud hosting for firmware repository (S3/CDN)
- Test hardware (multiple ESP32 variants)
- Community support infrastructure (Discord/Slack)

---

## Risk Assessment

### Technical Risks
- **Memory Leaks:** Mitigated by restart strategy, but limits feature complexity
- **SD Card Corruption:** No journaling, power loss during write is catastrophic
- **WiFi Reliability:** Captive portals may fail on enterprise networks
- **Security:** Plaintext AES keys on SD are vulnerable to physical access

### Project Risks
- **Maintainer Availability:** Single maintainer (TTP27), bus factor = 1
- **Community Growth:** Small user base may limit feedback and contributions
- **Hardware Obsolescence:** ESP32-C3 may be superseded by newer chips
- **Library Dependencies:** Unmaintained libraries could break build

---

## Decision Log

### Key Architectural Decisions

**Decision:** Restart after each operation
**Rationale:** Simplifies memory management, prevents leaks
**Trade-off:** Slower UX, no persistent state
**Status:** Committed for v1.x, revisit in v2.0

**Decision:** Arduino framework + ESP-IDF
**Rationale:** Leverage existing libraries, easier development
**Trade-off:** Larger binary size, slower than pure IDF
**Status:** Committed for v1.x, pure IDF in v2.0

**Decision:** FAT32 for SD card
**Rationale:** Universal compatibility, simple implementation
**Trade-off:** 8.3 filename limit, no journaling
**Status:** Committed for v1.x, LittleFS in v2.0

**Decision:** Single-threaded architecture
**Rationale:** Simpler code, no synchronization needed
**Trade-off:** All operations blocking, poor concurrency
**Status:** Committed for v1.x, FreeRTOS tasks in v1.2

---

## Changelog

### v1.0.0 (2025-11-27) - Initial Release
- Complete offline and online firmware flashing
- OLED menu with button navigation
- WiFi sync with AES-128-CBC encryption
- Monitor mode and chip erase
- Documentation suite (README, PDR, architecture)

---

**Roadmap Version:** 1.0
**Next Review:** 2025-12-27 (monthly updates)
