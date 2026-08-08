"""
master_client_ws.py
===================
WebSocket implementation of MasterClient — the real PC ↔ Master backend.

See docs/master-api.md for the wire protocol. In short:
  * PC opens one WebSocket to ws://<master-ip>/ws and sends `hello`.
  * Master replies `welcome` + pushes `event` messages (no polling).
  * PC commands get a correlated `result` keyed by `id`.

Design:
  * A background thread runs the recv loop and keeps `_slaves`,
    `_statuses`, `_sync_statuses` caches fresh from pushed events.
  * get_slaves()/get_status()/get_sync_status() read those caches — instant,
    no network round-trip.
  * Command methods send a message and block on a per-id Event until the
    matching `result` arrives (or timeout).
  * On socket drop the recv loop reconnects with backoff and re-handshakes;
    the master's fresh `welcome` rebuilds the caches.

Requires the `websocket-client` package:  pip install websocket-client
"""

from __future__ import annotations

import json
import threading
import time
from dataclasses import replace
from typing import Optional

from .master_client import (
    MasterClient, SlaveInfo, SlaveStatus, SyncStatus,
    ProvisionResult, ProgressCallback, LogCallback,
)

try:
    import websocket  # websocket-client
except ImportError:  # pragma: no cover - optional dependency
    websocket = None


PROTOCOL_VERSION = 1
PING_INTERVAL_S = 15.0
PROVISION_TIMEOUT_S = 300.0


