# FlashPorter UI Rewrite Plan

**Date**: 2026-05-15
**Type**: Refactoring + UX polish
**Scope**: `toolAddFirmware/FlashPorter_Public/main.pyw` (2238 lines → multi-file)
**Author**: phucttp + Claude
**Status**: v2 — UI-first strategy, awaiting sign-off

---

## 0. Architecture (chốt 2026-05-15)

```
   [FlashPorter PC]
         │ WiFi / HTTP (1 IP duy nhất)
         ▼
   [ESP32-C3 MASTER]  ── chỉ dispatch lệnh, không stream FW
         │ I2C (lệnh + status ngắn)
   ┌─────┼─────┬─────┬─────┐
   ▼     ▼     ▼     ▼     ▼
 Phost1 Phost2 P3   ... PhostN
 0x10   0x11  0x12     0x1X
   │     │     │         │
   ▼     ▼     ▼         ▼
  FW    FW    FW        FW    ← FW lưu CỤC BỘ ở slave (SD/flash slave)
 lib   lib   lib       lib
   │     │     │         │
   ▼ UART/SWD ▼          ▼
 Target Target ...     Target
```

**Key insight**: Slave tự lưu FW + tự flash. Master + PC chỉ phát lệnh ngắn + nhận status ngắn. → I2C không có bottleneck băng thông.

## 1. Executive Summary

Tách `main.pyw` (2238 dòng monolith) thành package `tabs/`, đồng thời redesign NetFlash tab theo **master-slave architecture mới** (1 master IP, N slave I2C), polish layout (Flash All hero, Erase/Reboot All, summary), và gộp 2 tab `Add Firmware` + `Library & SD Card` thành 1 tab `Firmware Manager`.

**Strategy**: **UI-first**. Tách `MasterClient` ra thành interface, viết mock backend trả về fake data → UI thật chạy với fake slaves để user duyệt UX → khi UI OK mới ngồi viết ESP32-C3 master firmware + slave firmware.

**KHÔNG đổi**: stack (Tkinter + ttk + theme tự build), data model FW library, auth/crypto/sd_card modules, file format (`settings.json`, `index.txt`).

**ĐỔI**: `modules/net_flash.py` → 2 client: `NodeClient` (legacy, multi-IP, giữ để rollback) + `MasterClient` (mới, 1 master + N slaves). Default sang `MasterClient` với mock backend.

**ĐỂ SAU** (không trong scope đợt này): History DB + CSV export, master firmware ESP-IDF, slave firmware.

---

## 2. Current State

### File map (`main.pyw`)
| Range | Methods | Purpose |
|---|---|---|
| 48–221 | `LoginWindow` | Login modal |
| 223–355 | `MainApp.__init__`/`_build_ui` | App glue + notebook + log pane |
| 357–428 | `_build_add_tab` (+ `_choose_*`, `_clear_add_form`, `_on_id_selected`, `_new_fw_id`, `_on_fw_id_changed`, `_add_firmware`) | Tab Add Firmware |
| 429–724 | `_build_manage_tab` (+ `_refresh_firmware_list`, `_on_fw_select`, `_filter_fw_tree`, `_get_selected_fw_ids`, `_delete_firmware`, SD ops, history methods, OLED order) | Tab Library & SD |
| 725–774 | `_build_settings_tab` (+ `_load_settings`, `_save_settings`, `_change_password`, `_choose_lib_path`) | Tab Settings |
| 775–1483 | `_build_netflash_tab` + 24 `_nf_*` helpers | Tab NetFlash |
| 1484–2200 | Shared (`_log`, file pickers, encryption, git sync, history) | Cross-cutting |

### Pain points
1. **NetFlash card width = 195px** → progress bar bị nén, mini-status 1 dòng ngắn ngủn → UX tù
2. **Flash All button** không nổi bật (cùng style với mấy nút khác) → user không biết bấm đâu
3. **Thiếu Erase All / Reboot All** ở master control → phải bấm từng card
4. **Counter chỉ có "N/M online"** → thiếu Success/Failed sau khi flash
5. **Add Firmware + Library tách 2 tab** → workflow đi qua đi lại
6. **Monolith 2238 dòng** → khó maintain, mọi state đều là attribute của `MainApp`

