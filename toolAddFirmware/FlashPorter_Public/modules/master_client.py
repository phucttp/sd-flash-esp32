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
from typing import Callable, Optional


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


@dataclass
class ProvisionResult:
    """Outcome of provisioning a freshly-plugged phost via master's UART slot."""
    ok: bool
    error: Optional[str] = None
    duration_s: Optional[float] = None


@dataclass
class SyncStatus:
    """Live per-slave library-sync snapshot for status polling.

    A slave mirrors the WHOLE server FW library into its external memory.
    Sync is manifest-driven + incremental: only missing / md5-mismatched
    files are downloaded, so a re-sync of an up-to-date slave is a no-op.
    """
    addr: int
    phase: str                      # idle/connecting/downloading/verifying/storing/done/failed
    files_done: int                 # files already up-to-date or downloaded
    files_total: int                # files in the manifest
    bytes_done: int
    bytes_total: int
    current_file: str               # e.g. "EMC32/v1.3" — "" when idle/done
    error: Optional[str] = None


# Provisioning callbacks fire on worker thread; UI must dispatch via after(0, ...).
ProgressCallback = Callable[[int, str], None]   # (percent 0..100, status)
LogCallback = Callable[[str], None]             # (one log line)


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
        """Firmware versions currently stored in this slave's external memory.

        Returns [{id, display}, ...]. Populated by sync_library() — empty
        until the slave has synced at least once.
        """

    @abstractmethod
    def sync_library(self, addrs: list[int], manifest_url: str) -> dict:
        """Tell the listed slaves to mirror the full FW library.

        Each slave fetches the manifest from `manifest_url`, diffs it against
        its external memory, and downloads any missing / changed files over
        its own WiFi. Non-blocking — returns {ok, job_id} immediately; poll
        get_sync_status() for progress.
        """

    @abstractmethod
    def get_sync_status(self) -> list[SyncStatus]:
        """Aggregate 1-shot library-sync status of every slave (poll-friendly)."""

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

    @abstractmethod
    def provision_slave(
        self,
        addr: int,
        label: str,
        progress_cb: ProgressCallback,
        log_cb: LogCallback,
    ) -> ProvisionResult:
        """Provision a freshly-plugged phost via master's dedicated UART slot.

        Master uses its pre-flashed phost_main partition (no FW upload from PC).
        Blocks the calling thread — caller must run on a worker thread and
        dispatch callbacks back to UI via after(0, ...).

        Adds new slave to internal state on success so subsequent get_slaves()
        and get_status() reflect it.
        """


# ─────────────────────────── Factory ───────────────────────────

def make_master_client(backend: str = "mock", token: str = "") -> MasterClient:
    """Pick implementation based on settings.json `netflash_backend`.

    - "mock" — MockMasterClient, fake slaves for UI development.
    - "ws"   — WsMasterClient, real WebSocket backend (see docs/master-api.md).
               Needs the `websocket-client` package; `token` is the shared
               auth token sent in the WS handshake.
    """
    if backend == "mock":
        from .master_client_mock import MockMasterClient
        return MockMasterClient()
    if backend == "ws":
        from .master_client_ws import WsMasterClient
        return WsMasterClient(token=token)
    raise NotImplementedError(
        f"backend {backend!r} not recognised — use 'mock' or 'ws'"
    )
