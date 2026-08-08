# Master ↔ Phost I²C Protocol

Communication contract between the **ESP32-C3 master** and the **Phost
slaves** (firmware burners that flash the actual target chip via SWD/UART).
The master runs an I²C bus; every phost is a slave on that bus.

- **Protocol version:** 1
- **Transport:** I²C, 7-bit addressing, **400 kHz Fast mode**
- **Bus role:** Master = I²C master (only initiator), Phost = I²C slave (responds only)
- **Max phosts per bus:** 10
- **Concurrency:** Parallel (master fires commands, phosts run jobs in background, master polls status)

The PC never talks to phosts directly. PC → Master is WebSocket
(see [master-api.md](master-api.md)); Master → Phost is this protocol.

```
PC ──WS──► Master ──I²C──► Phost ──SWD/UART──► Target chip
                            ▲
                            └──WiFi──► FW Library Server (phost pulls own copy)
```

---

## 1. Bus Topology

| Property | Value |
|---|---|
| Speed | 400 kHz (Fast mode) |
| Pull-up | 4.7 kΩ on SDA + SCL to 3.3 V |
| Max devices | 10 phosts (addresses 0x10–0x19) |
| Wiring | Daisy-chain preferred, total length < 50 cm |
| Bus capacitance budget | ≤ 400 pF (10 phosts ≈ 100 pF, safe) |
| Clock stretching | Enabled on phost side (allow up to 10 ms) |

> **Long-bus / production-jig note:** if board layout forces > 50 cm total
> length, or routing passes through switches / pogo-pins (typical for
> gang-flash jigs where parasitic capacitance accumulates fast), add an I²C
> buffer IC (PCA9517 / PCA9515) between master and the phost chain. For
> severe parasitic loads — measure SDA rise time on a scope; if > 1 µs at
> 400 kHz the bus is borderline — split into segments with an I²C
> multiplexer (TCA9548A) so each segment sees only 2–4 phosts. Last resort:
> drop to 100 kHz.

### Address Map

```
0x00–0x07   I²C reserved
0x08–0x0F   reserved for master's internal use
0x10–0x19   10 phost slots (PHOST_01 … PHOST_10)
0x1A–0x77   free for future expansion / on-board sensors
0x78–0x7F   I²C reserved
```

Each phost stores its own address in NVS. Provisioning (via the **Phost Setup**
tab in FlashPorter) assigns the first free slot in `0x10–0x19`.

---

## 2. Frame Format