---

## 3. Target Architecture

### File tree (sau rewrite)
```
toolAddFirmware/FlashPorter_Public/
├── main.pyw                 # ~150 dòng — chỉ entry point + LoginWindow + MainApp glue
├── modules/                 # (giữ nguyên 100%)
│   ├── auth.py
│   ├── crypto.py
│   ├── firmware_lib.py
│   ├── git_sync.py
│   ├── net_flash.py
│   ├── oled_preview.py
│   ├── sd_card.py
│   ├── theme.py
│   └── utils.py
└── tabs/                    # NEW package
    ├── __init__.py
    ├── base.py              # TabBase: shared log/_save_settings access, common helpers
    ├── firmware_manager.py  # Merged Add + Library + SD ops + history list
    ├── netflash.py          # Multi-node flash với layout mới
    └── settings_tab.py      # Settings + change password + paths
```

### Glue contract — `MainApp` truyền vào mỗi Tab
```python
class TabContext:
    auth: AuthManager
    lib: FirmwareLibrary
    sd: SDCardManager
    git: GitManager
    settings: dict
    save_settings: Callable[[], None]
    log: Callable[[str], None]      # MainApp._log
```
Mỗi tab nhận `ctx` ở `__init__`, không động chạm vào `MainApp` trực tiếp → dễ test, dễ refactor tiếp.

---

## 4. NetFlash Redesign — Master-Slave Layout

### Layout mới (top → bottom)
```
┌────────────────────────────────────────────────────────────────────────────┐
│ MASTER BAR:                                                                │
│  Master: [192.168.1.50 ____] [Connect] [🔍 Find]  •  ● Connected · 4 slaves│
├────────────────────────────────────────────────────────────────────────────┤
│ HERO ACTIONS (BG_CARD, padding 14):                                        │
│  ┌──────────────────────┐ ┌─────────────┐ ┌─────────────┐                  │
│  │  ⚡ FLASH ALL  (40h) │ │ ✖ Erase All │ │ ↻ Reboot All│  4 online        │
│  │  font 14 bold        │ │   (red)     │ │             │  3 OK · 1 ✗     │
│  └──────────────────────┘ └─────────────┘ └─────────────┘                  │
├────────────────────────────────────────────────────────────────────────────┤
│ SLAVE CARDS GRID (auto-reflow, min-width 320px):                           │
│  ┌──────────────────────────────┐  ┌──────────────────────────────┐        │
│  │ ● Phost-1            [↺][✎]  │  │ ● Phost-2            [↺][✎]  │        │
│  │   0x10 · ESP32-C3            │  │   0x11 · STM32F1             │        │
│  │   Status: Ready              │  │   Status: Flashing 67%       │        │
│  │   ──────────────────         │  │   ──────────────────         │        │
│  │   FW: [▼ EMC32_v1.2 ______]  │  │   FW: [▼ STM32_app_v0.9 __]  │        │
│  │   ╔══════════════════════╗   │  │   ╔════════════════════════╗ │        │
│  │   ║  0%                  ║   │  │   ║ ████████████░░░░  67% ║ │        │
│  │   ╚══════════════════════╝   │  │   ╚════════════════════════╝ │        │
│  │   [⚡ Flash]  [↻ Reboot]      │  │   [⚡ Flash]  [↻ Reboot]      │        │
│  │   ┌─ mini log ──────────────┐│  │   ┌─ mini log ──────────────┐│        │
│  │   │ > Idle                  ││  │   │ > Erasing sector 3       ││        │
│  │   │ > Last: EMC32 OK (3.2s) ││  │   │ > Writing 0x08010000     ││        │
│  │   │                          ││  │   │ > 67% (2.1/3.1 MB)      ││        │
│  │   └──────────────────────────┘│  │   └──────────────────────────┘│        │
│  └──────────────────────────────┘  └──────────────────────────────┘        │
│                                                                            │
│  ┌──────────────────────────────┐  ┌──────────────────────────────┐        │
│  │ ◯ Phost-3            [↺][✎]  │  │ ✖ Phost-4            [↺][✎]  │        │
│  │   0x12 · (no target)         │  │   0x13 · ESP32 (border red)  │        │
│  │   Status: Offline            │  │   Status: ✗ SWD fail         │        │
│  │   (controls disabled)        │  │   FW: [▼ disabled ________]  │        │
│  │   ...                        │  │   ...                        │        │
│  └──────────────────────────────┘  └──────────────────────────────┘        │
└────────────────────────────────────────────────────────────────────────────┘
```

