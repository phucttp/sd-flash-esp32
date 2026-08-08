# Master API — Usage Guide

Practical guide for working with the PC ↔ Master WebSocket API.
For the exact message formats, see the contract in
[`master-api.md`](./master-api.md) — this guide shows **how to use them**.

There are two sides:

- **PC side** — already implemented as `WsMasterClient` in the FlashPorter
  tool. You mostly just switch a setting.
- **Master side** — ESP32-C3 firmware that must be built to match the
  contract. Most of this guide is for that.

---

## 1. Mental model

One WebSocket carries everything:

```
            ws://<master-ip>/ws
   PC  ──────────────────────────────►  Master
        commands  (PC asks)
   PC  ◄──────────────────────────────  Master
        results   (reply to a command, matched by `id`)
        events    (master pushes — no request needed)
```

- A **command** always gets one **result** with the same `id`.
- An **event** is pushed any time — flash progress, a slave coming online,
  sync finishing. The PC never polls.
- Long jobs (flash / sync / provision) are **async**: the result only says
  "accepted", real progress arrives as events.

The PC keeps a local cache updated from events, so reading slave state is
instant and offline-safe.

---

## 2. PC side — using `WsMasterClient`

### Switch the tool to the real backend

Edit `toolAddFirmware/FlashPorter_Public/settings.json`:

```json
{
  "netflash_backend": "ws",
  "master_ip": "192.168.1.50",
  "master_token": "your-shared-secret"
}
```

`"netflash_backend": "mock"` (default) keeps the fake-slave simulator for UI
work without hardware.

### Using the client directly

```python
from modules.master_client import make_master_client

mc = make_master_client("ws", token="your-shared-secret")

r = mc.connect("192.168.1.50")          # opens WS, handshakes
if not r["ok"]:
    print("connect failed:", r["error"])

for s in mc.get_slaves():               # instant — reads cache
    print(hex(s.addr), s.label, s.online)

# async — returns after the master accepts the job
mc.sync_library([0x10, 0x11], "https://.../manifest.json")

# watch progress via the cache (events keep it fresh)
for st in mc.get_sync_status():
    print(hex(st.addr), st.phase, f"{st.files_done}/{st.files_total}")
```

`get_slaves()`, `get_status()`, `get_sync_status()` never hit the network —
they return the event-fed cache. `flash_*`, `erase_*`, `sync_library`,
`provision_slave` send a command and wait for the result.

Requires `pip install websocket-client`.

---

## 3. Workflow walkthroughs

### Connect

```text
PC  → hello {token}
M   → welcome {master_id, fw_version, protocol, slaves[]}
PC  → get_status        (client primes its status/sync caches)
M   → result {status[], sync[]}
```

After this the PC has the full picture and only listens for events.

### Sync library

```text
PC  → sync {addrs, manifest_url}
M   → result {accepted, skipped}          ← job accepted, not done
M   → event sync_progress {phase:"connecting"}
M   → event sync_progress {phase:"downloading", files_done:1, ...}
M   → event sync_progress {phase:"done", files_done:5, files_total:5}
```

Each slave fetches `manifest_url`, diffs it against its external memory, and
downloads only what is missing. A re-sync of an up-to-date slave goes
straight to `phase:"done"`.

### Flash

```text
PC  → flash {jobs:[{addr, fw_id}]}
M   → result {accepted, rejected}
M   → event slave_progress {addr, progress:50, status:"Flashing"}
M   → event slave_result   {addr, result:"ok", current_fw, duration_s}
```

`fw_id` must already be in the slave's library — sync first, or the master
rejects with `FW_NOT_STORED`.

### Provision a new phost

```text
PC  → provision {addr, label}
M   → result {accepted:true}
M   → event provision_progress {progress:15, status:"Erasing...", line:"..."}
M   → event provision_progress {progress:95, status:"Writing I2C addr..."}
M   → event provision_result   {ok:true, addr, label, duration_s}
M   → event slave_added        {slave:{...}}
```

The master flashes `phost_main` from its **own internal partition** — the PC
sends no firmware. On success the new slave joins the bus and `slave_added`
lets the PC show it without a reconnect.

---

## 4. The manifest file

`sync` points slaves at a `manifest.json` (served from a Git raw URL or any
HTTP host). It lists the whole firmware library:

```json
{
  "version": 1,
  "generated_at": "2026-05-16T14:30:00Z",
  "base_url": "https://raw.githubusercontent.com/user/repo/main/firmware/",
  "firmware": [
    { "device_type": "EMC32", "version": "v1.2",
      "file": "EMC32/v1.2/firmware.bin", "size": 642000, "md5": "a1b2c3" },
    { "device_type": "EMC32", "version": "v1.3",
      "file": "EMC32/v1.3/firmware.bin", "size": 648000, "md5": "d4e5f6" }
  ]
}
```

A slave downloads each entry as `<base_url><file>`, verifies `md5`, and
stores it as `<device_type>_<version>.bin`. The FW id used by `flash` is
`<device_type>_<version>` (e.g. `EMC32_v1.2`).

