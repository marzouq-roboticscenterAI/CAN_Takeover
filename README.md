# CAN_Takeover — Controller → Arduino UNO R4 WiFi (Snake demo)

A game controller drives a **Snake** game on the Arduino UNO R4 WiFi's 12×8 LED
matrix, plus a live **controller GUI** on the laptop. Input is read on the laptop
and sent to the R4 — over **USB serial** or **BLE**.

This started as the CAN/OpenArm robot-arm project (see `MEMORY`/hardware notes);
this folder is the controller-input + display demo built along the way.

---

## TL;DR

```bash
# one-time: install arduino-cli + board core + libs
bash install_arduino_cli.sh

# flash the R4 (BLE) + launch the controller GUI  (run from a NATIVE terminal)
bash flash_and_run.sh
```

The R4 boots, advertises over BLE as **`SnakeR4`**, the GUI window pops up,
auto-detects your controller, highlights presses, and forwards them to the R4.
The **USB-C cable is then only power + flashing.**

---

## Why the laptop is in the loop (important)

The end goal was "controller talks straight to the Arduino, no laptop." After
reverse-engineering the hardware, that is **not possible on the UNO R4**:

- The R4's USB is **device-only** (no USB host) → can't read a wired controller.
- The R4's ESP32-S3 radio is **BLE-only (no Bluetooth Classic)**, and `ArduinoBLE`
  can't do the secure HID bonding a controller needs → can't host one over BLE.

So a controller (8BitDo/PS5/Xbox) **cannot connect directly to the R4.** The
laptop — a full Bluetooth/USB host — reads the controller and relays it. The R4
is just a BLE **peripheral**.

*Laptop-free options (future):* a **USB Host Shield** on the R4, or an
**original ESP32 + Bluepad32** forwarding to the R4 over UART. See hardware notes.

---

## Architecture

```
controller ──(USB or Bluetooth)──> laptop ──(USB serial OR BLE)──> R4 ──> LED matrix
            read via evdev          relay                          snake game
```

Two interchangeable transports laptop→R4:

| Transport | Laptop script        | R4 sketch          | USB-C is…        |
|-----------|----------------------|--------------------|------------------|
| USB serial| `controller_to_arduino.py` | `snake_led`  | power + data     |
| **BLE**   | `controller_gui.py` / `controller_to_ble.py` | `snake_ble` | **power + flash only** |

---

## Files

| File | Role |
|------|------|
| `snake_ble/snake_ble.ino` | **R4 = BLE peripheral** `SnakeR4`; snake on the matrix; input via a writable BLE characteristic. |
| `snake_led/snake_led.ino` | R4 snake driven over **USB serial** (no BLE). |
| `controller_led/controller_led.ino` | Shows the last button/stick as text/arrows on the matrix (USB serial). |
| `controller_gui.py` | **Laptop GUI** (pygame): auto-detects the pad, draws it, highlights presses, and forwards to the R4 over BLE (bleak). |
| `controller_to_ble.py` | Headless laptop→R4 **BLE** bridge (no GUI). |
| `controller_to_arduino.py` | Headless laptop→R4 **USB-serial** bridge. |
| `controller_cui.c` | **Pure-C console UI** (no deps): auto-detects the pad, draws it in the terminal with live highlights, forwards over **USB serial** to `snake_led`. |
| `install_openarm.sh` | **One-shot installer** for the whole stack: Arduino IDE + arduino-cli + R4 core/libs, `can-utils`+`python-can`, controller libs (evdev/bleak/pygame/pyserial), bluez, groups. `--lerobot` adds the OpenArm/LeRobot Python stack. |
| `install_arduino_cli.sh` | Just `arduino-cli` + UNO R4 core + `ArduinoGraphics` + `ArduinoBLE` (subset of the above). |
| `flash_and_run.sh` | Compiles+flashes `snake_ble`, sets up the Python venv, launches `controller_gui.py` (pygame GUI, BLE). |
| `run_cui.sh` | Builds `controller_cui.c`, flashes `snake_led`, runs the C console UI (serial). |

---

## Setup

### Arduino side
```bash
bash install_arduino_cli.sh
```
Installs `arduino-cli` to `~/.local/bin`, the `arduino:renesas_uno` core, and the
`ArduinoGraphics` + `ArduinoBLE` libraries.

> **BLE firmware:** the R4's ESP32-S3 needs connectivity firmware with BLE enabled.
> If the matrix scrolls **`BLE?`** at boot, update it once via the Arduino IDE:
> **Tools → Firmware Updater**.

