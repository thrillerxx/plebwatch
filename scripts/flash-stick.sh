#!/usr/bin/env bash
# Flash PlebWatch to the M5StickC Plus2 on /dev/ttyACM0.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-/dev/ttyACM0}"
export PATH="${HOME}/.platformio/penv/bin:${PATH}"

if [[ ! -e "$PORT" ]]; then
  echo "No serial device at $PORT — plug in the Stick (data USB-C) and retry."
  exit 1
fi

if [[ ! -w "$PORT" ]]; then
  echo "Port $PORT not writable; elevating chmod (sudo password)…"
  sudo chmod a+rw "$PORT"
fi

cd "$ROOT"
echo "Uploading to $PORT…"
pio run -t upload --upload-port "$PORT"
echo "Done. Unplug when convenient and let it deep-sleep for battery testing."
