#!/usr/bin/env bash
#
# install_openarm.sh — provision everything to operate the OpenArm 1.0 with an
# Arduino UNO R4 WiFi over CAN 2.0, controlled by a game controller (8BitDo).
#
# Installs:
#   - Arduino toolchain: arduino-cli + UNO R4 core (native CAN 2.0) + libs
#   - CAN utilities: can-utils + python-can (motor config/test via USB-CAN)
#   - Controller stack: evdev / bleak / pygame / pyserial + evtest/joystick
#   - Bluetooth (bluez) for pairing the controller
#   - (optional) LeRobot/OpenArm Python stack with:  --lerobot
#
# Usage:
#   bash install_openarm.sh            # core toolchain
#   bash install_openarm.sh --lerobot  # also install LeRobot (large ML deps)
#
set -euo pipefail

VENV="$HOME/openarm-venv"
BINDIR="$HOME/.local/bin"
ARDUINO_CLI="$BINDIR/arduino-cli"
CORE="arduino:renesas_uno"

WANT_LEROBOT=0
[ "${1:-}" = "--lerobot" ] && WANT_LEROBOT=1

echo "============================================================"
echo " OpenArm 1.0 toolchain installer"
echo "   Arduino R4 + CAN 2.0  |  CAN utils  |  controller bridge"
[ "$WANT_LEROBOT" = 1 ] && echo "   + LeRobot/OpenArm Python stack (heavy)"
echo "============================================================"

# ---------------------------------------------------------------- 1. apt
echo "==> System packages (sudo) ..."
sudo apt-get update
sudo apt-get install -y \
  build-essential git curl \
  python3 python3-venv python3-pip python3-dev \
  can-utils \
  bluez \
  evtest joystick

# ---------------------------------------------------------------- 2. groups
echo "==> Adding $USER to 'dialout' (serial) and 'input' (controller) ..."
sudo usermod -aG dialout,input "$USER" || true

# ---------------------------------------------------------------- 3. arduino-cli
mkdir -p "$BINDIR"
if [ ! -x "$ARDUINO_CLI" ]; then
  echo "==> Installing arduino-cli ..."
  curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR="$BINDIR" sh
fi
if ! grep -qs "$BINDIR" "$HOME/.bashrc"; then
  echo "export PATH=\"$BINDIR:\$PATH\"" >> "$HOME/.bashrc"
fi
export PATH="$BINDIR:$PATH"

echo "==> Arduino core (UNO R4 = native CAN 2.0 via Arduino_CAN) + libraries ..."
"$ARDUINO_CLI" config init --overwrite
"$ARDUINO_CLI" core update-index
"$ARDUINO_CLI" core install "$CORE"
"$ARDUINO_CLI" lib install ArduinoGraphics    # LED-matrix text/graphics
"$ARDUINO_CLI" lib install ArduinoBLE         # optional wireless link to the R4
# (Arduino_CAN ships inside the renesas_uno core — no separate install.)

# ---------------------------------------------------------------- 3b. Arduino IDE
# Idempotent: skip entirely if the IDE is already set up or present.
IDE_NAME="arduino-ide_2.3.10_Linux_64bit.AppImage"
APPDIR="$HOME/Applications"
if [ -x "$BINDIR/arduino-ide" ]; then
  echo "==> Arduino IDE already set up ('$BINDIR/arduino-ide') — skipping."
elif [ -e "$HOME/Downloads/squashfs-root/arduino-ide" ]; then
  echo "==> Arduino IDE already present (extracted in ~/Downloads/squashfs-root) — skipping."
