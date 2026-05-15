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

from .master_client import MasterClient, SlaveInfo, SlaveStatus


class MockMasterClient(MasterClient):
    """4 fake slaves with simulated I2C state + progress threads."""

    DEFAULT_SLAVES = [
        # (addr, label, target_type, online)
        (0x10, "Phost-1", "esp32",     True),
        (0x11, "Phost-2", "stm32f1",   True),
        (0x12, "Phost-3", "no-target", False),
        (0x13, "Phost-4", "esp32",     True),
    ]

    DEFAULT_FW_PER_SLAVE = {
        0x10: [
            {"id": "EMC32_v1.2", "display": "EMC32 v1.2"},
            {"id": "EMC32_v1.3", "display": "EMC32 v1.3 (beta)"},
        ],
        0x11: [
            {"id": "STM_app_v0.9", "display": "STM app v0.9"},
        ],
        0x12: [],
        0x13: [
            {"id": "EMC32_v1.2", "display": "EMC32 v1.2"},
        ],
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
        self._fw_pools: dict[int, list[dict]] = {
            k: list(v) for k, v in self.DEFAULT_FW_PER_SLAVE.items()
        }
        self._statuses: dict[int, SlaveStatus] = {
            addr: SlaveStatus(addr=addr, busy=False, progress=0,
                              status_text="Ready" if info.online else "Offline")
            for addr, info in self._slaves.items()
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
        return list(self._fw_pools.get(addr, []))

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