### Card states (visual)
| State | Border | Dot | Controls |
|---|---|---|---|
| Online + idle | green | ● green | enabled |
| Online + busy (flashing) | blue | ● blue (blink) | Flash btn → "Cancel"? (TBD) |
| Online + last result OK | green | ● green | enabled, last_result chip |
| Online + last result FAIL | red | ✗ red | enabled (retry), error text |
| Offline | gray | ◯ gray | disabled |
| No target connected | yellow | ⚠ yellow | Flash disabled, Reboot enabled |

### Per-card info (chốt theo ý ông)
- **Label** (custom, default "Phost-N") + **I2C addr** (0x10, monospace, muted)
- **Target chip type** (ESP32-C3 / STM32F1 / STM32F4 / "no target") — slave tự detect, master forward
- **Status text**: `Ready` / `Flashing X%` / `Erasing` / `Offline` / `✗ <error>`
- **FW dropdown** (per-slave list, gọi `MasterClient.get_slave_fw_list(addr)`)
- **Progress bar**
- **Last result chip**: small badge "OK 3.2s" / "FAIL" cạnh status text
- **Mini log** 3 dòng (tk.Text height=3, ring buffer)
- **Edit button** [✎]: mở dialog đổi label slave (lưu vào settings.json)

### Mock backend interface (`MasterClient`)
```python
class MasterClient:
    """1 master, N slaves."""
    def connect(self, host: str) -> dict: ...
        # → {ok, slaves_count, master_fw_version}
    def get_slaves(self) -> list[dict]: ...
        # → [{addr, label, online, target_type, status, current_fw, last_result}, ...]
    def get_slave_fw_list(self, addr: int) -> list[dict]: ...
        # → [{id, display}, ...]   per-slave
    def get_status(self) -> dict: ...
        # → {slaves: [{addr, busy, progress, status_text}, ...]}   aggregate 1-shot
    def flash_slave(self, addr: int, fw_id: str) -> dict: ...
    def flash_all(self, fw_per_slave: dict[int, str]) -> dict: ...
    def erase_slave(self, addr: int) -> dict: ...
    def erase_all(self) -> dict: ...
    def reboot_slave(self, addr: int) -> dict: ...
    def reboot_all(self) -> dict: ...
    def set_slave_label(self, addr: int, label: str) -> None: ...  # local-only, save settings

class MockMasterClient(MasterClient):
    """Fake backend trả về fixed data + progress simulation cho dev UI."""
```

### Changes table (vs code hiện tại)
| # | Item | Before | After |
|---|---|---|---|
| N1 | Identity | IP per node | 1 master IP + N slaves (I2C addr) |
| N2 | Discovery | Scan subnet → N IP | Scan subnet → 1 master → `GET /slaves` |
| N3 | Card min-width | 195px | **320px** |
| N4 | Flash All button | normal | **Hero**: `Hero.TButton`, font 14b, padding 14x24 |
| N5 | Erase All / Reboot All | ❌ | ➕ với confirm dialog gộp |
| N6 | Status summary | "N/M online" | "N online · X OK · Y ✗" |
| N7 | Mini log per card | 1 dòng StringVar | `tk.Text` h=3 ring buffer |
| N8 | Per-slave FW list | ❌ (chung) | ➕ mỗi slave có FW list riêng |
| N9 | Auto-find master | ❌ thủ công | Chạy nếu `settings.master_ip` rỗng |
| N10 | Error border | ❌ | Border đỏ + error text ngắn |
| N11 | Slave label edit | ❌ | ➕ dialog đổi tên slave, persist `settings.slave_labels` |

### settings.json schema mở rộng
```json
{
  "master_ip": "192.168.1.50",
  "slave_labels": {"0x10": "Phost-1", "0x11": "STM-station"},
  "...existing keys..."
}
```

---

## 5. Firmware Manager Merge — Detailed