else
  echo "==> Arduino IDE (AppImage) ..."
  sudo apt-get install -y libfuse2t64 || sudo apt-get install -y libfuse2 || true
  mkdir -p "$APPDIR"
  # find an existing AppImage anywhere sensible — do NOT re-download if present
  IDE_PATH=""
  for c in "$APPDIR"/arduino-ide_*_Linux_64bit.AppImage "$HOME"/Downloads/arduino-ide_*_Linux_64bit.AppImage; do
    [ -f "$c" ] && IDE_PATH="$c" && break
  done
  if [ -n "$IDE_PATH" ]; then
    echo "    found existing AppImage: $IDE_PATH (not re-downloading)"
  else
    echo "    no AppImage found; downloading $IDE_NAME ..."
    curl -fL -o "$APPDIR/$IDE_NAME" "https://downloads.arduino.cc/arduino-ide/$IDE_NAME" && IDE_PATH="$APPDIR/$IDE_NAME" || true
  fi
  if [ -n "$IDE_PATH" ]; then
    chmod +x "$IDE_PATH"
    cat > "$BINDIR/arduino-ide" <<EOF
#!/usr/bin/env bash
exec "$IDE_PATH" "\$@"
EOF
    chmod +x "$BINDIR/arduino-ide"
    mkdir -p "$HOME/.local/share/applications"
    cat > "$HOME/.local/share/applications/arduino-ide.desktop" <<EOF
[Desktop Entry]
Name=Arduino IDE
Exec=$IDE_PATH
Icon=arduino
Type=Application
Categories=Development;IDE;
Terminal=false
EOF
    echo "    Arduino IDE ready: $IDE_PATH  (also 'arduino-ide' / app menu)"
  else
    echo "    !! Could not obtain the IDE — get it from arduino.cc into $APPDIR"
  fi
fi

# ---------------------------------------------------------------- 4. python venv
echo "==> Python venv at $VENV (controller + CAN libs) ..."
python3 -m venv "$VENV"
"$VENV/bin/pip" install --upgrade pip >/dev/null
# Required libs (bridge + CAN). These must succeed.
"$VENV/bin/pip" install evdev pyserial bleak python-can
# Optional GUI dep. NON-FATAL: the C console UI (run_cui.sh) needs no pygame,
# and pygame-ce is a drop-in that imports as 'pygame' where wheels exist.
if "$VENV/bin/pip" install pygame; then
  echo "    pygame installed."
elif "$VENV/bin/pip" install pygame-ce; then
  echo "    pygame-ce installed (drop-in for the GUI)."
else
  echo "    !! pygame unavailable for this Python — skipping the graphical GUI."
  echo "       Use the C console UI (run_cui.sh) or the headless bridge instead."
fi

# ---------------------------------------------------------------- 5. optional LeRobot
if [ "$WANT_LEROBOT" = 1 ]; then
  echo "==> Installing LeRobot (this is large: torch & friends) ..."
  "$VENV/bin/pip" install lerobot || \
    echo "!! lerobot install failed — see https://docs.openarm.dev / huggingface lerobot docs"
fi

# ---------------------------------------------------------------- done
cat <<EOF

============================================================
 Done.

 IMPORTANT — finish these manually:
   1) LOG OUT and back IN  (so dialout/input group membership applies).
   2) New shell or 'source ~/.bashrc'  (so 'arduino-cli' is on PATH).

 Quick reference:
   Arduino IDE         : launch 'arduino-ide' or use the app menu (NATIVE
                         session, not the VS Code snap terminal)
                         -> Tools > Firmware Updater  enables BLE on the R4
   Arduino venv python : $VENV/bin/python
   Flash + GUI (BLE)   : bash ~/CAN_Takeover/flash_and_run.sh
   Flash + C console UI: bash ~/CAN_Takeover/run_cui.sh
   CAN sniff/test      : candump can0          (needs a USB-CAN adapter up)
   Bring up CAN @1Mbps : sudo ip link set can0 type can bitrate 1000000 && \\
                         sudo ip link set can0 up

 Motor setup notes:
   - DAMIAO motors must be set to CLASSIC CAN 2.0 @ 1 Mbit (+ nonzero timeout).
     Use the DAMIAO debug tool (Windows) or can-utils/python-can on Linux.
   - R4 drives the bus via native CAN (D10/D13) + an external transceiver.
============================================================
EOF
