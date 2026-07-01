#!/usr/bin/env bash
#
# Flash snake_ble to the UNO R4 WiFi, then run the laptop BLE bridge.
#   - compiles + uploads the R4 sketch (R4 becomes BLE peripheral "SnakeR4")
#   - creates a Python venv (bleak + evdev) on first run
#   - reads the wired 8BitDo and writes input to the R4 over BLE
#
# Run from a NATIVE terminal (not the VS Code snap terminal), with the
# R4 plugged in by USB and the 8BitDo plugged in / connected.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARDUINO_CLI="$HOME/.local/bin/arduino-cli"
FQBN="arduino:renesas_uno:unor4wifi"
SKETCH="$SCRIPT_DIR/snake_ble"
VENV="$HOME/snakepad-venv"
PY="$VENV/bin/python"

# --- snap-terminal guard (evdev + BlueZ access fail under the VS Code snap) ---
if [ -n "${SNAP:-}" ]; then
  echo "!! You're in a snap-confined shell ($SNAP_NAME). Controller/BLE access will likely fail."
  echo "!! Run this from a native terminal (GNOME Terminal/Console)."
  echo
fi

# --- find the R4 serial port ---
PORT="${1:-}"
if [ -z "$PORT" ]; then
  PORT="$(ls /dev/ttyACM* 2>/dev/null | head -n1 || true)"
fi
if [ -z "$PORT" ]; then
  echo "ERROR: no /dev/ttyACM* found. Plug in the R4 (or pass the port: $0 /dev/ttyACM0)."
  exit 1
fi
echo "==> R4 port: $PORT"

# --- compile + upload ---
if [ ! -x "$ARDUINO_CLI" ]; then
  echo "ERROR: arduino-cli not found at $ARDUINO_CLI (run install_arduino_cli.sh first)."
  exit 1
fi
echo "==> Compiling snake_ble ..."
"$ARDUINO_CLI" compile --fqbn "$FQBN" "$SKETCH"
echo "==> Uploading to $PORT ..."
"$ARDUINO_CLI" upload -p "$PORT" --fqbn "$FQBN" "$SKETCH"

# --- python venv for the bridge ---
if [ ! -x "$PY" ]; then
  echo "==> Creating venv at $VENV ..."
  python3 -m venv "$VENV"
fi
if ! "$PY" -c "import bleak, evdev, pygame" >/dev/null 2>&1; then
  echo "==> Installing bleak + evdev + pygame ..."
  "$PY" -m pip install --upgrade pip >/dev/null
  "$PY" -m pip install bleak evdev pygame
fi

# --- let the R4 reboot and start advertising ---
echo "==> Waiting for the R4 to boot & advertise (3s) ..."
sleep 3

# --- launch the controller GUI (auto-detects the pad, connects to the R4) ---
echo "==> Launching controller GUI (close the window or Ctrl+C to stop) ..."
exec "$PY" "$SCRIPT_DIR/controller_gui.py"
