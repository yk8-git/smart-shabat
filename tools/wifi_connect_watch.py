#!/usr/bin/env python3
"""
Connect helper (API-driven) for when you're on the device Hotspot (AP).

It uses /api/wifi/scan to grab channel+BSSID for the target SSID and includes those
as hints in /api/wifi/connect to avoid blocking scans on the device.
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


def _disc_reason_name(code: int) -> str:
    # ESP8266 wifi_station_disconnect_reason (partial, most useful ones)
    names = {
        0: "UNSPECIFIED/UNKNOWN",
        1: "UNSPECIFIED",
        2: "AUTH_EXPIRE",
        3: "AUTH_LEAVE",
        4: "ASSOC_EXPIRE",
        5: "ASSOC_TOOMANY",
        6: "NOT_AUTHED",
        7: "NOT_ASSOCED",
        8: "ASSOC_LEAVE",
        9: "ASSOC_NOT_AUTHED",
        13: "MIC_FAILURE",
        14: "4WAY_HANDSHAKE_TIMEOUT",
        15: "GROUP_KEY_UPDATE_TIMEOUT",
        18: "PAIRWISE_CIPHER_INVALID",
        19: "AKMP_INVALID",
        20: "UNSUPP_RSN_IE_VERSION",
        21: "INVALID_RSN_IE_CAP",
        22: "802_1X_AUTH_FAILED",
        23: "CIPHER_SUITE_REJECTED",
        24: "BEACON_TIMEOUT",
        200: "NO_AP_FOUND",
        201: "AUTH_FAIL",
        202: "ASSOC_FAIL",
        203: "HANDSHAKE_TIMEOUT",
    }
    return names.get(code, f"UNKNOWN_{code}")


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--device", default="192.168.4.1")
    p.add_argument("--ssid", required=True)
    p.add_argument("--password", default="")
    p.add_argument("--simple", action="store_true", help="STA-only mode (drops AP temporarily)")
    p.add_argument("--timeout", type=float, default=120.0)
    p.add_argument("--http-timeout", type=float, default=4.0)
    args = p.parse_args()

    base = f"http://{args.device}"
    ssid = args.ssid
    pw_len = len(args.password or "")

    nets = _get_json(base + "/api/wifi/scan", timeout_s=args.http_timeout) or []
    best = None
    for n in nets:
        if not isinstance(n, dict):
            continue
        if str(n.get("ssid") or "") != ssid:
            continue
        if best is None or int(n.get("rssi") or -9999) > int(best.get("rssi") or -9999):
            best = n

    channel = int(best.get("ch") or 0) if isinstance(best, dict) else 0
    bssid = str(best.get("bssid") or "") if isinstance(best, dict) else ""
    print(
        f"scan: ssid={ssid!r} channel={channel} bssid={bssid!r} rssi={(best or {}).get('rssi') if best else None} "
        f"passwordLen={pw_len}"
    )

    body = {"ssid": ssid, "password": args.password, "channel": channel, "bssid": bssid, "simple": bool(args.simple)}
    resp = _post_json(base + "/api/wifi/connect", body, timeout_s=args.http_timeout) or {}
    print("connect:", resp)

    start = time.time()
    last = None
    while time.time() - start < args.timeout:
        try:
            st = _get_json(base + "/api/wifi/status", timeout_s=args.http_timeout) or {}
        except Exception as e:  # noqa: BLE001
            print(f"status: fetch failed: {e}")
            time.sleep(1.0)
            continue
        key = (
            st.get("connectStage"),
            st.get("targetChannel"),
            st.get("apChannel"),
            st.get("staStatusCode"),
            st.get("discReason"),
            st.get("discReasonRaw"),
            st.get("discExpected"),
            st.get("sdkStaStatus"),
            st.get("staSsid"),
            st.get("staIp"),
            st.get("connecting"),
            st.get("lastFailCode"),
        )
        if key != last:
            disc = int(st.get("discReason") or 0)
            disc_raw = int(st.get("discReasonRaw") or 0)
            disc_exp = bool(st.get("discExpected"))
            sdk = int(st.get("sdkStaStatus") or 0)
            sdk_text = st.get("sdkStaStatusText") or ""
            print(
                f"status: stage={st.get('connectStage')} apCh={st.get('apChannel')} targetCh={st.get('targetChannel')} "
                f"sta={st.get('staStatus')}({st.get('staStatusCode')}) ssid={st.get('staSsid')!r} ip={st.get('staIp')!r} "
                f"connecting={st.get('connecting')} lastFail={st.get('lastFailCode')} "
                f"disc={disc}({_disc_reason_name(disc)}) raw={disc_raw}({_disc_reason_name(disc_raw)}) exp={disc_exp} "
                f"sdk={sdk}({sdk_text})"
            )
            last = key

        if int(st.get("staStatusCode") or 0) == 3 and (st.get("staIp") or ""):
            return 0
        if not st.get("connecting"):
            try:
                log = _get_json(base + "/api/wifi/log", timeout_s=args.http_timeout) or {}
                print("wifi log:", json.dumps(log, ensure_ascii=False))
            except Exception as e:  # noqa: BLE001
                print("wifi log: failed to fetch:", e)
            return 2
        time.sleep(1.0)
    return 3


if __name__ == "__main__":
    raise SystemExit(main())
