# ESP MultiFlasher (Muti)

A **portable, PC-free firmware programmer** built on a single ESP32-C3.

Firmware lives on an SD card. Pick one from a 3-button OLED menu and it is
written to the target — ESP32 over UART, STM32 over bit-banged SWD — with no
computer attached. Refresh the library over WiFi when there is a network, or
carry a prepared card when there is not.

```
   ┌──────────────┐                          ┌──────────────┐
   │   SD card    │   FW library + index     │   ESP32-C3   │
   │  index.txt   │─────────────────────────►│     Muti     │
   └──────────────┘                          │  OLED + 3btn │
   ┌──────────────┐   sync / NetFlash / BLE  └──────┬───────┘
   │  WiFi (opt)  │◄────────────────────────────────┤
   └──────────────┘                     UART / SWD  │
                                             ┌──────▼───────┐
                                             │  Target chip │
                                             └──────────────┘
```

WiFi is optional everywhere. A unit that never sees a network still flashes
from its card; the network only changes how firmware *gets onto* the card.

> **Which branch is this?**
> The repository is named `sd-flash-esp32` and its `main` branch is a
> different product — the **CHIVI FlashPorter TFT**, an ESP32-S3 build with a
> TFT screen and a USB drive, with WiFi removed. Muti lives on **`muti-oled`**.
> The two share history up to `9a58899` and diverged there. Do not merge one
> into the other expecting it to work.

## Supported targets

| Target | Interface | Protocol | Detection |
|---|---|---|---|
| ESP32 (all variants) | UART | `esp-serial-flasher` | — |
| STM32F1 (Cortex-M3) | SWD | FPEC half-word | IDCODE `0x1BA01477` |
| STM32F4 (Cortex-M4) | SWD | Flash CR sector | IDCODE `0x2BA01477` |

STM32 family is chosen from the SWD IDCODE, so one menu entry works for either
part. Read protection (RDP) is detected, confirmed with the user, and cleared
by mass-erase before writing — up to 3 attempts.

STM32 writes go out in 256-byte chunks, each read back and compared, with 3
retries per chunk. Connection is retried 10 times at 500 ms.

## Hardware

**ESP32-C3** (RISC-V), ESP-IDF v5.1.6+ with the Arduino component.

| Function | GPIO | Notes |
|---|---|---|
| OLED SDA / SCL | 8 / 9 | I²C, 128×64 @ `0x3C` |
| SD card CS | 7 | SPI, FAT32 |
| Button UP / DOWN / OK | 21 / 10 / 20 | active low, `INPUT_PULLUP` |

The four target pins are shared between the two modes:

| GPIO | ESP32 mode | STM32 mode |
|---|---|---|
| 0 | UART TX | **SWDIO** |
| 1 | UART RX | — |
| 2 | RESET trigger | — |
| 3 | BOOT trigger | **SWCLK** |

`nRESET` is not wired; use the reset button on the target board. GPIO 2 is
deliberately left out of the SWD assignment — it is FSPIQ by default on C3.

> ⚠️ Power STM32 targets from **3.3 V only**. 5 V causes an SWD level mismatch
> and can damage the programmer's GPIOs.

### About the display

The panel is an **SSD1306**, driven through Adafruit's `Adafruit_SH1106G`
class. That is not a mistake left in place — the class was deliberately
repointed at the SSD1306 register set (`_page_start_offset = 0`, charge pump
`0x8D,0x14`, PAGE addressing) because the rest of the stack was already built
on `Adafruit_SH110X`.

`OledUI` itself is driver-agnostic: it only calls `Adafruit_GFX` primitives,
and `SH110X_WHITE`/`BLACK` are `1`/`0` — the same values as their SSD1306
counterparts. So the driver swap never reached the UI layer.

If you put a genuine SH1106 on a board, revert that file; nothing else changes.

## Firmware library on the SD card

```
SD_ROOT/
├── index.txt              JSON metadata for every firmware
├── config/
│   └── url.txt            sync source URL (not secret — key/IV live in NVS)
└── f001/
    ├── FW.bin             app        (or .enc when encrypted)
    ├── BOTL.bin           bootloader (ESP32 only)
    └── PART.bin           partitions (ESP32 only)
```

One entry per firmware in `index.txt`:

```json
{
  "fw_id": "f001",
  "device_type": "v2",
  "version": "2.3.5",
  "path": "f001/FW.bin",
  "md5": "cd47774716f78d0567429821bdb840e6",
  "path_bootloader": "f001/BOTL.bin",
  "md5_bootloader": "538b88676ae1c4f4c327536512cca616",
  "path_partition": "f001/PART.bin",
  "md5_partition": "394900faa84ab5389d7cde19eb9797cd"
}
```

Every file is MD5-checked after writing. Encrypted firmware stays `.enc` on the
card and is decrypted in flight — AES-128-CBC, key and IV in NVS rather than on
the card, so a lifted SD card yields ciphertext.

## UI

Five tabs: **FW** (list, select to flash), **Tools**, **Desc**, **Hist**
(last 10 flashes), **Info**. Selected tab and item are saved across reboots.

Tools also carries the serial monitor, chip erase, device scan, and WiFi setup.

## Getting WiFi onto a fresh unit

Tools → WiFi Config offers two paths, BLE first:

```
  0  WiFi qua BLE          ← default
  1  Portal (WiFi+URL)     ← fallback, also sets URL / AES key / IV
```

