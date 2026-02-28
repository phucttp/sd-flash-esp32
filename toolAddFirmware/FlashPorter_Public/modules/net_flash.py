"""
net_flash.py
HTTP client for communicating with ESP32-C3 FlashPorter nodes via NetFlash API.

Endpoints:
    GET  /ping          → {id, ip, uptime, netflash}
    GET  /api/status    → {busy, progress, status}
    GET  /api/fw_list   → {firmwares: [{id, display}]}
    POST /api/flash     → {accepted: true} or {error: ...}
    POST /api/reboot    → {ok: true}
"""

import json
import select
import socket
import time
import urllib.request
import urllib.error
from typing import Optional, List, Dict, Any

TIMEOUT_PING = 2   # ping: short timeout for quick online/offline feedback
TIMEOUT = 5        # fast ops: fw_list, flash cmd, reboot
TIMEOUT_POLL = 12  # status polling during flash (ESP32 SWD can delay HTTP briefly)


class NodeClient:
    """HTTP client for a single FlashPorter node."""

    def __init__(self, host: str, port: int = 80):
        """
        :param host: Hostname or IP, e.g. "flashporter-A1B2.local" or "192.168.1.5"
        :param port: HTTP port (default 80)
        """
        self.host = host
        self.port = port
        self.base_url = f"http://{host}:{port}"

    def _get(self, path: str, timeout: int = TIMEOUT) -> Dict[str, Any]:
        url = f"{self.base_url}{path}"
        req = urllib.request.Request(url, headers={"Accept": "application/json"})
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return json.loads(resp.read().decode())

    def _post(self, path: str, data: dict = None) -> Dict[str, Any]:
        url = f"{self.base_url}{path}"
        body = json.dumps(data or {}).encode()
        req = urllib.request.Request(
            url, data=body, method="POST",
            headers={
                "Content-Type": "application/json",
                "Accept": "application/json"
            }
        )
        with urllib.request.urlopen(req, timeout=TIMEOUT) as resp:
            return json.loads(resp.read().decode())

    def _tcp_check(self, timeout: float) -> None:
        """Quick TCP connect check — raises if port unreachable within timeout.
        Bypasses Windows TCP SYN retry delays (default ~7s) via select()."""
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.setblocking(False)
        s.connect_ex((self.host, self.port))
        _, wr, _ = select.select([], [s], [], timeout)
        if wr:
            err = s.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
            s.close()
            if err != 0:
                raise ConnectionError(f"refused ({err})")
        else:
            s.close()
            raise ConnectionError("timeout")

    def ping(self) -> Dict[str, Any]:
        """GET /ping → {id, ip, uptime, netflash}"""
        self._tcp_check(TIMEOUT_PING)
        return self._get("/ping", timeout=TIMEOUT_PING)

    def get_status(self) -> Dict[str, Any]:
        """GET /api/status → {busy, progress, status}"""
        return self._get("/api/status", timeout=TIMEOUT_POLL)

    def get_fw_list(self) -> List[Dict[str, str]]:
        """GET /api/fw_list → [{id, display}, ...]"""
        result = self._get("/api/fw_list")
        return result.get("firmwares", [])

    def flash(self, fw_id: str) -> Dict[str, Any]:
        """POST /api/flash {fw_id} → {accepted: true} or {error: ...}"""
        return self._post("/api/flash", {"fw_id": fw_id})

    def reboot(self) -> Dict[str, Any]:
        """POST /api/reboot → {ok: true}"""
        return self._post("/api/reboot")


def discover_nodes(timeout: float = 5.0) -> List[str]:
    """
    Discover FlashPorter nodes on LAN.
    Strategy:
      1. ARP cache  — instant, catches recently-seen devices → probe directly
      2. ipconfig   — get local /24 subnet(s), TCP pre-filter port 80 (0.8s)
         then GET /ping to confirm FlashPorter identity
    ARP candidates bypass TCP check (known devices, firewall may block TCP probe).
    """
    import subprocess
    import concurrent.futures
    import re

    # --- Step 1: ARP cache — recently-seen devices (probe directly, no TCP check) ---
    arp_candidates: set = set()
    try:
        out = subprocess.run(['arp', '-a'], capture_output=True, text=True, timeout=5).stdout
        for line in out.splitlines():
            parts = line.split()
            # Windows: "  192.168.x.y  xx-xx-xx-xx-xx-xx  dynamic"
            if len(parts) >= 3 and parts[2].lower() == 'dynamic':
                ip = parts[0]
                if not ip.endswith('.255') and not ip.startswith('169.254'):
                    arp_candidates.add(ip)
    except Exception:
        pass

    # --- Step 2: subnet scan — all /24 IPs not already in ARP cache ---
    subnet_candidates: set = set()
    try:
        out = subprocess.run(['ipconfig'], capture_output=True, text=True, timeout=5).stdout
        for match in re.finditer(r'IPv4[^:]*:\s*([\d.]+)', out):
            my_ip = match.group(1)
            if (my_ip.startswith('192.168.') or
                    my_ip.startswith('10.') or
                    re.match(r'^172\.(1[6-9]|2\d|3[01])\.', my_ip)):
                prefix = '.'.join(my_ip.split('.')[:3])
                for i in range(1, 255):
                    ip = f"{prefix}.{i}"
                    if ip not in arp_candidates:
                        subnet_candidates.add(ip)
    except Exception:
        pass

    # --- Step 3: merge and TCP pre-filter all candidates ---
    all_candidates = arp_candidates | subnet_candidates
    if not all_candidates:
        return []

    def tcp_open(ip: str) -> Optional[str]:
        """Non-blocking TCP connect with select() — works on Windows."""
        import select as _select
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.setblocking(False)
            s.connect_ex((ip, 80))  # WSAEWOULDBLOCK on Windows is normal
            _, writable, _ = _select.select([], [s], [], 0.8)
            if writable:
                err = s.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
                s.close()
                return ip if err == 0 else None
            s.close()
            return None
        except Exception:
            return None

    http_hosts: List[str] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=128) as ex:
        for result in ex.map(tcp_open, list(all_candidates)):
            if result:
                http_hosts.append(result)

    if not http_hosts:
        return []

    # --- Step 4: GET /ping — confirm FlashPorter identity ---
    found: List[str] = []

    def probe(ip: str) -> Optional[str]:
        try:
            info = NodeClient(ip).ping()
            if info.get('netflash') is not None:
                return ip
        except Exception:
            pass
        return None

    with concurrent.futures.ThreadPoolExecutor(max_workers=32) as ex:
        for result in ex.map(probe, http_hosts):
            if result:
                found.append(result)

    return found