### Laptop side
`flash_and_run.sh` builds a venv automatically. Manually it's:
```bash
sudo apt install bluez
python3 -m venv ~/snakepad-venv
~/snakepad-venv/bin/pip install bleak evdev pygame
```

### Permissions (one-time)
```bash
sudo usermod -aG dialout,input $USER   # serial + /dev/input access; then log out/in
```

> **Run scripts from a NATIVE terminal** (GNOME Terminal/Console) — the VS Code
> **snap** terminal blocks `/dev/input`, serial, BLE, and FUSE.

---

## Connect the controller to the laptop (Ubuntu 26.04, Bluetooth)

The 8BitDo pairs to the **laptop** (a real Bluetooth host) — not to the Arduino.

**1. Put the controller in Bluetooth pairing mode.**
For an 8BitDo in Xbox/XInput mode: power on by holding **Start**, then hold the
small **pair** button (top edge) for ~3 s until the LEDs **fast-blink**.

**2a. GUI way — GNOME Settings:**
- Open **Settings → Bluetooth** (Bluetooth on).
- Pick the controller from the list → it pairs and connects.

**2b. CLI way — `bluetoothctl`:**
```bash
bluetoothctl
power on
agent on
default-agent
scan on                 # wait for the pad; note its MAC (XX:XX:XX:XX:XX:XX)
pair  XX:XX:XX:XX:XX:XX
trust XX:XX:XX:XX:XX:XX  # auto-reconnect next time
connect XX:XX:XX:XX:XX:XX
scan off
exit
```

**3. Verify it's seen:**
```bash
sudo evtest        # should list the controller; pick it and watch events
# or:
ls /dev/input/by-id/ | grep -i pad
```

USB also works — just plug it in; `evdev` reads it identically and no pairing is
needed.

---

## Run

```bash
bash flash_and_run.sh                  # auto-detects R4 port, or:
bash flash_and_run.sh /dev/ttyACM0
```

1. R4 flashes, reboots, advertises `SnakeR4` → matrix blinks **four corners**.
2. GUI window opens, finds the controller, shows live highlights; status line
   shows `R4: connected` once linked.
3. Press **Start/A** → the snake game begins on the R4's matrix.
4. **Controls:** d-pad or either stick = steer; **A** = restart after game over.

### USB-serial fallback (no BLE)
```bash
~/.local/bin/arduino-cli upload -p /dev/ttyACM0 --fqbn arduino:renesas_uno:unor4wifi snake_led
~/snakepad-venv/bin/python controller_to_arduino.py
```

### Pure-C console UI (no Python, no pygame)
```bash
bash run_cui.sh            # builds controller_cui.c, flashes snake_led, runs it
```
Reads the controller and draws it in the terminal with live button highlights,
forwarding over USB serial. Build manually with:
`gcc -O2 -o controller_cui controller_cui.c`.
Note: this path uses USB serial, so the cable carries data (not power-only).
A pure-C **BLE** version would require a BlueZ D-Bus GATT client (much larger);
for power-only BLE without pygame, use the headless `controller_to_ble.py`.

---

## Token protocol (laptop → R4)

Short ASCII strings; the R4 reads the **last char** for direction:

| Token | Meaning |
|-------|---------|
| `^ v < >` | up / down / left / right (sent as `D^`, `L^`, `R>`, … — prefix = source) |
| `A` | A button (start / restart) |
| `STR` | Start button |
| `B X Y LB RB L3 R3 …` | other buttons (snake ignores; `controller_led` shows them) |

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| Matrix scrolls **`BLE?`** | Update R4 WiFi/BLE firmware: IDE → Tools → **Firmware Updater**. |
| GUI: "No controller found" | Pair/plug the pad first; check `sudo evtest`; be in `input` group. |
| GUI: `R4: not found` | R4 not powered / not running `snake_ble` / still flashing. |
| `Permission denied` on `/dev/ttyACM0` | Join `dialout`, log out/in. |
| Nothing works from VS Code terminal | Use a **native** terminal (snap confinement). |
| `arduino-cli: not found` | Run `bash install_arduino_cli.sh` (adds `~/.local/bin` to PATH). |

---

## Hardware notes

- **Board:** Arduino UNO R4 WiFi (RA4M1 + ESP32-S3). 12×8 LED matrix is on the R4.
- **Controller:** 8BitDo in Xbox-360/XInput mode (USB vendor `0x3537`); Classic BT
  when wireless.
- The broader project targets controlling **OpenArm / DAMIAO** motors over **CAN
  2.0** from the R4 — see the CAN/hardware notes for that side.
