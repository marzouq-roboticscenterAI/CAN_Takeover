#!/usr/bin/env bash
#
# snake_gui.sh — play the Arduino snake with the web controller GUI.
#   1) grants gamepad access to snap browsers
#   2) flashes snake_led (serial sketch) to the R4
#   3) starts the web bridge (serves the page + forwards tokens over serial)
#   4) opens the browser
#
# Controller -> browser (Gamepad API) -> /send -> serial -> R4 snake.
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

# 2) flash the serial snake sketch
if [ -n "$SERPORT" ] && [ -x "$ARDUINO_CLI" ]; then
  echo "==> Flashing snake_led -> $SERPORT ..."
  if "$ARDUINO_CLI" compile --fqbn "$FQBN" "$DIR/snake_led"; then
    "$ARDUINO_CLI" upload -p "$SERPORT" --fqbn "$FQBN" "$DIR/snake_led" || echo "!! upload failed (continuing)"
  fi
  sleep 2
else
  echo "!! No /dev/ttyACM* or arduino-cli — skipping flash (GUI will visualize only)."
fi

# 3) pick a python that has pyserial, then start the bridge
PY="python3"
for cand in "$HOME/openarm-venv/bin/python" "$HOME/snakepad-venv/bin/python"; do
  [ -x "$cand" ] && "$cand" -c "import serial" >/dev/null 2>&1 && PY="$cand" && break
done
# clear any previous instance so Ctrl+C cleanly owns this one
PORT="$PORT" bash "$DIR/stop_gui.sh" >/dev/null 2>&1 || true

echo "==> Starting web bridge ($PY) on :$PORT ..."
PORT="$PORT" "$PY" "$DIR/web_bridge.py" "${SERPORT:-}" &
SRV=$!
trap 'echo; echo "Stopping bridge ..."; kill "$SRV" 2>/dev/null; wait "$SRV" 2>/dev/null; exit 0' INT TERM
sleep 2

# 4) open and stay attached so Ctrl+C stops the bridge
echo "==> Opening $URL"
xdg-open "$URL" >/dev/null 2>&1 || echo "   Open manually: $URL"
echo
echo "Play: press a button to connect the pad. D-pad/sticks steer; A = start/restart."
echo "Running. Press Ctrl+C here to stop."
wait "$SRV"
