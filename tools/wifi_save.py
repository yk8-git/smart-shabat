#!/usr/bin/env python3
"""
Save Wi-Fi credentials on the device via its AP API (default: 192.168.4.1).

This does not require serial access and works well when you're connected to the device Hotspot.
"""

from __future__ import annotations

import argparse
import json
import time
import urllib.request


def _get_json(url: str, timeout_s: float) -> object:
    with urllib.request.urlopen(url, timeout=timeout_s) as r:
        raw = r.read().decode("utf-8", errors="replace")
    return json.loads(raw) if raw else None


def _post_json(url: str, payload: dict, timeout_s: float) -> object:
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(url, data=data, method="POST", headers={"content-type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout_s) as r:
        raw = r.read().decode("utf-8", errors="replace")
    return json.loads(raw) if raw else None


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--device", default="192.168.4.1")
    p.add_argument("--ssid", required=True)
    p.add_argument("--password", default="")
    p.add_argument("--no-make-last", action="store_true")
    p.add_argument("--connect", action="store_true", help="also start a connection attempt after saving")
    p.add_argument("--simple", action="store_true", help="if --connect, use STA-only mode (drops AP temporarily)")
    p.add_argument("--timeout", type=float, default=30.0, help="poll timeout when --connect is used")
    p.add_argument("--http-timeout", type=float, default=6.0)
    args = p.parse_args()

    base = f"http://{args.device}"
    make_last = not args.no_make_last

    payload = {
        "ssid": args.ssid,
        "password": args.password,
        "makeLast": make_last,
        "connect": bool(args.connect),
        "simple": bool(args.simple),
    }
    res = _post_json(base + "/api/wifi/save", payload, timeout_s=args.http_timeout) or {}
    print(json.dumps(res, ensure_ascii=False))

    if not args.connect:
        return 0 if res.get("ok") else 2

    start = time.time()
    while time.time() - start < args.timeout:
        try:
            st = _get_json(base + "/api/wifi/status", timeout_s=args.http_timeout) or {}
        except Exception:
            time.sleep(1.0)
            continue

        if int(st.get("staStatusCode") or 0) == 3 and (st.get("staIp") or ""):
            print("connected:", json.dumps(st, ensure_ascii=False))
            return 0
        if not st.get("connecting"):
            print("stopped:", json.dumps(st, ensure_ascii=False))
            return 2
        time.sleep(1.0)
    print("timeout")
    return 3


if __name__ == "__main__":
    raise SystemExit(main())

