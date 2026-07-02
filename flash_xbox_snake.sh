#!/usr/bin/env bash
#
# flash_xbox_snake.sh — flash the R4 with xbox_snake (direct Xbox BLE pairing).
#
# The R4 pairs DIRECTLY with an Xbox Wireless (BLE) controller — no laptop, no
# ESP32 bridge — and plays Snake on its 12x8 LED matrix. This needs the PATCHED
# ArduinoBLE (central-side LESC initiator + raw-notify hook); the script checks
# for it and refuses to flash a stock library that would only sit there unpaired.
#
# USB-C is used for THIS flash only; after that it's power-only and everything is
# Bluetooth. Run from a NATIVE terminal (the VS Code snap terminal blocks serial).
#
#   Xbox controller --BLE(bonded)--> R4 (xbox_snake) --> Snake on LED matrix
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARDUINO_CLI="${ARDUINO_CLI:-$HOME/.local/bin/arduino-cli}"
FQBN="arduino:renesas_uno:unor4wifi"
SKETCH="$SCRIPT_DIR/xbox_snake"
LIB="$HOME/Arduino/libraries/ArduinoBLE/src/utility/L2CAPSignaling.cpp"

# --- snap-terminal guard ---
if [ -n "${SNAP:-}" ]; then
  echo "!! You're in a snap-confined shell (${SNAP_NAME:-}). Serial upload may fail."
  echo "!! Run this from a native terminal (GNOME Terminal / Console)."
  echo
fi

# --- arduino-cli present? ---
if [ ! -x "$ARDUINO_CLI" ]; then
  echo "ERROR: arduino-cli not found at $ARDUINO_CLI (run install_arduino_cli.sh first)."
  exit 1
fi

# --- patched ArduinoBLE present? (central-side pairing lives in startPairing) ---
if [ ! -f "$LIB" ] || ! grep -q "startPairing" "$LIB" 2>/dev/null; then
  echo "ERROR: ArduinoBLE is NOT patched for central-side Xbox pairing."
  echo "       Expected 'startPairing' in: $LIB"
  echo "       xbox_snake will compile but never pair. See README (Direct Xbox pairing)."
  exit 1
fi

# --- find the R4 serial port ---
PORT="${1:-}"
[ -z "$PORT" ] && PORT="$(ls /dev/ttyACM* 2>/dev/null | head -n1 || true)"
if [ -z "$PORT" ]; then
  echo "ERROR: no /dev/ttyACM* found. Plug in the R4 (or pass a port: $0 /dev/ttyACM0)."
  exit 1
fi
echo "==> R4 port: $PORT"

# --- compile + upload ---
echo "==> Compiling xbox_snake ..."
"$ARDUINO_CLI" compile --fqbn "$FQBN" "$SKETCH" || { echo "compile failed"; exit 1; }
echo "==> Uploading to $PORT ..."
"$ARDUINO_CLI" upload -p "$PORT" --fqbn "$FQBN" "$SKETCH" || { echo "upload failed"; exit 1; }

cat <<'STEPS'

==> Flashed! Now (do NOT reflash after this — it drops the Xbox and wedges the radio):

  1) POWER-CYCLE the R4: unplug USB-C, wait ~10s, plug back in.
     The LED matrix shows 4 blinking corners = "waiting for Xbox".
       (If it scrolls "BLE?": the radio is wedged -> full USB unplug, or reflash
        the ESP32-S3 firmware once via Arduino IDE -> Tools -> Firmware Updater.)

  2) PUT THE XBOX IN PAIRING MODE: hold the top pair button until the Xbox logo
     fast-blinks. The R4 pairs (a few seconds) and the matrix shows the title.

  3) PLAY: press A to start, steer with the d-pad or left stick, eat the blinking
     food. On game-over it scrolls your score; press A to restart.

STEPS
