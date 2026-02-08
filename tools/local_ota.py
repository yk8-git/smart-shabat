#!/usr/bin/env python3
"""
Local OTA helper for SmartShabat.

What it does:
1) Builds firmware via `pio run` (optional)
2) Serves `ota.json` + `firmware.bin` over HTTP
3) Tells the device to use the manifest served from *this* client IP
4) Triggers `/api/ota/update`

Run this while your computer is connected to the device Hotspot (usually 192.168.4.1).
"""

from __future__ import annotations

import argparse
import hashlib
import http.server
import json
import os
import shutil
import subprocess
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path


def _run(cmd: list[str], cwd: Path) -> None:
    subprocess.run(cmd, cwd=str(cwd), check=True)


def _md5_file(path: Path) -> str:
    h = hashlib.md5()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 256), b""):
            h.update(chunk)
    return h.hexdigest()


def _http_post_json(url: str, payload: dict, timeout_s: float) -> dict:
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        url,
        data=data,
        method="POST",
        headers={"content-type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=timeout_s) as r:
        raw = r.read().decode("utf-8", errors="replace")
    try:
        return json.loads(raw) if raw else {}
    except json.JSONDecodeError:
        raise RuntimeError(f"Non-JSON response from {url}: {raw[:200]!r}")


def _http_get_json(url: str, timeout_s: float) -> dict:
    with urllib.request.urlopen(url, timeout=timeout_s) as r:
        raw = r.read().decode("utf-8", errors="replace")
    try:
        return json.loads(raw) if raw else {}
    except json.JSONDecodeError:
        raise RuntimeError(f"Non-JSON response from {url}: {raw[:200]!r}")


class _CountingHandler(http.server.SimpleHTTPRequestHandler):
    ota_hits = 0
    bin_hits = 0
    _lock = threading.Lock()

    def log_message(self, fmt: str, *args) -> None:
        # Keep logs compact and single-line.
        msg = fmt % args
        print(f"[http] {self.address_string()} {msg}")

    def do_GET(self) -> None:  # noqa: N802
        with self._lock:
            if self.path.startswith("/ota.json"):
                type(self).ota_hits += 1
            elif self.path.startswith("/firmware.bin"):
                type(self).bin_hits += 1
        super().do_GET()


def _start_server(directory: Path, port: int) -> tuple[http.server.ThreadingHTTPServer, threading.Thread]:
    handler = lambda *args, **kwargs: _CountingHandler(*args, directory=str(directory), **kwargs)  # noqa: E731
    httpd = http.server.ThreadingHTTPServer(("0.0.0.0", port), handler)
    t = threading.Thread(target=httpd.serve_forever, daemon=True)
    t.start()
    return httpd, t


def main() -> int:
    p = argparse.ArgumentParser(description="Local OTA helper (build + serve + trigger update).")
    p.add_argument("--device", default="192.168.4.1", help="Device IP (default: 192.168.4.1)")
    p.add_argument("--env", default="esp12e", help="PlatformIO env (default: esp12e)")
    p.add_argument("--port", type=int, default=8000, help="HTTP server port (default: 8000)")
    p.add_argument("--timeout", type=float, default=8.0, help="HTTP request timeout in seconds (default: 8)")
    p.add_argument("--no-build", action="store_true", help="Skip `pio run` (use existing .pio/build)")
    p.add_argument(
        "--ota-dir",
        default="ota-local",
        help="Directory to serve from (default: ota-local)",
    )
    args = p.parse_args()

    repo = Path(__file__).resolve().parents[1]
    firmware_src = repo / ".pio" / "build" / args.env / "firmware.bin"
    ota_dir = repo / args.ota_dir
    ota_dir.mkdir(parents=True, exist_ok=True)

    if not args.no_build:
        print("[ota] building firmware…")
        _run(["pio", "run", "-e", args.env], cwd=repo)

    if not firmware_src.exists():
        raise SystemExit(f"Firmware not found: {firmware_src}")

    firmware_dst = ota_dir / "firmware.bin"
    shutil.copy2(firmware_src, firmware_dst)
    md5 = _md5_file(firmware_dst)

    httpd, _ = _start_server(ota_dir, args.port)
    print(f"[ota] serving on 0.0.0.0:{args.port} from {ota_dir}")

    device_base = f"http://{args.device}"
    try:
        # Tell the device to use our manifest URL based on the client IP it sees.
        mf = _http_post_json(
            device_base + "/api/ota/manifest_from_client",
            {"port": args.port, "path": "/ota.json"},
            timeout_s=args.timeout,
        )
        manifest_url = str(mf.get("manifestUrl") or "")
        if not manifest_url:
            raise RuntimeError(f"Device did not return manifestUrl: {mf}")
        parsed = urllib.parse.urlparse(manifest_url)
        host_ip = parsed.hostname or ""
        if not host_ip:
            raise RuntimeError(f"Bad manifestUrl returned by device: {manifest_url!r}")

        version = str(int(time.time()))
        manifest = {
            "version": version,
            "bin": f"http://{host_ip}:{args.port}/firmware.bin",
            "md5": md5,
            "notes": f"local dev build {version}",
        }
        (ota_dir / "ota.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        print(f"[ota] wrote manifest {ota_dir / 'ota.json'} (bin host {host_ip})")

        # Ensure the manifest is reachable by the device before triggering update.
        chk = _http_post_json(device_base + "/api/ota/check", {}, timeout_s=args.timeout)
        print(f"[ota] check: {chk}")
        upd = _http_post_json(device_base + "/api/ota/update", {}, timeout_s=args.timeout)
        print(f"[ota] update: {upd}")

        print("[ota] waiting for device to download firmware.bin…")
        deadline = time.time() + 90
        while time.time() < deadline:
            if _CountingHandler.bin_hits > 0:
                print("[ota] firmware.bin downloaded; waiting a bit for reboot…")
                time.sleep(10)
                return 0
            time.sleep(0.25)

        print("[ota] timeout waiting for firmware.bin download. Check device logs (/api/ota/status or serial).")
        return 2
    except urllib.error.URLError as e:
        print(f"[ota] network error: {e}")
        return 3
    finally:
        httpd.shutdown()


if __name__ == "__main__":
    raise SystemExit(main())