**BLE is the default because the SoftAP portal was unreachable on this board** —
the AP comes up off-channel and phones never list it. BLE provisioning skips the
scan entirely: pair from the standard *ESP BLE Provisioning* app and send
credentials directly.

The portal is kept, not replaced, because it is still the only way to enter the
sync URL and the AES key/IV — BLE provisioning has no field for those.

## NetFlash — HTTP API over WiFi

Advertised over mDNS as `flashporter-XXXX.local`.

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/ping` | liveness |
| `GET` | `/api/status` | current state |
| `GET` | `/api/fw_list` | library contents |
| `POST` | `/api/flash` | flash a `fw_id` |
| `POST` | `/api/reboot` | reboot the unit |
| `POST` | `/api/update_meta` | correct MD5 fields for a `fw_id` |

`update_meta` exists for one reason: a wrong MD5 in `index.txt` makes a
firmware unflashable, and the only other cure is physically pulling the SD card.
On a unit already at a customer site, that is a site visit. It accepts any
subset of `md5` / `md5_bootloader` / `md5_partition`, ignores values that are
not 32 hex chars, and returns 404 for an unknown `fw_id` rather than inventing
an entry.

## Sync

Pulls the library from the configured URL and reconciles it against the card.

`index.txt` is written **first**, before any firmware download, then reloaded.
The comparison that decides what to re-download runs against a snapshot of the
old map taken beforehand. This ordering matters: writing the index last meant a
sync interrupted midway left new binaries on the card described by stale
metadata, and the next boot trusted that metadata.

## Build

Requires **ESP-IDF v5.1.6+**.

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32c3
idf.py build
idf.py -p COM3 flash monitor
```

`partitions.csv` is custom — a **3 MB factory partition**. Linking Bluedroid for
BLE provisioning pushes the image past the stock 2 MB single-app-large preset.
`sdkconfig.defaults` is committed alongside it so a clean checkout does not
silently fall back to the default table and then fail to link.

> **Fresh clones do not build yet.** `components/Adafruit_BusIO`,
> `Adafruit_GFX` and `Adafruit_SSD1306` are recorded as gitlinks (mode
> `160000`) but the repository has no `.gitmodules`, so `git clone` leaves
> three empty directories and the build fails on a missing `Adafruit_GFX`.
> Copy those three from a working tree, or convert them to plain tracked
> files. Unresolved — see [Known gaps](#known-gaps).

## Three bugs worth knowing about

**The captive portal killed itself in about five seconds.** Phones probe a new
AP with `/generate_204` and friends the instant they associate. WiFiManager
registers no handler, so they fell through to the default 404 path — which
calls `WebServer.send()` with a body, and that blocks while the WiFi TX queue is
full. Long enough to trip the main-task watchdog, right as the user opened the
page. Answering 204 with an empty body removes the blocking send, and 204 is
what the probes want anyway.

**Every encrypted firmware failed MD5.** AES-CBC pads plaintext up to a 16-byte
boundary, so the decrypted stream is up to 16 bytes longer than the file it came
from. MD5 was taken over the padded length and compared against a hash of the
original — a guaranteed mismatch on correctly written images. PKCS7 stores the
pad length in the final byte; subtract it before hashing.

**Some ESP32 targets never entered download mode.** The ROM samples BOOT next to
the *rising edge* of EN, so the order of the two releases decides the outcome —
not the delay before them. Pull both low, wait 100 ms for the rail to actually
drop, then release BOOT **before** EN. Targets that failed under brute-force
retry now come up first try.

## FlashPorter — PC tool

```bash
cd toolAddFirmware/FlashPorter_Public
pip install -r requirements.txt
python main.pyw
```

Python/Tkinter. Manages the firmware library, encrypts and pushes to cloud,
prepares SD cards, and drives NetFlash. Runs against a mock backend with no
hardware attached, which is how the UI is developed.

> `tool_setting.json` holds the AES key and IV. It is gitignored at both the
> repo root and under `toolAddFirmware/`. Keep it that way.

## Related projects

| | |
|---|---|
| **CHIVI FlashPorter TFT** | Same repo, `main` branch. ESP32-S3, TFT, USB-MSC drive, no WiFi. |
| **[Gang Flasher (GangF)](https://github.com/phucttp/GangF)** | Separate repo, forked from Muti. One master coordinating N programmer nodes over I²C for parallel multi-target flashing. |

## Known gaps

- **Fresh clone does not build** — three components are orphaned gitlinks with
  no `.gitmodules` (see [Build](#build)). This is the first thing to fix.
- **SWD runs at roughly a third of the speed it could.** `targetConnect()` is
  called with no argument, so the DAP delay loop uses the header default of 50
  NOPs per half-cycle. GangF's programmer node passes 10 and measured 20 s → 6.6 s
  on a 38 KB image with zero retries. The change is one parameter; it has simply
  never been re-measured on Muti hardware, where the OLED shares the I²C bus.
- **No noise-resilience retry on SWD select.** The TFT branch added a 5× retry
  with a DP sticky-error clear (`STKERR`/`WDERR`/`ORUNERR`) that Muti does not
  have. Worth porting.
- `dependencies.lock` currently records IDF 5.1.6 while at least one build
  machine is on 5.1.7.

## License

MIT — TTP27 (2025–2026)
