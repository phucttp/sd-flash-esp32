# Master ↔ PC WebSocket API

Communication contract between the **FlashPorter PC tool** and the
**ESP32-C3 master**. One persistent WebSocket carries everything: the PC
sends commands, the master pushes events. No polling.

- **Protocol version:** 1
- **Transport:** WebSocket over LAN (`ws://`, no TLS)
- **Encoding:** UTF-8 JSON text frames, one JSON object per frame
- **Roles:** PC = WebSocket client (connects out) · Master = WebSocket server

The PC only needs the master's IP on the shared WiFi. After the PC opens the
connection, both sides send freely over that same socket — the master never
opens a connection back, so the PC needs no inbound port / firewall rule.

---

## 1. Connection & Handshake

| Step | Direction | Detail |
|------|-----------|--------|
| 1 | PC → Master | WebSocket upgrade `GET ws://<master-ip>:80/ws` |
| 2 | PC → Master | `hello` message (carries auth token) |
| 3 | Master → PC | `welcome` message (master info + full slave snapshot) |
| 4 | both | normal command / event traffic |

The master accepts a single active PC connection. A second `hello` from a new
socket replaces the old one (the stale socket is closed).

---

## 2. Authentication

A shared token gates the connection. The token is configured on the master
(stored in NVS) and in the PC tool (`settings.json` → `master_token`).

The PC sends the token **both** ways for flexibility — pick one on the
firmware side:

- **WS upgrade header:** `X-Auth-Token: <token>` on the HTTP upgrade request.
- **`hello` message field:** `token` (see below).

If the token is missing or wrong, the master replies with an `error` message
(`code: "UNAUTHORIZED"`) and closes the socket. An empty configured token on
the master disables the check (LAN-trusted mode).

---

## 3. Message Envelope

Every frame is a JSON object with a `type` field.

### Command (PC → Master)

```json
{ "type": "<command>", "id": <int>, ...params }
```

- `id` — monotonic integer chosen by the PC, used to correlate the reply.

### Result (Master → PC)

```json
{ "type": "result", "id": <int>, "ok": <bool>, ...data }
```

- `id` — echoes the command's `id`.
- On failure: `"ok": false, "error": "<message>", "code": "<CODE>"`.

### Event (Master → PC, unsolicited)

```json
{ "type": "event", "event": "<event-name>", ...data }
```

- No `id` — events are pushes, not replies.

### Protocol error (Master → PC)

```json
{ "type": "error", "error": "<message>", "code": "<CODE>" }
```

Sent for malformed JSON, unknown `type`, or auth failure.

---

## 4. Data Types

I2C addresses are **hex strings** (`"0x10"`) everywhere in the wire format.

### SlaveInfo

| Field | Type | Notes |
|-------|------|-------|
| `addr` | string | I2C address, e.g. `"0x10"` |
| `label` | string | user-editable name |
| `online` | bool | responded to last I2C probe |
| `target_type` | string | `"esp32"` / `"stm32f1"` / `"stm32f4"` / `"no-target"` |
| `current_fw` | string\|null | FW id last flashed into the target |
| `last_result` | string\|null | `"ok"` / `"fail"` / `null` |
| `last_duration_s` | number\|null | last flash duration |
| `error_text` | string\|null | short error for UI |

### SlaveStatus

| Field | Type | Notes |
|-------|------|-------|
| `addr` | string | I2C address |
| `busy` | bool | flash/erase in progress |
| `progress` | int | 0–100 |
| `status` | string | `"Ready"` / `"Flashing"` / `"Erasing"` / `"✗ no target"` |

### SyncStatus

| Field | Type | Notes |
|-------|------|-------|
| `addr` | string | I2C address |
| `phase` | string | `idle` / `connecting` / `downloading` / `verifying` / `storing` / `done` / `failed` |
| `files_done` | int | files already up-to-date or downloaded |
| `files_total` | int | files in the manifest |
| `bytes_done` | int | |
| `bytes_total` | int | |
| `current_file` | string | e.g. `"EMC32/v1.3"`, `""` when idle/done |
| `error` | string\|null | set when `phase == "failed"` |

### FirmwareEntry

| Field | Type | Notes |
|-------|------|-------|
| `id` | string | `"<device_type>_<version>"`, e.g. `"EMC32_v1.2"` |
| `display` | string | human-readable, e.g. `"EMC32 v1.2"` |
| `device_type` | string | |
| `version` | string | |
| `size` | int | bytes |
| `md5` | string | integrity hash |

---

## 5. Commands (PC → Master)

### `hello`

Handshake. Must be the first message.

```json
{ "type": "hello", "id": 1, "token": "<token>", "client": "FlashPorter 2.0" }
```