> The PC tool will generate `manifest.json` during "Sync to Git" in the
> Firmware Manager. Until that is wired, host the file by hand.

---

## 5. Master side — implementation guide

Target: ESP-IDF `esp_http_server` with WebSocket support.

### 5.1 Register the WS endpoint

```c
httpd_uri_t ws = {
    .uri       = "/ws",
    .method    = HTTP_GET,
    .handler   = ws_handler,
    .is_websocket = true,
};
httpd_register_uri_handler(server, &ws);
```

### 5.2 Check the token

On the first frame (or the HTTP upgrade headers) read `X-Auth-Token` /
the `hello` message's `token`. If it does not match NVS, send an
`error {code:"UNAUTHORIZED"}` and close. An empty NVS token disables the
check (LAN-trusted mode).

### 5.3 The golden rule — never block a handler

`ws_handler` runs on the httpd task. Flashing a target or downloading
firmware takes seconds to minutes. **Do not do that work inside the
handler** — it would freeze the socket and trip timeouts.

Instead:

```c
// inside ws_handler, after parsing a "flash" command:
validate(job);                       // fast checks only
xQueueSend(flash_queue, &job, 0);     // hand off to a worker task
send_result(req, id, /*ok=*/true);    // reply immediately
return ESP_OK;                        // handler done in milliseconds
```

A dedicated FreeRTOS task drains `flash_queue`, does the real work, and
emits `slave_progress` / `slave_result` events as it goes.

### 5.4 Pushing events from a worker task

A worker task cannot call `httpd_ws_send_frame` directly — that API must run
on the httpd task. Marshal it:

```c
// remember the connected client when it handshakes:
int g_client_fd = httpd_req_to_sockfd(req);

// from a worker task, to push an event:
httpd_queue_work(server, send_event_cb, event_payload);

// send_event_cb runs on the httpd task:
void send_event_cb(void *arg) {
    httpd_ws_frame_t f = { .type = HTTPD_WS_TYPE_TEXT,
                           .payload = arg, .len = strlen(arg) };
    httpd_ws_send_frame_async(server, g_client_fd, &f);
}
```

Keep `g_client_fd` updated on every `hello`; clear it when the socket drops.

### 5.5 Per-slave state machine

Each slave needs a tiny state record the master updates and reports:

```
flash:   idle → flashing(progress%) → done/failed   → slave_result
sync:    idle → connecting → downloading → verifying → storing → done/failed
provision: idle → erasing → writing → verifying → addr_write → done/failed
```

Emit an event whenever a field changes — the PC mirrors it. Snapshot model:
the master only has to hold the **current** state, no job history.

### 5.6 Reconnect

When the PC's socket drops, the master keeps running jobs. On the next
`hello`, send a fresh `welcome` with current slave state — the PC rebuilds
its cache wholesale, so nothing is lost. Only one PC connection is active;
a new `hello` replaces the old socket.

---

## 6. Testing without hardware

Use the **mock backend** — set `"netflash_backend": "mock"`. It simulates
4 slaves, a 5-file library, flash/sync/provision with realistic progress and
a 10% random failure rate. The whole UI and every workflow are exercisable
with no master, no firmware.

When the firmware is ready, flip the setting to `"ws"` — no UI code changes,
because `WsMasterClient` and `MockMasterClient` implement the same
`MasterClient` interface.

---

## 7. Troubleshooting

| Symptom | Likely cause |
|---------|--------------|
| `connect` returns `no welcome (timeout)` | master not running, wrong IP/port, or it never sent `welcome` after `hello` |
| Socket closes right after connect | token mismatch — check `master_token` vs master NVS |
| Commands time out | master handler is blocking (see §5.3) — it must reply fast |
| Progress never updates | worker task not emitting events, or `g_client_fd` stale |
| Flash rejected `FW_NOT_STORED` | slave has not synced that firmware yet — run sync first |
| New provisioned slave missing in NetFlash | master did not send `slave_added` (the PC also re-detects via status, but the event is the clean path) |
| `ws backend needs 'websocket-client'` | run `pip install websocket-client` |

---

## 8. Firmware implementation checklist

- [ ] `/ws` endpoint registered with `is_websocket = true`
- [ ] Token check on connect (`X-Auth-Token` or `hello.token`)
- [ ] `hello` → `welcome` with `protocol`, `master_id`, full `slaves[]`
- [ ] Every command replies with a `result` carrying the same `id`
- [ ] `flash` / `sync` / `provision` hand off to worker tasks — handler never blocks
- [ ] Worker tasks push events via `httpd_queue_work` + `httpd_ws_send_frame_async`
- [ ] Per-slave state machine for flash / sync / provision
- [ ] `slave_online` / `slave_offline` on I2C probe changes
- [ ] `slave_added` after a successful provision
- [ ] `ping` → `pong`
- [ ] Fresh `welcome` on reconnect
- [ ] Errors use the `error` message + a `code` from §9 of `master-api.md`
