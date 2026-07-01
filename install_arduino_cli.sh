#!/usr/bin/env bash
#
# Install arduino-cli (no sudo) and set up everything needed to build/upload
# the controller_led sketch for the Arduino UNO R4 WiFi.
#
# Usage:  bash install_arduino_cli.sh
#
set -euo pipefail

BINDIR="$HOME/.local/bin"
CLI="$BINDIR/arduino-cli"
FQBN="arduino:renesas_uno:unor4wifi"

echo "==> Installing arduino-cli into $BINDIR"
mkdir -p "$BINDIR"
# Official installer; BINDIR controls where the binary lands.
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR="$BINDIR" sh

# Make sure this shell (and future ones) can find it.
export PATH="$BINDIR:$PATH"
if ! grep -qs "$BINDIR" "$HOME/.bashrc" 2>/dev/null; then
  echo "export PATH=\"$BINDIR:\$PATH\"" >> "$HOME/.bashrc"
  echo "    (added $BINDIR to PATH in ~/.bashrc — open a new terminal or 'source ~/.bashrc')"
fi

echo "==> arduino-cli version"
"$CLI" version

echo "==> Initializing config + board index"
"$CLI" config init --overwrite
"$CLI" core update-index

echo "==> Installing UNO R4 core (arduino:renesas_uno)"
"$CLI" core install arduino:renesas_uno

echo "==> Installing ArduinoGraphics library"
"$CLI" lib install ArduinoGraphics

echo
echo "============================================================"
echo " Done. Quick reference:"
echo
echo "   Compile:  arduino-cli compile --fqbn $FQBN controller_led"
echo "   Upload :  arduino-cli upload -p /dev/ttyACM0 --fqbn $FQBN controller_led"
echo "   Boards :  arduino-cli board list"
echo
echo " (If 'arduino-cli' isn't found, run:  source ~/.bashrc )"
echo "============================================================"
