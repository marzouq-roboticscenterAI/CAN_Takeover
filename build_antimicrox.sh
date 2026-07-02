#!/usr/bin/env bash
#
# build_antimicrox.sh — install deps and build the AntiMicroX fork (~/antimicrox).
# AntiMicroX is a Qt6 + SDL2 GUI that detects a gamepad and shows/reads its inputs.
#
# Run from a NATIVE terminal (needs sudo for apt; the GUI needs a display).
#
set -euo pipefail

REPO="$HOME/antimicrox"
[ -d "$REPO" ] || { echo "ERROR: $REPO not found (fork/clone it first)."; exit 1; }

echo "==> Installing build dependencies (sudo) ..."
sudo apt-get update
sudo apt-get install -y \
  g++ cmake extra-cmake-modules \
  qt6-base-dev qt6-tools-dev-tools libqt6core5compat6-dev qt6-tools-dev \
  libsdl2-dev libxi-dev libxtst-dev libx11-dev \
  itstool gettext ninja-build

echo "==> Configuring + building (Ninja) ..."
cmake -S "$REPO" -B "$REPO/build" -G Ninja
cmake --build "$REPO/build"

echo
echo "============================================================"
echo " Built:  $REPO/build/bin/antimicrox"
echo " Run it (controller is already connected):"
echo "     $REPO/build/bin/antimicrox"
echo " Its GUI auto-detects the pad and highlights inputs live."
echo "============================================================"
