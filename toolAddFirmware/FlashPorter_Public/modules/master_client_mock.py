"""
master_client_mock.py
=====================
Fake MasterClient with 4 simulated slaves + progress threads.
Use during UI development before ESP32-C3 master firmware is ready.

To activate: set `"netflash_backend": "mock"` in settings.json (default).
"""

from __future__ import annotations

import random
import threading
import time
from dataclasses import asdict

from .master_client import (
    MasterClient, SlaveInfo, SlaveStatus, SyncStatus,
    ProvisionResult, ProgressCallback, LogCallback,
)


class MockMasterClient(MasterClient):
    """4 fake slaves with simulated I2C state + progress threads."""

    DEFAULT_SLAVES = [
        # (addr, label, target_type, online)
        (0x10, "Phost-1", "esp32",     True),
        (0x11, "Phost-2", "stm32f1",   True),
        (0x12, "Phost-3", "no-target", False),
        (0x13, "Phost-4", "esp32",     True),
    ]

    # Full FW library hosted on the server. Every slave mirrors ALL of it
    # into its external memory via sync_library(). Each entry mimics one
    # manifest.json record.
    SERVER_LIBRARY = [
        {"id": "EMC32_v1.2",  "display": "EMC32 v1.2",
         "device_type": "EMC32",       "version": "v1.2", "size": 642_000,  "md5": "a1b2c3"},
        {"id": "EMC32_v1.3",  "display": "EMC32 v1.3 (beta)",
         "device_type": "EMC32",       "version": "v1.3", "size": 648_000,  "md5": "d4e5f6"},
        {"id": "STM_v0.9",    "display": "STM app v0.9",
         "device_type": "STM-app",     "version": "v0.9", "size": 128_000,  "md5": "112233"},
        {"id": "STM_v1.0",    "display": "STM app v1.0",
         "device_type": "STM-app",     "version": "v1.0", "size": 131_000,  "md5": "445566"},
        {"id": "Sensor_v2.1", "display": "Sensor-node v2.1",
         "device_type": "Sensor-node", "version": "v2.1", "size": 890_000,  "md5": "778899"},
    ]

    # What each slave already has stored before the first sync — drives an
    # incremental-sync demo (only missing files get "downloaded").
    INITIAL_STORED = {
        0x10: {"EMC32_v1.2"},                       # missing 4
        0x11: {"STM_v0.9", "STM_v1.0"},             # missing 3
        0x12: set(),                                # offline — nothing
        0x13: {e["id"] for e in SERVER_LIBRARY},    # already fully synced
    }

    def __init__(self, fail_rate: float = 0.10, flash_duration_s: float = 4.0):
        self._connected = False
        self._fail_rate = fail_rate
        self._flash_duration = flash_duration_s
        self._lock = threading.Lock()

        self._slaves: dict[int, SlaveInfo] = {
            addr: SlaveInfo(
                addr=addr, label=label, online=online,
                target_type=target,
                current_fw=None, last_result=None,
            )
            for addr, label, target, online in self.DEFAULT_SLAVES
        }
        # Stored FW pool per slave = subset of SERVER_LIBRARY held locally.
        self._fw_pools: dict[int, list[dict]] = {
            addr: [dict(e) for e in self.SERVER_LIBRARY
                   if e["id"] in self.INITIAL_STORED.get(addr, set())]
            for addr in self._slaves
        }
        self._statuses: dict[int, SlaveStatus] = {
            addr: SlaveStatus(addr=addr, busy=False, progress=0,
                              status_text="Ready" if info.online else "Offline")
            for addr, info in self._slaves.items()
        }
        # Library-sync state per slave.
        self._sync_statuses: dict[int, SyncStatus] = {
            addr: self._fresh_sync_status(addr)
            for addr in self._slaves
        }

    # --- handshake ---

    def connect(self, host: str, port: int = 80) -> dict:
        time.sleep(0.3)
        self._connected = True
        return {
            "ok": True,
            "master_id": f"mock-master@{host}",
            "slaves_count": len(self._slaves),
        }

    def is_connected(self) -> bool:
        return self._connected

    def disconnect(self) -> None:
        self._connected = False

    # --- queries ---

    def get_slaves(self) -> list[SlaveInfo]:
        with self._lock:
            return [SlaveInfo(**asdict(s)) for s in self._slaves.values()]

    def get_slave_fw_list(self, addr: int) -> list[dict]:
        with self._lock:
            return [dict(e) for e in self._fw_pools.get(addr, [])]

    def get_sync_status(self) -> list[SyncStatus]:
        with self._lock:
            return [
                SyncStatus(**asdict(s)) for s in self._sync_statuses.values()
            ]

    def get_status(self) -> list[SlaveStatus]:
        with self._lock:
            return [
                SlaveStatus(addr=s.addr, busy=s.busy,
                            progress=s.progress, status_text=s.status_text)
                for s in self._statuses.values()
            ]

    # --- actions ---

    def flash_slave(self, addr: int, fw_id: str) -> dict:
        slave = self._slaves.get(addr)
        if slave is None:
            return {"ok": False, "error": "unknown slave"}
        if not slave.online:
            return {"ok": False, "error": "offline"}
        if slave.target_type == "no-target":
            return {"ok": False, "error": "no target connected"}
        if fw_id not in {f["id"] for f in self._fw_pools.get(addr, [])}:
            return {"ok": False, "error": f"fw {fw_id} not in slave's library"}

        threading.Thread(
            target=self._simulate_flash, args=(addr, fw_id), daemon=True
        ).start()
        return {"ok": True, "accepted": True}

    def flash_all(self, fw_per_slave: dict[int, str]) -> dict:
        accepted, rejected = [], []
        for addr, fw_id in fw_per_slave.items():
            r = self.flash_slave(addr, fw_id)
            (accepted if r.get("ok") else rejected).append(addr)
        return {"ok": True, "accepted": accepted, "rejected": rejected}

    def erase_slave(self, addr: int) -> dict:
        slave = self._slaves.get(addr)
        if slave is None or not slave.online:
            return {"ok": False, "error": "offline"}
        threading.Thread(
            target=self._simulate_erase, args=(addr,), daemon=True
        ).start()
        return {"ok": True, "accepted": True}

    def erase_all(self) -> dict:
        for addr, slave in self._slaves.items():
            if slave.online:
                self.erase_slave(addr)
        return {"ok": True}

    def reboot_slave(self, addr: int) -> dict:
        slave = self._slaves.get(addr)
        if slave is None or not slave.online:
            return {"ok": False, "error": "offline"}
        with self._lock:
            self._statuses[addr].status_text = "Rebooting..."
        threading.Thread(
            target=self._simulate_reboot, args=(addr,), daemon=True
        ).start()
        return {"ok": True}

    def reboot_all(self) -> dict:
        for addr, slave in self._slaves.items():
            if slave.online:
                self.reboot_slave(addr)
        return {"ok": True}

    # --- simulation helpers ---

    def _set_status(self, addr: int, **kwargs) -> None:
        with self._lock:
            st = self._statuses[addr]
            for k, v in kwargs.items():
                setattr(st, k, v)

    def _simulate_flash(self, addr: int, fw_id: str) -> None:
        start = time.time()
        self._set_status(addr, busy=True, progress=0, status_text="Flashing")
        steps = 20
        will_fail = random.random() < self._fail_rate
        fail_at = random.randint(5, 18) if will_fail else None

        for i in range(1, steps + 1):
            if will_fail and i == fail_at:
                with self._lock:
                    self._statuses[addr].busy = False
                    self._statuses[addr].progress = (fail_at * 100) // steps
                    self._statuses[addr].status_text = "✗ Flash failed"
                    self._slaves[addr].last_result = "fail"
                    self._slaves[addr].last_duration_s = time.time() - start
                    self._slaves[addr].error_text = "SWD verify mismatch"
                return
            self._set_status(addr, progress=(i * 100) // steps)
            time.sleep(self._flash_duration / steps)

        duration = time.time() - start
        with self._lock:
            self._statuses[addr].busy = False
            self._statuses[addr].progress = 100
            self._statuses[addr].status_text = "Ready"
            self._slaves[addr].current_fw = fw_id
            self._slaves[addr].last_result = "ok"
            self._slaves[addr].last_duration_s = duration
            self._slaves[addr].error_text = None

    def _simulate_erase(self, addr: int) -> None:
        self._set_status(addr, busy=True, progress=0, status_text="Erasing")
        for i in range(1, 11):
            self._set_status(addr, progress=i * 10)
            time.sleep(0.15)
        with self._lock:
            self._statuses[addr].busy = False
            self._statuses[addr].progress = 0
            self._statuses[addr].status_text = "Ready"
            self._slaves[addr].current_fw = None
            self._slaves[addr].last_result = None
            self._slaves[addr].error_text = None

    def _simulate_reboot(self, addr: int) -> None:
        time.sleep(0.8)
        self._set_status(addr, busy=False, progress=0, status_text="Ready")

    # --- provisioning ---

    PROVISION_STEPS = [
        (5,   "Master: detecting slave on UART slot...",
              "> master: handshake OK, ESP32-C3 (4MB flash)"),
        (15,  "Master: erasing slave app partition...",
              "> master: erase 0x10000..0x110000 done"),
        (25,  "Master: erasing slave NVS partition...",
              "> master: erase NVS done"),
        (45,  "Master: writing phost_main.bin (656KB)...",
              "> master: write 50%"),
        (70,  "Master: writing phost_main.bin (656KB)...",
              "> master: write 100%"),
        (80,  "Master: verifying flash (md5)...",
              "> master: verify OK"),
        (88,  "Master: rebooting slave into setup mode...",
              "> master: slave boot, NVS empty → setup mode"),
        (95,  "Master: sending SET_I2C_ADDR via UART...",
              "> master: ACK from slave, addr written to NVS"),
        (100, "Done — slave ready on I2C bus",
              "> master: provisioning complete"),
    ]

    def provision_slave(
        self,
        addr: int,
        label: str,
        progress_cb: ProgressCallback,
        log_cb: LogCallback,
    ) -> ProvisionResult:
        if not self._connected:
            return ProvisionResult(ok=False, error="master not connected")
        if addr in self._slaves and self._slaves[addr].online:
            return ProvisionResult(
                ok=False,
                error=f"addr 0x{addr:02x} already in use (online)",
            )

        start = time.time()
        will_fail = random.random() < self._fail_rate
        fail_at_idx = random.choice([1, 3, 4, 7]) if will_fail else None

        per_step = 5.0 / len(self.PROVISION_STEPS)
        for i, (pct, status, log_line) in enumerate(self.PROVISION_STEPS):
            time.sleep(per_step)
            progress_cb(pct, status)
            log_cb(log_line)
            if i == fail_at_idx:
                err = random.choice([
                    "Slave did not respond on UART slot",
                    "Master flash verify mismatch on phost_main",
                    "SET_I2C_ADDR ACK timeout",
                    "NVS write failed",
                ])
                log_cb(f"✗ master: {err}")
                return ProvisionResult(
                    ok=False, error=err,
                    duration_s=time.time() - start,
                )

        # Success: add new slave to master's known list
        with self._lock:
            self._slaves[addr] = SlaveInfo(
                addr=addr, label=label, online=True,
                target_type="esp32",
                current_fw=None, last_result=None,
            )
            self._statuses[addr] = SlaveStatus(
                addr=addr, busy=False, progress=0, status_text="Ready",
            )
            self._fw_pools[addr] = []     # freshly provisioned — empty library
            self._sync_statuses[addr] = self._fresh_sync_status(addr)
        return ProvisionResult(ok=True, duration_s=time.time() - start)

    # --- library sync ---

    def _fresh_sync_status(self, addr: int) -> SyncStatus:
        """Build an idle SyncStatus reflecting the slave's current stored pool."""
        stored = self._fw_pools.get(addr, [])
        files_total = len(self.SERVER_LIBRARY)
        bytes_total = sum(e["size"] for e in self.SERVER_LIBRARY)
        bytes_done = sum(e["size"] for e in stored)
        return SyncStatus(
            addr=addr, phase="idle",
            files_done=len(stored), files_total=files_total,
            bytes_done=bytes_done, bytes_total=bytes_total,
            current_file="", error=None,
        )

    def sync_library(self, addrs: list[int], manifest_url: str) -> dict:
        accepted, skipped = [], []
        for addr in addrs:
            slave = self._slaves.get(addr)
            if slave is None or not slave.online:
                skipped.append(addr)
                continue
            with self._lock:
                if self._sync_statuses[addr].phase in (
                    "connecting", "downloading", "verifying", "storing"
                ):
                    skipped.append(addr)      # already syncing
                    continue
            accepted.append(addr)
            threading.Thread(
                target=self._simulate_sync, args=(addr,), daemon=True
            ).start()
        return {
            "ok": True,
            "job_id": f"sync-{int(time.time())}",
            "accepted": accepted,
            "skipped": skipped,
        }

    def _set_sync(self, addr: int, **kwargs) -> None:
        with self._lock:
            st = self._sync_statuses[addr]
            for k, v in kwargs.items():
                setattr(st, k, v)

    def _simulate_sync(self, addr: int) -> None:
        bytes_total = sum(e["size"] for e in self.SERVER_LIBRARY)
        stored_ids = {e["id"] for e in self._fw_pools.get(addr, [])}
        missing = [e for e in self.SERVER_LIBRARY if e["id"] not in stored_ids]
        bytes_done = bytes_total - sum(e["size"] for e in missing)
        files_done = len(self.SERVER_LIBRARY) - len(missing)

        # connecting phase
        self._set_sync(addr, phase="connecting", error=None, current_file="")
        time.sleep(0.6)

        if not missing:
            # already up-to-date — no-op re-sync
            self._set_sync(addr, phase="done", current_file="",
                           bytes_done=bytes_total, files_done=files_done)
            return

        will_fail = random.random() < self._fail_rate
        fail_at = random.randint(0, len(missing) - 1) if will_fail else None

        for i, entry in enumerate(missing):
            label = f"{entry['device_type']}/{entry['version']}"
            # downloading — a few byte-progress ticks
            self._set_sync(addr, phase="downloading", current_file=label)
            chunks = 5
            for c in range(1, chunks + 1):
                time.sleep(self._flash_duration / (len(missing) * chunks * 2))
                self._set_sync(addr,
                               bytes_done=bytes_done + entry["size"] * c // chunks)
            if i == fail_at:
                self._set_sync(addr, phase="failed",
                               error=f"download {label}: connection reset")
                return
            # verify + store
            self._set_sync(addr, phase="verifying", current_file=label)
            time.sleep(self._flash_duration / (len(missing) * 4))
            self._set_sync(addr, phase="storing", current_file=label)
            time.sleep(self._flash_duration / (len(missing) * 4))
            # commit file into the slave's stored pool
            with self._lock:
                self._fw_pools[addr].append(dict(entry))
            bytes_done += entry["size"]
            files_done += 1
            self._set_sync(addr, files_done=files_done, bytes_done=bytes_done)

        self._set_sync(addr, phase="done", current_file="",
                       bytes_done=bytes_total)
