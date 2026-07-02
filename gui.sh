#!/usr/bin/env bash
#
# gui.sh — one command: browser-based controller visualizer (like dualshock-tools).
#   1) grants gamepad access to snap browsers (Ubuntu sandboxes /dev/input)
#   2) serves the page on http://localhost (secure context for the Gamepad API)
#   3) opens it in the browser
#
# Auto-detects the connected controller (USB or Bluetooth) and shows inputs live.
# Run from a NATIVE terminal.
#
set -uo pipefail    # not -e: the snap/open steps are best-effort

DIR="$HOME/CAN_Takeover"
PAGE="controller_web.html"
PORT="${PORT:-8000}"
URL="http://localhost:$PORT/$PAGE"

# 1) Snap browsers block gamepad access by default — connect the 'joystick' plug.
if command -v snap >/dev/null 2>&1; then
  for B in firefox chromium chromium-browser brave; do
    snap list "$B" >/dev/null 2>&1 || continue
    STATE="$(snap connections "$B" 2>/dev/null | awk -v p="$B:joystick" '$2==p{print $3}')"
    if [ "$STATE" = ":joystick" ]; then
      echo "==> snap '$B' already has gamepad access."
    else
      echo "==> Granting gamepad access to snap '$B' (sudo) ..."
      if sudo snap connect "$B:joystick" 2>/dev/null; then
        echo "    connected."
      else
        echo "    (skipped — needs sudo, or this snap has no joystick plug)"
      fi
    fi
  done
fi

# 2) Clear any previous instance so Ctrl+C cleanly owns this one.
PORT="$PORT" bash "$DIR/stop_gui.sh" >/dev/null 2>&1 || true

# 3) Serve over localhost (secure context for the Gamepad API), in the foreground.
echo "==> Serving $DIR at http://localhost:$PORT ..."
python3 -m http.server "$PORT" --directory "$DIR" >/tmp/controller_web.log 2>&1 &
SRV=$!
trap 'echo; echo "Stopping server ..."; kill "$SRV" 2>/dev/null; wait "$SRV" 2>/dev/null; exit 0' INT TERM
sleep 1

# 4) Open the browser and stay attached so Ctrl+C stops the server.
echo "==> Opening $URL  (press any button on the controller)"
xdg-open "$URL" >/dev/null 2>&1 || echo "    Open manually in Chrome/Firefox: $URL"
echo "    Running. Press Ctrl+C here to stop the server."
wait "$SRV"
