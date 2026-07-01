#!/usr/bin/env bash
#
# Build the C console app, flash the serial snake sketch, and run it.
#   controller_cui (C)  --USB serial-->  R4 (snake_led)  -->  LED matrix
#
# Run from a NATIVE terminal. Be in the 'input' and 'dialout' groups.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARDUINO_CLI="$HOME/.local/bin/arduino-cli"
FQBN="arduino:renesas_uno:unor4wifi"

if [ -n "${SNAP:-}" ]; then
  echo "!! Snap-confined shell — /dev/input & serial may be blocked. Use a native terminal."
fi

PORT="${1:-}"
[ -z "$PORT" ] && PORT="$(ls /dev/ttyACM* 2>/dev/null | head -n1 || true)"
if [ -z "$PORT" ]; then echo "ERROR: no /dev/ttyACM* found (pass one: $0 /dev/ttyACM0)"; exit 1; fi
echo "==> R4 port: $PORT"

echo "==> Building controller_cui ..."
gcc -O2 -o "$SCRIPT_DIR/controller_cui" "$SCRIPT_DIR/controller_cui.c"

echo "==> Flashing snake_led (serial sketch) ..."
"$ARDUINO_CLI" compile --fqbn "$FQBN" "$SCRIPT_DIR/snake_led"
"$ARDUINO_CLI" upload -p "$PORT" --fqbn "$FQBN" "$SCRIPT_DIR/snake_led"

echo "==> Waiting for reboot (2s) ..."
sleep 2

echo "==> Launching console UI (Ctrl+C to quit) ..."
exec "$SCRIPT_DIR/controller_cui" "$PORT"
