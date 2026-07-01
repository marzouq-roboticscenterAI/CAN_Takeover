#!/usr/bin/env bash
#
# run_openarm.sh — bring up CAN FD, activate the LeRobot env, open the
# browser visualization (Rerun web viewer), and print the operate command.
#
# "Normal" OpenArm operation is CLI (lerobot-teleoperate / lerobot-record) over
# SocketCAN; the localhost web page is Rerun's live viewer (or the dataset
# viewer). There is no official web GUI that *drives* the arm.
#
# Usage:  bash run_openarm.sh
#
set -uo pipefail            # not -e: web-viewer steps are best-effort

VENV="$HOME/openarm-venv"
BITRATE=1000000            # CAN FD nominal (1 Mbit)
DBITRATE=5000000           # CAN FD data phase (5 Mbit)
PORTAL="http://localhost:9090"

echo "============================================================"
echo " OpenArm — CAN FD bring-up + visualization"
echo "============================================================"

# --- 1. find CAN interfaces (usb-can adapter shows up as canN via socketcan) ---
mapfile -t CANS < <(ip -br link show type can 2>/dev/null | awk '{print $1}')
if [ "${#CANS[@]}" -eq 0 ]; then
  echo "!! No CAN interface found."
  echo "   - Is the USB-CAN adapter plugged in and Linux-supported (gs_usb/socketcan)?"
  echo "   - Check:  ip -br link | grep -i can    and    dmesg | grep -i can"
  exit 1
fi
echo "Found CAN interface(s): ${CANS[*]}"

# --- 2. configure each as CAN FD (1M nominal / 5M data) and bring up ---
for IF in "${CANS[@]}"; do
  echo "==> $IF -> CAN FD ${BITRATE}/${DBITRATE} ..."
  sudo ip link set "$IF" down 2>/dev/null || true
  if command -v openarm-can-configure-socketcan >/dev/null 2>&1; then
    sudo openarm-can-configure-socketcan "$IF" -fd -b "$BITRATE" -d "$DBITRATE" \
      || sudo ip link set "$IF" type can bitrate "$BITRATE" dbitrate "$DBITRATE" fd on
  else
    sudo ip link set "$IF" type can bitrate "$BITRATE" dbitrate "$DBITRATE" fd on
  fi
  sudo ip link set "$IF" up
  ip -details -brief link show "$IF" | sed 's/^/    /'
done

# --- 3. activate the python env; check lerobot ---
if [ -d "$VENV" ]; then
  # shellcheck disable=SC1091
  source "$VENV/bin/activate"
else
  echo "!! venv $VENV missing — run: bash install_openarm.sh --lerobot"
fi
if ! python -c "import lerobot" 2>/dev/null; then
  echo "!! lerobot not installed in the venv — run: bash install_openarm.sh --lerobot"
fi

# --- 4. web portal: Rerun web viewer (best-effort), then open the browser ---
if command -v rerun >/dev/null 2>&1; then
  echo "==> Launching Rerun web viewer at $PORTAL ..."
  ( rerun --serve-web >/tmp/rerun.log 2>&1 & ) \
    || ( rerun --serve >/tmp/rerun.log 2>&1 & ) || true
  sleep 2
  ( xdg-open "$PORTAL" >/dev/null 2>&1 & ) || echo "   open $PORTAL manually"
else
  echo "!! 'rerun' not found (installed with lerobot). Skipping the web viewer."
fi

# --- 5. how to actually operate the arm ---
LEADER="${CANS[1]:-can1}"
FOLLOWER="${CANS[0]:-can0}"
cat <<EOF

------------------------------------------------------------
CAN is up. Operate the arm from here.

Leader -> follower TELEOPERATION (needs two arms / two CAN buses):
  lerobot-teleoperate \\
    --robot.type=openarm_follower --robot.port=$FOLLOWER --robot.side=right --robot.id=my_follower \\
    --teleop.type=openarm_leader  --teleop.port=$LEADER  --teleop.id=my_leader \\
    --display_data=true

Single follower arm (no leader) — scan/verify motors first:
  candump $FOLLOWER            # watch bus traffic
  lerobot-setup-can --mode=test --interfaces=$FOLLOWER

'--display_data=true' streams live visualization to the Rerun page ($PORTAL).
------------------------------------------------------------
EOF