Each I²C transaction is one direction (write to phost OR read from phost).
There is no length prefix on the wire — both sides know the size per command
from a fixed lookup table (see [§4](#4-command-reference)).

### Master → Phost (write transaction)

```
[CMD_CODE][PAYLOAD...][CRC8]
    1B       0–30B      1B
```

### Phost → Master (read transaction)

```
[STATUS_BYTE][DATA...][CRC8]
     1B        0–30B    1B
```

`STATUS_BYTE` values:

| Code | Meaning |
|---|---|
| 0x00 | OK — data follows, CRC valid |
| 0x01 | BUSY — phost can't accept new command right now |
| 0x02 | INVALID_CMD — unknown command code |
| 0x03 | CRC_FAIL — last received command had bad CRC, discarded |
| 0x04 | ERROR — payload contains error details (see [§6](#6-error-codes)) |

### CRC8

- Polynomial: `0x07` (CRC-8/CCITT)
- Initial value: `0x00`
- Covers all bytes in the frame **except** the CRC byte itself
- A frame with bad CRC is silently dropped (master must time-out and retry)

### Why per-command fixed size (not length-prefix)?

I²C masters must specify how many bytes to read from a slave **before** the
read starts. Length-prefix would force two separate read transactions (one for
length, one for body), doubling overhead. Per-command fixed size lets master
read everything in a single transaction once it knows the command code it
issued.

---

## 3. Encoding Rules

- **Endianness:** little-endian for all multi-byte integers
- **Strings:** ASCII, null-padded, fixed slot length (no null-terminator required)
- **Booleans:** 1 byte, 0 = false, non-zero = true
- **Reserved bytes:** must be sent as `0x00`, receivers ignore on read

---

## 4. Command Reference

| Code | Name | Cmd size | Resp size | Purpose |
|------|------|----------|-----------|---------|
| 0x01 | `PING` | 2 | 2 | Liveness check, used in discovery |
| 0x02 | `GET_INFO` | 2 | 18 | Static info (model, fw version, uptime) |
| 0x03 | `READ_STATUS` | 2 | 34 | Snapshot of current state (most-polled cmd) |
| 0x10 | `FLASH_FW` | 3 | 2 | Start flashing `fw_id` (background job) |
| 0x11 | `ERASE_TARGET` | 2 | 2 | Erase target chip flash |
| 0x12 | `REBOOT_TARGET` | 2 | 2 | Pulse target reset line |
| 0x13 | `ABORT` | 2 | 2 | Cancel running job |
| 0x20 | `SET_LABEL` | 18 | 2 | Set 16-char human label (persisted in NVS) |
| 0x21 | `SET_TARGET_TYPE` | 3 | 2 | Tell phost which target chip is wired |
| 0x30 | `SYNC_LIBRARY` | 2 | 2 | Trigger phost to pull manifest over WiFi |
| 0x31 | `GET_LIBRARY` | 2 | 34 | List FW available on this phost (first page) |
| 0x32 | `GET_LIBRARY_NEXT` | 3 | 34 | Next page of FW list (`offset` byte) |

Cmd/Resp sizes include the trailing CRC8 byte.

### 4.1 PING (0x01)

```
Cmd:  [0x01][CRC]                          → 2 bytes
Resp: [STATUS=0x00][CRC]                   → 2 bytes
```

Master uses this to scan addresses at startup and to recover after long
silence.

### 4.2 GET_INFO (0x02)

```
Cmd:  [0x02][CRC]                          → 2 bytes
Resp: [STATUS][model:1][fw_major:1][fw_minor:1][fw_patch:1]
      [uptime_sec:4][serial:8][CRC]        → 18 bytes
```

`model` codes:

| Code | Model |
|---|---|
| 0x01 | PHOST_V1 (ESP32-C3 standalone) |
| 0x02 | PHOST_V2 (planned) |

### 4.3 READ_STATUS (0x03)

```
Cmd:  [0x03][CRC]                          → 2 bytes
Resp: [STATUS][PhostStatus:32][CRC]        → 34 bytes
```

`PhostStatus` is 32 bytes — see [§5](#5-phoststatus-struct).
This is the workhorse command; master calls it 1 Hz idle / 5 Hz active per phost.

### 4.4 FLASH_FW (0x10)

```
Cmd:  [0x10][fw_id:1][CRC]                 → 3 bytes
Resp: [STATUS][CRC]                        → 2 bytes
```

- `fw_id` references an entry in the phost's local FW library
  (see `GET_LIBRARY`).
- If `fw_id` is not in the library, response `STATUS = 0x04 ERROR` and a
  subsequent `READ_STATUS` reports `error_code = 0x20 FW_NOT_FOUND`.
- On `STATUS = 0x00`, the job has been **accepted and started**. Master must
  poll `READ_STATUS` for progress.

### 4.5 ERASE_TARGET (0x11) / REBOOT_TARGET (0x12) / ABORT (0x13)

Same shape as `FLASH_FW` without the payload:

```
Cmd:  [CODE][CRC]                          → 2 bytes
Resp: [STATUS][CRC]                        → 2 bytes
```

All are fire-and-forget — they queue work, master polls for completion.
`ABORT` cancels any running job (flash, erase, sync). Idempotent on idle.

### 4.6 SET_LABEL (0x20)

```
Cmd:  [0x20][label:16][CRC]                → 18 bytes
Resp: [STATUS][CRC]                        → 2 bytes
```

Label is 16 ASCII bytes, null-padded if shorter. Saved to NVS; persists across
reboot. Shown in NetFlash UI cards.

### 4.7 SET_TARGET_TYPE (0x21)

```
Cmd:  [0x21][target:1][CRC]                → 3 bytes
Resp: [STATUS][CRC]                        → 2 bytes
```

`target` codes:

| Code | Target |
|---|---|
| 0x00 | NONE / autodetect |
| 0x01 | STM32F1 |
| 0x02 | STM32F4 |
| 0x10 | ESP32 |
| 0x11 | ESP32-C3 |
| 0x12 | ESP32-S3 |

Persisted in NVS. Phost uses this to pick the right protocol engine when
`FLASH_FW` runs.

### 4.8 SYNC_LIBRARY (0x30)

```
Cmd:  [0x30][CRC]                          → 2 bytes
Resp: [STATUS][CRC]                        → 2 bytes
```

Tells the phost to start a manifest-based sync over its own WiFi. While
syncing, `READ_STATUS.state` returns `SYNCING` and `bytes_done`/`bytes_total`
track download progress.

### 4.9 GET_LIBRARY (0x31) / GET_LIBRARY_NEXT (0x32)

```
Cmd 0x31: [0x31][CRC]                      → 2 bytes
Cmd 0x32: [0x32][offset:1][CRC]            → 3 bytes
Resp:     [STATUS][count:1][total:1]
          [entries: 6 × (fw_id:1 + name:4)][CRC]
                                           → 34 bytes
```

- `total` = total FW count on this phost
- `count` = entries in this response (≤ 6)
- Each entry: `fw_id` byte + 4-char short name (e.g. `"F001"`)
- For libraries > 6 entries, master calls `GET_LIBRARY_NEXT` with
  `offset = 6, 12, …` until `count = 0`

The 4-char name is a display hint only; the canonical name lives on the FW
server manifest. Master can ignore the short name and look up full metadata
via the PC's WebSocket layer.

---

## 5. PhostStatus Struct

Returned by `READ_STATUS`. Exactly 32 bytes little-endian.

```c
struct PhostStatus {            // offset
    uint8_t  state;             // 0  — see state enum below
    uint8_t  progress;          // 1  — 0..100
    uint8_t  current_fw_id;     // 2  — 0xFF if no active fw
    uint8_t  target_chip;       // 3  — same codes as SET_TARGET_TYPE
    uint32_t bytes_done;        // 4
    uint32_t bytes_total;       // 8
    uint16_t event_seq;         // 12 — increments on every state change
    uint8_t  error_code;        // 14 — see §6
    int8_t   wifi_rssi;         // 15 — dBm, 0 if no wifi
    uint8_t  lib_count;         // 16 — number of FW stored locally
    uint8_t  flags;             // 17 — bit flags (see below)
    uint8_t  label[8];          // 18 — first 8 chars of label (truncated)
    uint8_t  reserved[6];       // 26 — must be 0
};                              // total: 32 bytes
```

### state enum

| Value | Name | Meaning |
|---|---|---|
| 0 | `IDLE` | No active job |
| 1 | `FLASHING` | Writing FW to target |
| 2 | `VERIFYING` | Reading back + comparing |
| 3 | `ERASING` | Target chip erase in progress |
| 4 | `SYNCING` | Pulling FW library over WiFi |
| 5 | `DONE` | Last job finished OK, awaiting next command |
| 6 | `ERROR` | Last job failed, see `error_code` |
| 7 | `BUSY_BOOT` | Phost still initializing after reset |

### flags bits

| Bit | Name |
|---|---|
| 0 | `WIFI_CONNECTED` |
| 1 | `TARGET_DETECTED` |
| 2 | `LIBRARY_SYNCED` (last sync OK) |
| 3 | `STORAGE_LOW` (< 10% free) |
| 4–7 | reserved |

### event_seq usage

Phost increments `event_seq` (mod 65536) **every time `state` or
`error_code` changes**. Master caches `last_seq[addr]` and only forwards
events to the PC when it detects a delta — avoids spamming the WebSocket
with identical "progress 47%, still flashing" updates.

Progress changes alone do **not** bump `event_seq` — they're observable in
the next poll cycle but don't need to be event-broadcast.

---

## 6. Error Codes

Value of `PhostStatus.error_code` after a job fails (`state = ERROR`).

| Code | Name | Meaning |
|---|---|---|
| 0x00 | `OK` | No error |
| 0x10 | `TARGET_NOT_DETECTED` | No SWD/UART response from target |
| 0x11 | `TARGET_RDP_LOCKED` | STM32 read protection active |
| 0x12 | `TARGET_WRITE_FAIL` | Flash write rejected |
| 0x13 | `TARGET_VERIFY_FAIL` | Readback mismatch |
| 0x20 | `FW_NOT_FOUND` | `fw_id` not in local library |
| 0x21 | `FW_CORRUPT` | Local FW failed CRC check |
| 0x30 | `WIFI_DISCONNECTED` | Lost WiFi during sync |
| 0x31 | `SYNC_SERVER_UNREACHABLE` | Manifest URL not responding |
| 0x32 | `SYNC_STORAGE_FULL` | External flash out of space |
| 0x40 | `ABORT_BY_USER` | Job cancelled by `ABORT` cmd |
| 0xFF | `UNKNOWN_ERROR` | Catch-all |

---

## 7. Concurrency & Polling

### Polling cadence

| Bus state | Per-phost poll rate |
|---|---|
| All idle | 1 Hz |
| Any phost in active state (FLASHING / VERIFYING / SYNCING / ERASING) | 5 Hz for that phost, 1 Hz for idle others |

A full 10-phost transaction at 400 kHz takes ≈ 0.9 ms. Worst case (10 phosts
all flashing, 5 Hz each) = 45 ms/s = **4.5 % bus utilization**. Plenty of
headroom for re-polls on CRC failure.

### Parallel job model

```
t=0     master sends FLASH_FW to phost 0x10           (~1 ms)
t=1ms   master sends FLASH_FW to phost 0x11           (~1 ms)
…
t=10ms  master sends FLASH_FW to phost 0x19
        all 10 phosts now flashing target chips concurrently
        (each phost runs its own SWD/UART engine on its own CPU)
t=200ms master polls 0x10  → progress 18 %
t=400ms master polls 0x11  → progress 22 %
…
t=30s   first phost reports DONE
t=32s   last phost reports DONE
```

The I²C bus carries **commands and status only** — never firmware bulk data.
Phosts pull their own FW copies over WiFi (see `SYNC_LIBRARY`). This is why
parallel works even though the bus is electrically half-duplex one-master.

### Optional "Flash One" mode

Master MAY expose a sequential mode where only one phost runs at a time
(next phost waits for previous `DONE`). Useful for:

- Debug (isolate noise from one device)
- Power-constrained setups (avoid 10× simultaneous SWD current draw)

This is a master-side scheduling choice; the per-phost protocol is identical.

### Current-draw staggering

If the bench PSU can't handle 10 simultaneous flash currents, master MAY
stagger `FLASH_FW` sends by ~50 ms each instead of back-to-back. Jobs still
overlap (parallel), just shifted in start time.

### WiFi sync staggering

Triggering `SYNC_LIBRARY` on all 10 phosts at once will cause 10 ESP32-C3
clients to open HTTP(S) downloads against the same WiFi AP simultaneously.
Most consumer 2.4 GHz routers cap concurrent active sessions well below 10
— the result is packet loss, half-completed downloads, and spurious
`SYNC_SERVER_UNREACHABLE` errors.

Master MUST stagger sync starts. Recommended pattern:

- Start sync on **2 phosts at a time**
- Wait until both report `state ≠ SYNCING` (or 30 s elapsed) before
  launching the next pair
- Total wall time on a 10-phost / 5 MB-library sync ≈ 60–90 s

For weaker APs or larger libraries, drop to 1-at-a-time staggering.
Master MAY expose this as a UI setting `sync_concurrency` (default 2).

---

## 8. Discovery Flow

On master boot:

```
for addr in 0x10..0x19:
    send PING(addr) with 5 ms timeout
    if ACKed:
        send GET_INFO(addr)
        mark slave online, cache info
    else:
        mark slave offline
```

Total cold scan: 10 × ~5 ms = **50 ms** worst case.

After initial scan, master keeps PINGing offline addresses every 5 seconds
to detect phosts that come online (e.g. fresh provisioning, power-cycled).

---

## 9. Reliability

### Retries

If a transaction fails (NACK, CRC fail, timeout) master retries:

- Max 3 attempts per command
- 10 ms backoff between attempts
- On 3rd failure: mark phost offline, push `slave_offline` event up to PC,
  resume background re-discovery PINGs every 5 s

### Timeouts

| Operation | Timeout |
|---|---|
| `PING` | 5 ms |
| Any other command | 50 ms (allows for phost clock stretching during NVS write) |
| Full transaction with retries | 200 ms |

### Watchdog interaction

Phost firmware MUST feed its watchdog during long operations (flash, sync).
The I²C ISR uses clock stretching to defer responses while the main loop is
busy with target flashing — keep stretching to ≤ 10 ms per transaction so the
master's 50 ms timeout has margin.

### ESP-IDF I²C timeout register

ESP-IDF's I²C master driver has a hardware timeout register
(`I2C_TIME_OUT_REG`) whose default is below 10 ms on several chip variants
— short enough to misfire while a phost is legitimately clock-stretching
during an NVS write. Master firmware MUST raise it to at least 20 ms (2×
margin over the 10 ms stretch budget):

```c
// ESP-IDF v5.x, new i2c_master driver
i2c_master_bus_config_t bus_cfg = {
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .i2c_port = I2C_NUM_0,
    .scl_io_num = MASTER_SCL_GPIO,
    .sda_io_num = MASTER_SDA_GPIO,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = false,  // we use external 4.7 kΩ
};
i2c_master_bus_handle_t bus;
ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

// Bump per-device timeout when adding each phost
i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address  = 0x10,
    .scl_speed_hz    = 400000,
    .scl_wait_us     = 20000,   // 20 ms — raise above 10 ms stretch budget
};
```

Symptom of an undersized timeout: sporadic `ESP_ERR_TIMEOUT` from the
master driver only on NVS-writing commands (`SET_LABEL`,
`SET_TARGET_TYPE`, `SET_ADDRESS` during provisioning).

---

## 10. Provisioning a New Phost

When a fresh phost (factory state, no address) is wired to the bus, the
**Phost Setup** tab in FlashPorter walks the user through:

1. PC sends `provision` command to master via WebSocket
2. Master finds the first free slot in `0x10..0x19`
3. Master uses a **separate provisioning channel** (out-of-band, not the
   normal I²C bus addresses) — typically a default address `0x08` that
   factory phosts listen on
4. Master sends `SET_ADDRESS(target_addr)` on the provisioning channel
5. Phost writes new address to NVS, reboots
6. Phost rejoins bus at new address, responds to `PING` from master
7. Master pushes `slave_added` event to PC

Provisioning protocol details (factory address, address-set command) are
documented in firmware source, not this spec — they should never be needed
during normal operation.

> **⚠ Single-phost rule during provisioning:** the factory address `0x08` is
> shared by every un-provisioned phost. If two factory phosts are powered on
> the same bus simultaneously, both will ACK `0x08` and the assignment will
> collide (I²C arbitration may pick the wrong device, or both write the same
> NVS address). The Phost Setup UI MUST guide the operator to provision one
> physical phost at a time. Acceptable approaches, in order of preference:
>
> 1. **Per-socket power switch on the jig** — only one socket energized at a
>    time during the Provision step (best, hardware-enforced).
> 2. **Operator workflow rule** — UI shows "insert ONE fresh phost, then
>    click Provision" with a confirmation dialog; rely on operator discipline.
> 3. **Detection heuristic** — master sends a sentinel-write to `0x08` and
>    immediately reads back; if read returns inconsistent data across 3
>    attempts, abort with `PROVISION_COLLISION` error and instruct operator.
>
> This rule does not apply to normal operation — already-provisioned phosts
> use unique addresses in `0x10–0x19`.

---

## 11. Versioning

This protocol is version **1**. `GET_INFO.fw_major` indicates the protocol
version the phost firmware speaks. Master MUST refuse to use a phost with a
higher `fw_major` than itself (forward incompatibility is opt-in only).

Adding new command codes within the same major version is allowed — phost
returns `STATUS = 0x02 INVALID_CMD` for unknown codes, master treats this as
non-fatal.

---

## 12. Example Sessions

### Flash all phosts with the same FW

```
PC:                                                    Master:                                              Phost 0x10:
flash {addrs: ["0x10","0x11"], fw_id: 2}
                                                       FLASH_FW(0x10, 2)                                   ACK
                                                       FLASH_FW(0x11, 2)                                   …
                                                       (loop polling every 200 ms)
                                                       READ_STATUS(0x10) → state=FLASHING progress=15
                                                                                                            …
slave_progress {addr:"0x10", progress:15}
                                                       …                                                    state=DONE event_seq=12
slave_result {addr:"0x10", ok:true}
```

### Phost goes offline mid-flash

```
master polls 0x11 → no ACK
master retries × 3 → all fail
master marks 0x11 offline → push slave_offline event
master continues polling other phosts
master re-pings 0x11 every 5 s
… 30 s later 0x11 comes back → push slave_online event
```

### Sync library

```
PC: sync {addrs: ["all"], manifest_url: "https://..."}
master loops: SYNC_LIBRARY(0x10), SYNC_LIBRARY(0x11), …
master polls READ_STATUS each phost @ 5 Hz, watches bytes_done/bytes_total
forwards as sync_progress events to PC
```

---

## 13. Open Items

- **`index.txt` encryption:** PC-side FW library on Git is AES-128-CBC
  encrypted (see [git_sync.py](../toolAddFirmware/FlashPorter_Public/modules/git_sync.py)).
  Phost firmware will need the same key/IV to read the manifest. Key
  distribution strategy: stored in NVS at provisioning time (master pushes
  key via I²C `SET_AES_KEY` command — not yet specified, candidate for
  protocol v2).
- **Provisioning protocol** (factory address, address-set command) — to be
  specified in a follow-up doc once firmware skeleton lands.
- **Bus health metrics** — bit error rate, retry count per phost — could be
  exposed via a new `GET_BUS_STATS` command for diagnostics.
