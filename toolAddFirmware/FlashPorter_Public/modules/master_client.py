"""
master_client.py
================
Master-Slave NetFlash interface (long-term, stable contract).

Architecture:
    PC ──HTTP──> ESP32-C3 MASTER ──I2C──> Phost slave 0x10
                                  ├──I2C──> Phost slave 0x11
                                  └──I2C──> ...

The master dispatches short commands over I2C. Each slave stores firmware
locally and executes flashing autonomously. PC only talks to the master.

Implementations:
  * MockMasterClient in `master_client_mock.py` — fake slaves for UI dev.
  * HttpMasterClient (future) — real HTTP client when master firmware exists.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass, asdict
from typing import Optional


# ─────────────────────────── Data types ───────────────────────────

@dataclass
class SlaveInfo:
    addr: int                       # I2C address, e.g. 0x10
    label: str                      # user-editable, e.g. "Phost-1" / "Station-A"
    online: bool                    # slave responded to last I2C probe
    target_type: str                # "esp32" / "stm32f1" / "stm32f4" / "no-target"
    current_fw: Optional[str]       # FW id last flashed (slave-reported), or None
    last_result: Optional[str]      # "ok" / "fail" / None
    last_duration_s: Optional[float] = None
    error_text: Optional[str] = None         # short error for card display

    def to_dict(self) -> dict:
        return asdict(self)


@dataclass
class SlaveStatus:
    """Live per-slave snapshot for status polling."""
    addr: int
    busy: bool
    progress: int                   # 0..100
    status_text: str                # "Ready" / "Flashing" / "Erasing" / "✗ no target"


# ─────────────────────────── Interface ───────────────────────────

class MasterClient(ABC):
    """Abstract interface — keep this stable; firmware must match it."""

    @abstractmethod
    def connect(self, host: str, port: int = 80) -> dict:
        """One-shot connect + handshake. Returns {ok, master_id, slaves_count}."""

    @abstractmethod
    def is_connected(self) -> bool: ...

    @abstractmethod
    def disconnect(self) -> None: ...

    @abstractmethod
    def get_slaves(self) -> list[SlaveInfo]: ...

    @abstractmethod
    def get_slave_fw_list(self, addr: int) -> list[dict]:
        """Per-slave firmware list. Returns [{id, display}, ...]."""

    @abstractmethod
    def get_status(self) -> list[SlaveStatus]:
        """Aggregate 1-shot status of every slave (cheap, poll-friendly)."""

    @abstractmethod
    def flash_slave(self, addr: int, fw_id: str) -> dict: ...

    @abstractmethod
    def flash_all(self, fw_per_slave: dict[int, str]) -> dict:
        """fw_per_slave maps addr → fw_id. Master dispatches in parallel."""

    @abstractmethod
    def erase_slave(self, addr: int) -> dict: ...

    @abstractmethod
    def erase_all(self) -> dict: ...

    @abstractmethod
    def reboot_slave(self, addr: int) -> dict: ...

    @abstractmethod
    def reboot_all(self) -> dict: ...


# ─────────────────────────── Factory ───────────────────────────

def make_master_client(backend: str = "mock") -> MasterClient:
    """Pick implementation based on settings.json `netflash_backend`."""
    if backend == "mock":
        from .master_client_mock import MockMasterClient
        return MockMasterClient()
    raise NotImplementedError(
        f"backend {backend!r} not implemented yet — use 'mock' until "
        "ESP32-C3 master firmware is ready"
    )