### Layout
```
┌──────────────────────────────────────────────────────────────────────┐
│ [🔍 search _______________]              [+ New FW] [⟳ Refresh]      │
├─────────────────────────────┬────────────────────────────────────────┤
│ LIST (left, 35%)            │ DETAIL (right, 65%)                    │
│ ┌─────────────────────────┐ │ ┌─ tab pane (top of right panel) ────┐ │
│ │ FW_ID    Type    Ver    │ │ │ [Info & SD Ops]  [Edit / Add]      │ │
│ │ ─────────────────────── │ │ ├────────────────────────────────────┤ │
│ │ • EMC32  esp32   1.2.0  │ │ │ INFO tab:                          │ │
│ │ • XYZ01  stm32f1 0.9.0  │ │ │   FW ID:   EMC32                   │ │
│ │ • ABC    stm32f4 2.1.0  │ │ │   Type:    esp32                   │ │
│ │ ...                     │ │ │   Version: 1.2.0                   │ │
│ │                         │ │ │   Desc:    ...                     │ │
│ │ (treeview)              │ │ │   Files:   FW.bin / boot / part    │ │
│ │                         │ │ │   ── SD Ops ─────────────          │ │
│ │                         │ │ │   SD: [E:\___] [Browse] [Load]     │ │
│ │                         │ │ │   [Copy plain] [Copy enc]          │ │
│ │                         │ │ │   [Remove from SD]                 │ │
│ │                         │ │ │   [⟶ Sync to Git]                  │ │
│ │                         │ │ │   [🗑 Delete from Library]          │ │
│ │                         │ │ │                                    │ │
│ │                         │ │ │ EDIT/ADD tab:                      │ │
│ │                         │ │ │   (form từ _build_add_tab cũ)      │ │
│ └─────────────────────────┘ │ │   FW ID / Device Type / Version /  │ │
│                             │ │   Description, 3 file pickers,     │ │
│                             │ │   [Add to Library] / [Save Edits]  │ │
│                             │ └────────────────────────────────────┘ │
└─────────────────────────────┴────────────────────────────────────────┘
```

### Mapping methods cũ → tab mới
| Cũ | Mới (trong `tabs/firmware_manager.py`) |
|---|---|
| `_build_add_tab` | `_build_edit_pane()` |
| `_build_manage_tab` | `_build_list_pane()` + `_build_info_pane()` |
| `_refresh_firmware_list`, `_filter_fw_tree`, `_on_fw_select` | giữ nguyên signature |
| `_choose_app/_boot/_part`, `_add_firmware`, `_new_fw_id`, `_clear_add_form`, `_on_id_selected`, `_on_fw_id_changed`, `_on_device_type_changed`, `_is_stm32_selected` | move vào edit pane |
| `_choose_sd_path`, `_load_sd_card`, `_on_sd_select`, `_copy_to_sd_plain`, `_copy_to_sd_enc`, `_remove_from_sd`, `_sync_to_git` | move vào info pane (SD Ops section) |
| `_delete_firmware`, `_get_selected_fw_ids` | giữ trong list pane |
| `_history_*` (5 method) | Tạm để trong list pane phần đáy (collapsible LabelFrame) — sẽ tách ra History tab ở đợt sau |
| `_on_oled_order_apply`, `_on_oled_rename` | giữ với OLED preview widget (chuyển sang info pane phần dưới) |

### Edit/Add mode switch
- Bấm `+ New FW` → clear form, mode = `add`, button label "Add to Library"
- Click vào row trong list → populate form, mode = `edit`, button label "Save Edits"
- Mode = `edit` thì FW_ID disable (tránh đụng vào safe_name folder).

---

## 6. Implementation Plan

### Phase 0 — Mock backend (~45min, risk: LOW)
1. Tạo `modules/master_client.py`:
   - `MasterClient` (abstract interface — methods ở section 4)
   - `MockMasterClient`:
     - 4 fake slaves: `[(0x10,"Phost-1","esp32"), (0x11,"Phost-2","stm32f1"), (0x12,"Phost-3","no-target"), (0x13,"Phost-4","esp32")]`
     - `flash_slave()` start 1 thread tăng progress 0→100% trong ~4s, random fail 10% để test error UX
     - `get_status()` trả về snapshot từ shared dict (`threading.Lock`)
