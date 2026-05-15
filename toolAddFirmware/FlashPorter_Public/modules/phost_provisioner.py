"""
phost_provisioner.py
====================
Provisioning interface for fresh Phost slave boards.

Workflow:
  1. User plugs slave into PC via USB-TTL cable
  2. PC tool detects COM port
  3. User picks I2C address + label
  4. PC flashes phost_main FW via esp-serial-flasher (download mode pulse)
  5. Slave reboots → first-boot setup mode (NVS empty)
  6. PC sends 'SET_I2C_ADDR 0x14' command over UART
  7. Slave writes NVS, reboots into normal I2C mode
  8. User unplugs USB-TTL, plugs slave into master's I2C bus

Implementations:
  * MockPhostProvisioner in `phost_provisioner_mock.py` — fake ports + sim.
  * EsptoolProvisioner (future) — real esptool integration when phost FW
    is ready and the SET_I2C_ADDR UART protocol is defined.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass
from typing import Callable, Optional


# ─────────────────────────── Data types ───────────────────────────

@dataclass
class SerialPort:
    device: str        # "COM3", "/dev/ttyUSB0"
    description: str   # human-readable e.g. "CP210x USB UART (COM3)"
    hwid: str          # USB VID:PID etc. for matching adapters


@dataclass
class ProvisionResult:
    ok: bool
    error: Optional[str] = None
    duration_s: Optional[float] = None


# Callbacks fire on the worker thread; tab dispatches to Tk via after(0, ...)
ProgressCallback = Callable[[int, str], None]   # (percent 0..100, status)
LogCallback = Callable[[str], None]             # (one log line)


# ─────────────────────────── Interface ───────────────────────────

class PhostProvisioner(ABC):
    """Abstract interface — firmware/protocol must match this contract."""

    @abstractmethod
    def list_ports(self) -> list[SerialPort]:
        """Enumerate currently-connected USB-TTL adapters."""

    @abstractmethod
    def test_connection(self, port: str) -> dict:
        """Verify a slave responds on `port`.
        Returns {ok: bool, chip_info: str|None, error: str|None}."""

    @abstractmethod
    def provision_slave(
        self,
        port: str,
        fw_bytes: bytes,
        i2c_addr: int,
        progress_cb: ProgressCallback,
        log_cb: LogCallback,
    ) -> ProvisionResult:
        """Run the full provisioning sequence on the calling thread.
        Caller is responsible for running this on a worker thread."""


# ─────────────────────────── Factory ───────────────────────────

def make_phost_provisioner(backend: str = "mock") -> PhostProvisioner:
    if backend == "mock":
        from .phost_provisioner_mock import MockPhostProvisioner
        return MockPhostProvisioner()
    raise NotImplementedError(
        f"backend {backend!r} not implemented yet — use 'mock' until "
        "phost slave firmware + UART NVS protocol are ready"
    )
