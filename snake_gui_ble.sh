#!/usr/bin/env bash
#
# snake_gui_ble.sh — play the Arduino snake with the web GUI, over BLE.
# USB-C is POWER ONLY (after flashing); the R4 talks to the laptop via Bluetooth.
#
#   controller --BT--> laptop --Gamepad API--> browser --/send--> BLE --> R4 (snake_ble)
#
# One-time: the R4 needs BLE firmware (Arduino IDE -> Tools -> Firmware Updater).
# Run from a NATIVE terminal.
#
set -uo pipefail

DIR="$HOME/CAN_Takeover"
PORT="${PORT:-8000}"
URL="http://localhost:$PORT/controller_web.html"
ARDUINO_CLI="$HOME/.local/bin/arduino-cli"
FQBN="arduino:renesas_uno:unor4wifi"
SERPORT="$(ls /dev/ttyACM* 2>/dev/null | head -n1 || true)"

# 1) snap browser gamepad access
if command -v snap >/dev/null 2>&1; then
  for B in firefox chromium chromium-browser brave; do
    snap list "$B" >/dev/null 2>&1 || continue
    STATE="$(snap connections "$B" 2>/dev/null | awk -v p="$B:joystick" '$2==p{print $3}')"
    [ "$STATE" = ":joystick" ] || sudo snap connect "$B:joystick" 2>/dev/null || true
  done
fi

# 2) flash snake_ble (uses USB data ONLY for this flash; after that USB-C = power)
if [ -n "$SERPORT" ] && [ -x "$ARDUINO_CLI" ]; then
  echo "==> Flashing snake_ble -> $SERPORT (last time USB carries data) ..."
  if "$ARDUINO_CLI" compile --fqbn "$FQBN" "$DIR/snake_ble"; then
    "$ARDUINO_CLI" upload -p "$SERPORT" --fqbn "$FQBN" "$DIR/snake_ble" || echo "!! upload failed (continuing)"
  fi
  sleep 3   # let it reboot + start advertising
else
  echo "!! No /dev/ttyACM* or arduino-cli — skipping flash (assuming R4 already runs snake_ble)."
fi

# 3) pick a python that has bleak, then start the BLE bridge
PY="python3"
for cand in "$HOME/openarm-venv/bin/python" "$HOME/snakepad-venv/bin/python"; do
  [ -x "$cand" ] && "$cand" -c "import bleak" >/dev/null 2>&1 && PY="$cand" && break
done
if ! "$PY" -c "import bleak" >/dev/null 2>&1; then
  echo "!! bleak not installed. Run: bash install_openarm.sh   (or pip install bleak)"; exit 1
fi
# clear any previous instance so Ctrl+C cleanly owns this one
PORT="$PORT" bash "$DIR/stop_gui.sh" >/dev/null 2>&1 || true

echo "==> Starting BLE web bridge ($PY) on :$PORT ..."
PORT="$PORT" "$PY" "$DIR/web_bridge_ble.py" &
SRV=$!
trap 'echo; echo "Stopping bridge ..."; kill "$SRV" 2>/dev/null; wait "$SRV" 2>/dev/null; exit 0' INT TERM
sleep 3

# 4) open and stay attached so Ctrl+C stops the bridge
echo "==> Opening $URL"
xdg-open "$URL" >/dev/null 2>&1 || echo "   Open manually: $URL"
echo
echo "USB-C is now power-only; R4 <-> laptop is BLE."
echo "Press a button to connect the pad; D-pad/sticks steer; A = start/restart."
echo "Running. Press Ctrl+C here to stop.   Log: nothing (foreground)."
wait "$SRV"
