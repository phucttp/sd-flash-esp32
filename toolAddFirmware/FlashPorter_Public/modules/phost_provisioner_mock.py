"""
phost_provisioner_mock.py
=========================
Fake PhostProvisioner — 3 simulated USB-TTL ports + progress simulation.
Use during UI development before phost firmware + UART protocol are ready.
"""

from __future__ import annotations

import random
import time

from .phost_provisioner import (
    PhostProvisioner, SerialPort, ProvisionResult,
    ProgressCallback, LogCallback,
)


class MockPhostProvisioner(PhostProvisioner):
    """Three fake adapters + fake esptool-like flash sequence."""

    DEFAULT_PORTS = [
        SerialPort("COM3", "Silicon Labs CP210x USB to UART Bridge (COM3)",
                   "USB VID:PID=10C4:EA60"),
        SerialPort("COM5", "USB-SERIAL CH340 (COM5)",
                   "USB VID:PID=1A86:7523"),
        SerialPort("COM7", "FT232R USB UART (COM7)",
                   "USB VID:PID=0403:6001"),
    ]

    def __init__(self, fail_rate: float = 0.10, flash_duration_s: float = 5.0):
        self._fail_rate = fail_rate
        self._duration = flash_duration_s

    def list_ports(self) -> list[SerialPort]:
        return list(self.DEFAULT_PORTS)

    def test_connection(self, port: str) -> dict:
        time.sleep(0.4)
        known = {p.device for p in self.DEFAULT_PORTS}
        if port in known:
            return {
                "ok": True,
                "chip_info": "ESP32-C3 (chip rev 0, 4MB flash, MAC 7C:DF:A1:E5:14:88)",
                "error": None,
            }
        return {"ok": False, "chip_info": None,
                "error": f"Port {port} not responding (try ⟳ Refresh)"}

    def provision_slave(
        self,
        port: str,
        fw_bytes: bytes,
        i2c_addr: int,
        progress_cb: ProgressCallback,
        log_cb: LogCallback,
    ) -> ProvisionResult:
        start = time.time()
        size_kb = max(1, len(fw_bytes) // 1024) if fw_bytes else 642  # default-ish

        steps = [
            (5,  "Opening serial port...",
             f"> opened {port} @ 115200"),
            (10, "Pulsing RTS/DTR to enter download mode...",
             "> bootloader handshake OK"),
            (15, "Detecting chip...",
             "> ESP32-C3 (chip rev 0, 4MB flash)"),
            (25, "Erasing app partition (0x10000..0x110000)...",
             "> erase 1MB done"),
            (35, "Erasing NVS partition (0x9000..0xF000)...",
             "> erase NVS done"),
            (55, f"Writing phost_main.bin ({size_kb}KB)...",
             "> write 50%"),
            (75, f"Writing phost_main.bin ({size_kb}KB)...",
             "> write 100%"),
            (82, "Verifying flash...",
             "> verify OK (md5 match)"),
            (88, "Rebooting slave into first-boot mode...",
             "> slave boot: NVS empty → setup mode"),
            (94, f"Sending SET_I2C_ADDR 0x{i2c_addr:02x} via UART...",
             f"> ACK: addr 0x{i2c_addr:02x} written to NVS"),
            (98, "Final reboot into normal I2C mode...",
             "> slave reports: ready, listening on I2C 0x%02x" % i2c_addr),
            (100, "Done — slave ready for I2C bus",
             "> provisioning complete"),
        ]

        # Random fail at the FW write step (most realistic failure point)
        fail_at_idx = None
        if random.random() < self._fail_rate:
            fail_at_idx = random.choice([3, 5, 6, 9])  # erase or write or NVS

        per_step = self._duration / len(steps)
        for i, (pct, status, log_line) in enumerate(steps):
            time.sleep(per_step)
            progress_cb(pct, status)
            log_cb(log_line)
            if i == fail_at_idx:
                err = random.choice([
                    "Flash write timeout at offset 0x40000",
                    "Bootloader did not respond after 3 retries",
                    "MD5 verify mismatch — corrupt write",
                    "NVS write ACK timeout",
                ])
                log_cb(f"✗ {err}")
                return ProvisionResult(ok=False, error=err,
                                        duration_s=time.time() - start)

        return ProvisionResult(ok=True, duration_s=time.time() - start)