2. Add toggle vào `settings.json`: `"netflash_backend": "mock" | "real"` (default `"mock"` cho dev). Sau này khi firmware sẵn sàng → đổi `"real"`.

**Acceptance**: `python -c "from modules.master_client import MockMasterClient; m=MockMasterClient(); m.connect('x'); print(m.get_slaves())"` in ra 4 slaves giả.

### Phase 1 — Skeleton (1 session, ~1h, risk: LOW)
1. Tạo `tabs/__init__.py`, `tabs/base.py` (TabContext dataclass: auth, lib, sd, git, settings, save_settings, log, master_client)
2. Tạo `tabs/settings_tab.py` — move `_build_settings_tab` + `_change_password` + `_choose_lib_path`. Tab nhỏ nhất, verify pattern trước.
3. Tạo `tabs/firmware_manager.py` — copy `_build_add_tab` + `_build_manage_tab` (chưa merge layout, 2 sub-Frame trong 1 tab) → app chạy y hệt phần FW.
4. Tạo `tabs/netflash.py` — copy toàn bộ `_nf_*` + `_build_netflash_tab` nguyên xi (chưa redesign — vẫn dùng `NodeClient`/multi-IP cũ).
5. `main.pyw` gọn lại ~200 dòng: `LoginWindow` + `MainApp` glue.

**Acceptance**: App boot lên trông y hệt bản cũ, click hết các tab không lỗi.

### Phase 2 — NetFlash master-slave redesign (1 session, ~2.5h, risk: MED-HIGH)
**Đây là phase đập đi xây lại lớn nhất.** Code cũ multi-IP sẽ bị thay hoàn toàn.

1. Trong `tabs/netflash.py`:
   - Xóa toolbar add-IP, thay bằng **Master Bar** (1 ô IP + Connect + Find).
   - Add **Hero Actions** row: ⚡ FLASH ALL (Hero style), ✖ Erase All, ↻ Reboot All, summary text.
   - `_nf_cards` keyed by `addr` (int) thay vì `host` (str).
   - Discovery: `_nf_find_master()` scan subnet → tìm 1 master → save vào `settings.master_ip`.
   - `_nf_connect_master()` → `master_client.connect()` → `get_slaves()` → render cards từ data thật.
   - Polling: 1 thread gọi `get_status()` mỗi 500ms khi có slave busy, fan out update cards.
2. Thêm style `Hero.TButton` trong `modules/theme.py`.
3. Per-card mini log (tk.Text h=3 ring buffer).
4. Error UX: border đỏ + status text khi slave fail/no-target.
5. Slave label edit dialog ([✎] button → small Toplevel → save `settings.slave_labels[addr]`).
6. Auto-find master nếu `settings.master_ip` rỗng (sau login 800ms).
7. Toast sau Flash All: `messagebox.showinfo("Flash All Done", "OK: 3 · Failed: 1")`.

**Acceptance**:
- Login → tab NetFlash → tự connect mock master → 4 cards render (Phost-1..4 với target khác nhau).
- Bấm ⚡ FLASH ALL → 4 progress bars chạy → 1 card fail (random) chuyển border đỏ, 3 card OK xanh.
- Bấm [✎] đổi label "Phost-1" → "Station-A" → restart app → label vẫn còn.

### Phase 3 — Firmware Manager merge (1 session, ~2h, risk: MED)
1. Trong `tabs/firmware_manager.py`, đập layout → `ttk.PanedWindow` ngang (left 35% list, right 65%).
2. Right panel = `ttk.Notebook` con 2 tab: `Info & SD Ops`, `Edit / Add`.
3. Move widget cũ vào pane theo mapping ở section 5.
4. State machine `add`/`edit`: button label đổi, FW_ID disable khi edit.
5. Test workflow: `+ New FW → fill form → Add → list refresh → click row → Copy to SD`.

**Acceptance**: Workflow `+ New FW → Add → Copy to SD plain` chạy 1 mạch không đổi tab.

### Phase 4 — Smoke test + commit (~30min, risk: LOW)
1. Smoke test full flow: login → NetFlash auto-find → 4 slaves → Flash All → Firmware Manager add FW → SD ops.
2. Commit riêng từng phase:
   - `feat(netflash): add MasterClient interface + mock backend (phase 0)`
   - `refactor: split main.pyw into tabs/ package (phase 1)`
   - `feat(netflash): master-slave redesign with hero actions (phase 2)`
   - `feat(fw-manager): merge add+library tabs into single panel (phase 3)`

