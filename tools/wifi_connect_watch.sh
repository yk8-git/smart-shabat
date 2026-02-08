#!/bin/zsh
set -euo pipefail
cd "${0:A:h}/.."
exec python3 tools/wifi_connect_watch.py "$@"