Reply: `welcome` (see §7) — not a `result`.

### `get_slaves`

Request the full slave inventory.

```json
{ "type": "get_slaves", "id": 2 }
```

```json
{ "type": "result", "id": 2, "ok": true, "slaves": [ <SlaveInfo>, ... ] }
```

### `get_firmware`

List FW versions stored in one slave's external memory.

```json
{ "type": "get_firmware", "id": 3, "addr": "0x10" }
```

```json
{ "type": "result", "id": 3, "ok": true,
  "addr": "0x10", "firmware": [ <FirmwareEntry>, ... ] }
```

### `get_status`

Optional one-shot snapshot (events push the same data continuously).

```json
{ "type": "get_status", "id": 4 }
```

```json
{ "type": "result", "id": 4, "ok": true,
  "status": [ <SlaveStatus>, ... ],
  "sync": [ <SyncStatus>, ... ] }
```

### `flash`

Flash one or more targets with a stored FW version. **Async** — the result
only confirms acceptance; watch `slave_progress` / `slave_result` events.

```json
{ "type": "flash", "id": 5,
  "jobs": [ { "addr": "0x10", "fw_id": "EMC32_v1.2" },
            { "addr": "0x11", "fw_id": "STM_v1.0" } ] }
```

```json
{ "type": "result", "id": 5, "ok": true,
  "accepted": ["0x10", "0x11"],
  "rejected": [ { "addr": "0x12", "error": "no target connected" } ] }
```

### `erase`

Erase the target of each listed slave. Empty / omitted `addrs` = all online.

```json
{ "type": "erase", "id": 6, "addrs": ["0x10", "0x11"] }
```

```json
{ "type": "result", "id": 6, "ok": true, "accepted": ["0x10", "0x11"] }
```

### `reboot`

Reboot listed slaves. Empty / omitted `addrs` = all online.

```json
{ "type": "reboot", "id": 7, "addrs": ["0x10"] }
```

### `sync`

Tell listed slaves to mirror the full FW library. Each slave fetches the
manifest from `manifest_url`, diffs it against its external memory, and
downloads missing / changed files over its own WiFi. **Async** — watch
`sync_progress` events.

```json
{ "type": "sync", "id": 8,
  "addrs": ["0x10", "0x11", "0x13"],
  "manifest_url": "https://raw.githubusercontent.com/user/repo/main/manifest.json" }
```

```json
{ "type": "result", "id": 8, "ok": true,
  "accepted": ["0x10", "0x11", "0x13"],
  "skipped":  ["0x12"] }
```

`skipped` = offline or already syncing.

### `provision`

Provision a freshly-plugged phost on the master's dedicated UART slot. The
master flashes `phost_main` **from its own internal partition** (no FW upload
from the PC), then writes the I2C address into the slave's NVS. **Async** —
watch `provision_progress` / `provision_result` events.

```json
{ "type": "provision", "id": 9, "addr": "0x14", "label": "Phost-5" }
```

```json
{ "type": "result", "id": 9, "ok": true, "accepted": true }
```

### `set_label`

Rename a slave (persisted in master NVS).

```json
{ "type": "set_label", "id": 10, "addr": "0x10", "label": "Station-A" }
```

### `ping`

Liveness check. The PC sends one every ~15 s.

```json
{ "type": "ping", "id": 11 }
```

Reply: `{ "type": "pong" }` (no `id`).

---

## 6. Command summary

| `type` | Params | Async | Watch events |
|--------|--------|-------|--------------|
| `hello` | `token`, `client` | no | → `welcome` |
| `get_slaves` | — | no | — |
| `get_firmware` | `addr` | no | — |
| `get_status` | — | no | — |
| `flash` | `jobs[]` | yes | `slave_progress`, `slave_result` |
| `erase` | `addrs[]` | yes | `slave_progress`, `slave_result` |
| `reboot` | `addrs[]` | yes | `slave_progress` |
| `sync` | `addrs[]`, `manifest_url` | yes | `sync_progress` |
| `provision` | `addr`, `label` | yes | `provision_progress`, `provision_result` |
| `set_label` | `addr`, `label` | no | — |
| `ping` | — | no | → `pong` |

---

## 7. Events (Master → PC)

### `welcome`

Reply to `hello`. Carries master info and the full slave snapshot so the PC
can render immediately without a follow-up `get_slaves`.

```json
{ "type": "welcome",
  "master_id": "master-7CDFA1E51488",
  "fw_version": "1.0.0",
  "protocol": 1,
  "slaves": [ <SlaveInfo>, ... ] }
```

### `slave_progress`

