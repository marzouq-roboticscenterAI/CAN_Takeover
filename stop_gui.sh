#!/usr/bin/env bash
#
# stop_gui.sh — stop the controller-visualizer server / snake web bridge.
# Catches both the plain http.server (gui.sh) and web_bridge.py (snake_gui.sh).
# Matches the same PORT (default 8000; override with PORT=9000 ...).
#
set -uo pipefail

PORT="${PORT:-8000}"
DIR="$HOME/CAN_Takeover"

PIDS="$( { pgrep -f "web_bridge"; \
           pgrep -f "http.server $PORT"; } 2>/dev/null | sort -u | tr '\n' ' ' )"

if [ -z "${PIDS// /}" ]; then
  echo "No controller-web server running on port $PORT."
  exit 0
fi

echo "Stopping PID(s): $PIDS"
# shellcheck disable=SC2086
kill $PIDS 2>/dev/null || true
sleep 1
for p in $PIDS; do
  kill -0 "$p" 2>/dev/null && kill -9 "$p" 2>/dev/null || true   # force survivors
done

if { pgrep -f "web_bridge" || pgrep -f "http.server $PORT"; } >/dev/null 2>&1; then
  echo "!! Still alive:"; pgrep -af "web_bridge"; pgrep -af "http.server $PORT"
  exit 1
fi
echo "Stopped ✓  (port $PORT free)"