---

## 7. Backward Compatibility

| Thứ | Đổi không? |
|---|---|
| `settings.json` schema | Không. Giữ tất cả key cũ. |
| `index.txt` (SD card) | Không. |
| AES key/iv format | Không. |
| HTTP API với ESP32-C3 host | Không. |
| Git repo structure (firmware_library/) | Không. |
| User credentials (`credentials.json`) | Không. |

**Rollback plan**: Mỗi phase commit riêng → nếu phase 3 hỏng, `git revert` phase 3 commit, phase 1+2 vẫn còn (skeleton + NetFlash polish vẫn dùng được).

---

## 8. Risk Assessment

| Risk | Impact | Likelihood | Mitigation |
|---|---|---|---|
| Move code làm vỡ reference giữa methods | High | Med | Phase 1 = pure move + import, không đổi logic. |
| Mock backend lệch với firmware thật sau này | High | Med | Định interface `MasterClient` rõ ngay Phase 0; mock tuân thủ interface; firmware phải implement đúng interface. |
| UI design vẫn đổi sau Phase 2 | Med | Med | Stop & review với user sau Phase 2 trước khi qua Phase 3. |
| Slave label persistence corrupt settings.json | Low | Low | Validate addr là hex string, label max 32 char, backup `settings.json.bak` trước save. |
| User mất unsaved form data khi click row mới (FW Manager) | Low | Med | Phase 3: nếu mode=edit và form dirty → confirm "Discard changes?". |
| Mini log `tk.Text` chậm khi spam update | Low | Low | Ring buffer 3 dòng, `.config(state=disabled)` ngoài lúc insert. |
| Erase All race condition giữa các slave | Med | Low | Master xử lý sequential I2C; PC chỉ gửi 1 POST `/erase_all`, master tự loop. |
| Auto-find master scan subnet chậm | Low | Med | Đã có `discover_nodes()` parallel TCP, reuse. Timeout 5s, run background. |

---

## 9. Out of Scope (đợt sau)

- ❌ History DB + CSV export
- ❌ Đổi sang CustomTkinter
- ❌ Plugin / cert / eFuse / signed FW
- ❌ Unit tests cho Tkinter (smoke test thủ công)
- ❌ i18n đa ngôn ngữ
- ❌ ESP32-C3 master firmware (sẽ làm khi UI mock OK)
- ❌ Phost slave firmware (sẽ làm sau master)
- ❌ Real HTTP `MasterClient` impl (chỉ mock trong scope này)
- ❌ Multicast I2C flash (nice-to-have, sau)

---

## 10. Open Questions (đã chốt với user 2026-05-15)

| # | Question | Answer |
|---|---|---|
| 1 | Tên tab merged | **Firmware Manager** (English, đồng bộ) |
| 2 | Commit strategy | **3 commit riêng** + Phase 0 = 4 commits total |
| 3 | Hero button text | **⚡ FLASH ALL** |
| 4 | Erase confirm | **1 dialog gộp** |
| 5 | Auto-discover | **Find master if `settings.master_ip` empty** |
| 6 | Slave ID display | **Label + I2C addr** |
| 7 | FW pool | **Mỗi slave có FW list riêng** |
| 8 | Card info | Trạng thái (rảnh/busy), target chip info, online/offline, last result, mini log |
| 9 | Error UX | **Border đỏ + error text ngắn** |
| 10 | Strategy | **UI-first** → mock backend → firmware sau |

---

**Plan v2 sẵn sàng sign-off.**

Sau khi ông OK → tui chạy:
- **Phase 0** (mock backend, ~45min) → commit
- **Phase 1** (skeleton tách file, ~1h) → commit + dừng cho ông kiểm tra app boot
- **Phase 2** (NetFlash master-slave redesign, ~2.5h) → commit + dừng review UX
- **Phase 3** (Firmware Manager merge, ~2h) → commit
- **Phase 4** (smoke test, ~30min)

Tổng ~6.5h work, chia 4 commit, có check-point sau Phase 1 và Phase 2.