class WsMasterClient(MasterClient):
    """Real master backend over a single bidirectional WebSocket."""

    def __init__(self, token: str = "", timeout: float = 6.0):
        if websocket is None:
            raise RuntimeError(
                "ws backend needs the 'websocket-client' package — "
                "run: pip install websocket-client"
            )
        self._token = token
        self._timeout = timeout

        self._ws = None
        self._url: Optional[str] = None
        self._connected = False
        self._closing = False

        self._lock = threading.Lock()          # guards caches + _pending
        self._send_lock = threading.Lock()     # serialises ws.send()

        self._slaves: dict[int, SlaveInfo] = {}
        self._statuses: dict[int, SlaveStatus] = {}
        self._sync_statuses: dict[int, SyncStatus] = {}

        self._id_counter = 0
        self._pending: dict[int, dict] = {}     # id -> {"event", "result"}

        self._master_id = ""
        self._fw_version = ""
        self._welcome: Optional[dict] = None
        self._welcome_event = threading.Event()

        # One provisioning op at a time — callbacks fire from the recv thread.
        self._prov_progress_cb: Optional[ProgressCallback] = None
        self._prov_log_cb: Optional[LogCallback] = None
        self._prov_done: Optional[threading.Event] = None
        self._prov_result: Optional[ProvisionResult] = None

        self._recv_thread: Optional[threading.Thread] = None
        self._ping_thread: Optional[threading.Thread] = None

    # ─────────────────────────── addr helpers ───────────────────────────

    @staticmethod
    def _hex(addr: int) -> str:
        return f"0x{addr:02x}"

    @staticmethod
    def _addr(s: str) -> int:
        return int(s, 16)

    # ─────────────────────────── connection ───────────────────────────

    def connect(self, host: str, port: int = 80) -> dict:
        # Drop any previous socket first.
        self.disconnect()
        self._url = f"ws://{host}:{port}/ws"
        self._closing = False
        self._welcome = None
        self._welcome_event = threading.Event()

        if not self._open_socket():
            return {"ok": False, "error": f"could not connect to {self._url}"}

        self._recv_thread = threading.Thread(target=self._recv_loop, daemon=True)
        self._recv_thread.start()

        if not self._send_hello():
            return {"ok": False, "error": "handshake send failed"}

        if not self._welcome_event.wait(self._timeout):
            return {"ok": False, "error": "no welcome from master (timeout)"}
        if self._welcome is None:
            return {"ok": False, "error": "handshake rejected by master"}

        proto = self._welcome.get("protocol")
        if proto is not None and proto != PROTOCOL_VERSION:
            # Not fatal — additive changes are allowed — but surface it.
            pass

        # Prime status + sync caches (welcome only carries slave inventory).
        snap = self._command("get_status")
        if snap.get("ok"):
            self._apply_status_snapshot(snap)

        self._ping_thread = threading.Thread(target=self._ping_loop, daemon=True)
        self._ping_thread.start()

        return {
            "ok": True,
            "master_id": self._welcome.get("master_id", ""),
            "slaves_count": len(self._slaves),
        }

    def _open_socket(self) -> bool:
        headers = [f"X-Auth-Token: {self._token}"] if self._token else []
        try:
            self._ws = websocket.create_connection(
                self._url, timeout=self._timeout, header=headers,
            )
            return True
        except Exception:
            self._ws = None
            return False

    def _send_hello(self) -> bool:
        try:
            self._raw_send({
                "type": "hello", "id": self._next_id(),
                "token": self._token, "client": "FlashPorter 2.0",
            })
            return True
        except Exception:
            return False

    def is_connected(self) -> bool:
        return self._connected

    def disconnect(self) -> None:
        self._closing = True
        self._connected = False
        try:
            if self._ws is not None:
                self._ws.close()
        except Exception:
            pass
        self._ws = None

    # ─────────────────────────── recv loop ───────────────────────────

    def _recv_loop(self) -> None:
        backoff = 1.0
        while not self._closing:
            try:
                raw = self._ws.recv() if self._ws else None
            except Exception:
                if self._closing:
                    break
                self._connected = False
                time.sleep(backoff)
                backoff = min(backoff * 2, 30.0)
                if self._open_socket() and self._send_hello():
                    backoff = 1.0
                continue
            if raw is None:
                if self._closing:
                    break
                continue
            backoff = 1.0
            if not raw:
                continue
            try:
                msg = json.loads(raw)
            except (ValueError, TypeError):
                continue
            try:
                self._handle(msg)
            except Exception:
                # A malformed message must not kill the recv loop.
                continue

    def _ping_loop(self) -> None:
        while not self._closing:
            time.sleep(PING_INTERVAL_S)
            if self._closing or not self._connected:
                continue
            try:
                self._raw_send({"type": "ping", "id": self._next_id()})
            except Exception:
                pass

    # ─────────────────────────── message dispatch ───────────────────────────

    def _handle(self, msg: dict) -> None:
        mtype = msg.get("type")
        if mtype == "welcome":
            self._on_welcome(msg)
        elif mtype == "result":
            self._on_result(msg)
        elif mtype == "event":
            self._on_event(msg)
        # "pong" / "error" — nothing to do beyond keeping the socket alive.

    def _on_welcome(self, msg: dict) -> None:
        self._master_id = msg.get("master_id", "")
        self._fw_version = msg.get("fw_version", "")
        with self._lock:
            self._slaves.clear()
            self._statuses.clear()
            for s in msg.get("slaves", []):
                info = self._slave_from_json(s)
                self._slaves[info.addr] = info
                self._statuses[info.addr] = SlaveStatus(
                    addr=info.addr, busy=False, progress=0,
                    status_text="Ready" if info.online else "Offline",
                )
        self._connected = True
        self._welcome = msg
        self._welcome_event.set()

    def _on_result(self, msg: dict) -> None:
        rid = msg.get("id")
        with self._lock:
            slot = self._pending.get(rid)
            if slot is not None:
                slot["result"] = msg
                slot["event"].set()

    def _on_event(self, msg: dict) -> None:
        ev = msg.get("event")
        if ev == "slave_progress":
            addr = self._addr(msg["addr"])
            with self._lock:
                st = self._statuses.get(addr)
                if st is not None:
                    st.busy = msg.get("busy", st.busy)
                    st.progress = msg.get("progress", st.progress)
                    st.status_text = msg.get("status", st.status_text)
        elif ev == "slave_result":
            addr = self._addr(msg["addr"])
            result = msg.get("result")
            with self._lock:
                info = self._slaves.get(addr)
                if info is not None:
                    info.last_result = result
                    info.current_fw = msg.get("current_fw", info.current_fw)
                    info.last_duration_s = msg.get("duration_s")
                    info.error_text = msg.get("error")
                st = self._statuses.get(addr)
                if st is not None:
                    st.busy = False
                    if result == "ok":
                        st.progress = 100
                        st.status_text = "Ready"
                    else:
                        st.status_text = "✗ " + (msg.get("error") or "fail")
        elif ev == "sync_progress":
            addr = self._addr(msg["addr"])
            with self._lock:
                self._sync_statuses[addr] = SyncStatus(
                    addr=addr,
                    phase=msg.get("phase", "idle"),
                    files_done=msg.get("files_done", 0),
                    files_total=msg.get("files_total", 0),
                    bytes_done=msg.get("bytes_done", 0),
                    bytes_total=msg.get("bytes_total", 0),
                    current_file=msg.get("current_file", ""),
                    error=msg.get("error"),
                )
        elif ev == "slave_added":
            info = self._slave_from_json(msg.get("slave", {}))
            with self._lock:
                self._slaves[info.addr] = info
                self._statuses[info.addr] = SlaveStatus(
                    addr=info.addr, busy=False, progress=0,
                    status_text="Ready" if info.online else "Offline",
                )
        elif ev in ("slave_online", "slave_offline"):
            addr = self._addr(msg["addr"])
            with self._lock:
                info = self._slaves.get(addr)
                if info is not None:
                    info.online = (ev == "slave_online")
        elif ev == "provision_progress":
            if self._prov_progress_cb is not None:
                self._prov_progress_cb(msg.get("progress", 0),
                                       msg.get("status", ""))
            line = msg.get("line")
            if line and self._prov_log_cb is not None:
                self._prov_log_cb(line)
        elif ev == "provision_result":
            self._prov_result = ProvisionResult(
                ok=msg.get("ok", False),
                error=msg.get("error"),
                duration_s=msg.get("duration_s"),
            )
            if self._prov_done is not None:
                self._prov_done.set()

    # ─────────────────────────── parsing helpers ───────────────────────────

    def _slave_from_json(self, s: dict) -> SlaveInfo:
        return SlaveInfo(
            addr=self._addr(s["addr"]),
            label=s.get("label", ""),
            online=s.get("online", False),
            target_type=s.get("target_type", "no-target"),
            current_fw=s.get("current_fw"),
            last_result=s.get("last_result"),
            last_duration_s=s.get("last_duration_s"),
            error_text=s.get("error_text"),
        )

    def _apply_status_snapshot(self, snap: dict) -> None:
        with self._lock:
            for s in snap.get("status", []):
                addr = self._addr(s["addr"])
                self._statuses[addr] = SlaveStatus(
                    addr=addr, busy=s.get("busy", False),
                    progress=s.get("progress", 0),
                    status_text=s.get("status", "Ready"),
                )
            for s in snap.get("sync", []):
                addr = self._addr(s["addr"])
                self._sync_statuses[addr] = SyncStatus(
                    addr=addr, phase=s.get("phase", "idle"),
                    files_done=s.get("files_done", 0),
                    files_total=s.get("files_total", 0),
                    bytes_done=s.get("bytes_done", 0),
                    bytes_total=s.get("bytes_total", 0),
                    current_file=s.get("current_file", ""),
                    error=s.get("error"),
                )

    # ─────────────────────────── send + correlate ───────────────────────────

    def _next_id(self) -> int:
        with self._lock:
            self._id_counter += 1
            return self._id_counter

    def _raw_send(self, obj: dict) -> None:
        if self._ws is None:
            raise RuntimeError("socket not open")
        with self._send_lock:
            self._ws.send(json.dumps(obj))

    def _command(self, ctype: str, **params) -> dict:
        """Send a command, block until its `result`. Never raises —
        returns an {ok: false, error, code} dict on any failure."""
        if not self._connected or self._ws is None:
            return {"ok": False, "error": "not connected", "code": "NETWORK"}
        cid = self._next_id()
        evt = threading.Event()
        with self._lock:
            self._pending[cid] = {"event": evt, "result": None}
        msg = {"type": ctype, "id": cid}
        msg.update(params)
        try:
            self._raw_send(msg)
        except Exception as e:
            with self._lock:
                self._pending.pop(cid, None)
            return {"ok": False, "error": str(e), "code": "NETWORK"}
        if not evt.wait(self._timeout):
            with self._lock:
                self._pending.pop(cid, None)
            return {"ok": False, "error": "timeout waiting for result",
                    "code": "TIMEOUT"}
        with self._lock:
            slot = self._pending.pop(cid, None)
        return slot["result"] if slot and slot["result"] else {
            "ok": False, "error": "result lost", "code": "INTERNAL"}

    def _normalize_addrs(self, r: dict) -> dict:
        """Convert hex-string addrs in accepted/rejected/skipped back to ints
        so callers (NetFlash) see the same shape as the mock backend."""
        if not isinstance(r, dict):
            return r
        out = dict(r)
        for key in ("accepted", "skipped"):
            if key in out:
                out[key] = [self._addr(x) if isinstance(x, str) else x
                            for x in out[key]]
        if "rejected" in out:
            rej = []
            for x in out["rejected"]:
                if isinstance(x, dict):
                    rej.append(self._addr(x["addr"]))
                elif isinstance(x, str):
                    rej.append(self._addr(x))
                else:
                    rej.append(x)
            out["rejected"] = rej
        return out

    # ─────────────────────────── queries (cached) ───────────────────────────

    def get_slaves(self) -> list[SlaveInfo]:
        with self._lock:
            return [replace(s) for s in self._slaves.values()]

    def get_slave_fw_list(self, addr: int) -> list[dict]:
        r = self._command("get_firmware", addr=self._hex(addr))
        return r.get("firmware", []) if r.get("ok") else []

    def get_status(self) -> list[SlaveStatus]:
        with self._lock:
            return [replace(s) for s in self._statuses.values()]

    def get_sync_status(self) -> list[SyncStatus]:
        with self._lock:
            return [replace(s) for s in self._sync_statuses.values()]

    # ─────────────────────────── actions ───────────────────────────

    def flash_slave(self, addr: int, fw_id: str) -> dict:
        return self._normalize_addrs(self._command(
            "flash", jobs=[{"addr": self._hex(addr), "fw_id": fw_id}]))

    def flash_all(self, fw_per_slave: dict[int, str]) -> dict:
        jobs = [{"addr": self._hex(a), "fw_id": fw}
                for a, fw in fw_per_slave.items()]
        return self._normalize_addrs(self._command("flash", jobs=jobs))

    def erase_slave(self, addr: int) -> dict:
        return self._normalize_addrs(
            self._command("erase", addrs=[self._hex(addr)]))

    def erase_all(self) -> dict:
        return self._normalize_addrs(self._command("erase", addrs=[]))

    def reboot_slave(self, addr: int) -> dict:
        return self._normalize_addrs(
            self._command("reboot", addrs=[self._hex(addr)]))

    def reboot_all(self) -> dict:
        return self._normalize_addrs(self._command("reboot", addrs=[]))

    def sync_library(self, addrs: list[int], manifest_url: str) -> dict:
        return self._normalize_addrs(self._command(
            "sync",
            addrs=[self._hex(a) for a in addrs],
            manifest_url=manifest_url,
        ))

    def provision_slave(
        self,
        addr: int,
        label: str,
        progress_cb: ProgressCallback,
        log_cb: LogCallback,
    ) -> ProvisionResult:
        self._prov_progress_cb = progress_cb
        self._prov_log_cb = log_cb
        self._prov_result = None
        self._prov_done = threading.Event()
        try:
            r = self._command("provision", addr=self._hex(addr), label=label)
            if not r.get("ok"):
                return ProvisionResult(
                    ok=False, error=r.get("error", "provision rejected"))
            if not self._prov_done.wait(PROVISION_TIMEOUT_S):
                return ProvisionResult(ok=False, error="provision timed out")
            return self._prov_result or ProvisionResult(
                ok=False, error="no provision result")
        finally:
            self._prov_progress_cb = None
            self._prov_log_cb = None
            self._prov_done = None