Flash / erase progress for one slave. Pushed whenever `progress` or `status`
changes.

```json
{ "type": "event", "event": "slave_progress",
  "addr": "0x10", "busy": true, "progress": 45, "status": "Flashing" }
```

### `slave_result`

A flash / erase finished.

```json
{ "type": "event", "event": "slave_result",
  "addr": "0x10", "result": "ok",
  "current_fw": "EMC32_v1.2", "duration_s": 4.1, "error": null }
```

### `sync_progress`

Library-sync progress for one slave. Pushed on each phase / counter change.

```json
{ "type": "event", "event": "sync_progress",
  "addr": "0x11", "phase": "downloading",
  "files_done": 3, "files_total": 5,
  "bytes_done": 1531904, "bytes_total": 2439000,
  "current_file": "EMC32/v1.3", "error": null }
```

### `provision_progress`

A provisioning step advanced. `line` is an optional log line for the UI.

```json
{ "type": "event", "event": "provision_progress",
  "progress": 55, "status": "Writing phost_main.bin...",
  "line": "> master: write 100%" }
```

### `provision_result`

Provisioning finished. On success the new slave also triggers `slave_added`.

```json
{ "type": "event", "event": "provision_result",
  "ok": true, "addr": "0x14", "label": "Phost-5",
  "duration_s": 5.0, "error": null }
```

### `slave_added`

A newly provisioned slave joined the I2C bus.

```json
{ "type": "event", "event": "slave_added", "slave": <SlaveInfo> }
```

### `slave_online` / `slave_offline`

A known slave appeared / disappeared on the I2C bus.

```json
{ "type": "event", "event": "slave_online",  "addr": "0x12" }
{ "type": "event", "event": "slave_offline", "addr": "0x12" }
```

---

## 8. Reconnection & Heartbeat

- The PC sends `ping` every **15 s**; expects `pong` within **30 s**.
- On socket drop or missing `pong`, the PC reconnects with backoff
  (1 s, 2 s, 4 s … capped at 30 s) and re-sends `hello`.
- After reconnect the master sends a fresh `welcome` with current slave
  state — the PC replaces its cache wholesale, so no event is "lost".
- Long operations (flash / sync / provision) continue running on the master
  across a PC disconnect; their progress is reflected again once the PC
  reconnects and receives the next events.

---

## 9. Error Codes

| `code` | Meaning |
|--------|---------|
| `UNAUTHORIZED` | missing / wrong token |
| `BAD_REQUEST` | malformed JSON or missing required field |
| `UNKNOWN_TYPE` | unrecognised `type` |
| `UNKNOWN_SLAVE` | no slave at the given address |
| `SLAVE_OFFLINE` | slave is not on the bus |
| `SLAVE_BUSY` | slave already running an operation |
| `NO_TARGET` | slave has no target device connected |
| `FW_NOT_STORED` | `fw_id` not in the slave's library (sync first) |
| `MASTER_BUSY` | master cannot accept the job right now |
| `INTERNAL` | unexpected master-side error |

---

## 10. Example Session

```text
PC  → {"type":"hello","id":1,"token":"abc123","client":"FlashPorter 2.0"}
M   → {"type":"welcome","master_id":"master-7CDFA1E51488","fw_version":"1.0.0",
       "protocol":1,"slaves":[...]}

PC  → {"type":"sync","id":2,"addrs":["0x10","0x11"],"manifest_url":"https://..."}
M   → {"type":"result","id":2,"ok":true,"accepted":["0x10","0x11"],"skipped":[]}
M   → {"type":"event","event":"sync_progress","addr":"0x10","phase":"downloading",
       "files_done":1,"files_total":5,...}
M   → {"type":"event","event":"sync_progress","addr":"0x10","phase":"done",
       "files_done":5,"files_total":5,...}

PC  → {"type":"flash","id":3,"jobs":[{"addr":"0x10","fw_id":"EMC32_v1.2"}]}
M   → {"type":"result","id":3,"ok":true,"accepted":["0x10"],"rejected":[]}
M   → {"type":"event","event":"slave_progress","addr":"0x10","busy":true,
       "progress":50,"status":"Flashing"}
M   → {"type":"event","event":"slave_result","addr":"0x10","result":"ok",
       "current_fw":"EMC32_v1.2","duration_s":4.1,"error":null}

PC  → {"type":"ping","id":4}
M   → {"type":"pong"}
```

---

## 11. Versioning

The `protocol` integer in `welcome` is bumped on any breaking change. The PC
tool checks it on connect and warns if it does not match the version it was
built for. Additive changes (new optional fields, new event names) do **not**
bump `protocol` — clients must ignore unknown fields and events.
