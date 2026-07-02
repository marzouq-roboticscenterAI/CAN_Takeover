# CAN_Takeover

Workbench for driving an **Arduino UNO R4 WiFi** — the long-term goal is controlling
an **OpenArm 1.0** (DAMIAO motors) over **CAN 2.0**, and along the way a full set of
**game-controller → Snake** demos were built to work out the input/wireless plumbing.

Everything here falls into four buckets:
1. **Arduino sketches** (`*/`*.ino`) — run on the R4.
2. **Flipper Zero app** (`flipper_snake/`) — a `.fap`.
3. **Laptop programs** — read a controller and bridge it to the R4 (Python / C / HTML).
4. **Setup & launcher scripts** (`*.sh`).

---

## Hardware facts (learned the hard way)

- **UNO R4 WiFi** = Renesas RA4M1 (runs your sketch) + ESP32‑S3 (WiFi/BLE radio) + 12×8 LED matrix.
- **Native CAN is on D10 (CANTX) / D13 (CANRX)** — classic **CAN 2.0 only**, and needs an **external transceiver** (D10/D13 are logic, not the bus). No CAN‑FD natively.
- **The R4 cannot host a game controller directly.** Its USB is device‑only (no USB host) and `ArduinoBLE` can't do the encrypted pairing/bonding a real controller (Xbox/PS5) requires. Its radio *can* do plain BLE central (proven with the Flipper), just not controller HID hosting. → controllers go through the **laptop** or an **ESP32 + Bluepad32**.
- **The ESP32‑S3 radio is a locked co‑processor.** A **reflash or the reset button only resets the RA4M1** — if BLE gets wedged, you must **fully unplug USB‑C** to reset the radio.
- **BLE needs the ESP32‑S3 firmware that supports it** — if a sketch scrolls `BLE?`, update it once via Arduino IDE → **Tools → Firmware Updater**.
- **VS Code here is a snap** — its integrated terminal blocks `/dev/input`, serial, BLE, FUSE. **Run everything from a native terminal.**

---

## How controller input reaches the game (the transports)

| Transport | Reads controller | Sends to R4 | R4 sketch | USB‑C is… |
|---|---|---|---|---|
| **USB serial** | laptop (`evdev`) | USB serial | `snake_led` | power + data |
| **BLE (R4 = peripheral)** | laptop (`evdev`) | BLE (bleak) | `snake_ble` | **power only** |
| **Flipper** | Flipper buttons | BLE (Flipper=peripheral) | `snake_flipper` | power only |
| **Web (Gamepad API)** | browser | localhost bridge → serial/BLE | `snake_led`/`snake_ble` | depends |

Shared **token protocol** (newline‑terminated): `^ v < >` = directions, `A` = start/restart,
`STR` = Start. The R4 reads the last char for direction.

---

## Arduino sketches

| Folder | What it does |
|---|---|
| `Blink/` | Stock blink — smoke test that uploading works. |
| `controller_led/` | Shows the **last input** on the LED matrix (letters for buttons, arrows for sticks). Input over **USB serial**. |
| `snake_led/` | **Snake** on the matrix, controlled over **USB serial**. Title screen → OK/A starts, d‑pad/sticks steer. |
| `snake_ble/` | Snake with the **R4 as a BLE peripheral** (`SnakeR4`); the laptop connects as central and writes tokens. Also broadcasts a 16‑byte **game‑status** notify (score + snake bitmap) for the web mirror. |
| `snake_flipper/` | Snake with the **R4 as a BLE central** that connects to the **Flipper** (which advertises 16‑bit UUID `0xF00D`) and subscribes to its token characteristic. Has serial debug + `BLE.begin()` retry. |

Build/flash any of them with `arduino-cli` (FQBN `arduino:renesas_uno:unor4wifi`) or the IDE.

---

## Flipper Zero app — `flipper_snake/`

A FAP that makes the **Flipper a BLE peripheral** so the Arduino (`snake_flipper`) connects to it
(the Flipper can't be a BLE central). The Flipper's d‑pad/OK stream tokens to the Arduino; the
screen shows which buttons you press.

| File | Role |
|---|---|
| `application.fam` | Build manifest (name, category, stack size). |
| `flipper_snake.c` | GUI + input: draws the d‑pad/OK, highlights presses, sends tokens, exits cleanly (stops advertising = "forgets" the Arduino). |
| `ble_snake.c` / `.h` | Custom BLE peripheral: advertises `0xF00D`, one NOTIFY characteristic (`9a1e0002`) for tokens, via `bt_profile_start` / `bt_profile_restore_default`. |
| `dist/flipper_snake.fap` | Built app (produced by `ufbt`). |

Build with **`ufbt`** (see `build_antimicrox`/tooling notes below). Installs to the Flipper SD at
`apps/Bluetooth/flipper_snake.fap`; `ufbt launch` builds+installs+runs it.
> Known cosmetic bug: the screen says "advertising" even when connected (the bt‑service status
> callback doesn't report "connected" for a custom profile). Gameplay still works.

---

## Laptop programs

**Read a controller and bridge it to the R4 / visualize it.**

| File | Role |
|---|---|
| `controller_to_arduino.py` | Headless bridge: 8BitDo via `evdev` → **USB serial** → `snake_led`. |
| `controller_to_ble.py` | Headless bridge: controller → **BLE** (bleak) → `snake_ble` (R4 peripheral). |
| `controller_gui.py` | **pygame** window: draws the controller + live highlights, forwards over BLE. (pygame can be hard to install on new Python — the web/CUI options avoid it.) |
| `controller_cui.c` | **Pure‑C console UI**: draws the controller in the terminal (ANSI), forwards over **USB serial**. No deps. Build: `gcc -O2 -o controller_cui controller_cui.c`. |
| `controller_web.html` | Browser **Gamepad API** visualizer (dualshock‑tools style) + posts tokens to `/send` + shows a live **snake‑board mirror** from `/status`. |
| `web_bridge.py` | Serves `controller_web.html` and forwards POSTed tokens over **USB serial**. |
| `web_bridge_ble.py` | Serves the page, forwards tokens over **BLE**, and exposes the R4's game status at `/status`. |
| `controller_cui` | Compiled binary of `controller_cui.c`. |

---

## Setup / install scripts

| Script | What it installs |
|---|---|
| `install_arduino_cli.sh` | `arduino-cli` + UNO R4 core + `ArduinoGraphics` + `ArduinoBLE`. |
| `install_openarm.sh` | Everything for the OpenArm project: Arduino **IDE** + arduino‑cli + core/libs, `can-utils` + `python-can`, controller libs (evdev/bleak/pygame/pyserial) in `~/openarm-venv`, bluez, groups. `--lerobot` adds the LeRobot stack. |
| `build_antimicrox.sh` | Installs Qt6/SDL2 deps and builds the AntiMicroX fork in `~/antimicrox` (a GUI controller mapper we forked; superseded by `controller_web.html`). |

**Flipper tooling** (not a script here): `pipx install ufbt` then `pipx inject ufbt scons`, then
`ufbt` (build) / `ufbt launch` (deploy). Builds against official firmware API; loads on Momentum too.

---

## Launcher scripts

| Script | Runs |
|---|---|
| `flash_and_run.sh` | Flash `snake_ble` (BLE) + launch `controller_gui.py`. |
| `run_cui.sh` | Build `controller_cui.c`, flash `snake_led`, run the C console UI (serial). |
| `gui.sh` | Serve `controller_web.html` on localhost + open the browser (pure visualizer). Grants snap browsers gamepad access. Ctrl+C stops it. |
| `snake_gui.sh` | Web snake over **USB serial**: flash `snake_led`, run `web_bridge.py`, open browser. |
| `snake_gui_ble.sh` | Web snake over **BLE** (USB‑C power‑only): flash `snake_ble`, run `web_bridge_ble.py` (with live board mirror), open browser. |
| `stop_gui.sh` | Stop the localhost web server / bridge (any of the above). |
| `run_openarm.sh` | Bring up **CAN FD** SocketCAN (1M/5M), activate the venv, open the Rerun web viewer, print the `lerobot-teleoperate` command. |

All launchers assume a **native terminal**; the web/BLE ones prefer `~/openarm-venv` or `~/snakepad-venv`.

---

## Quick starts

**Snake, controller wired to laptop, over USB serial (simplest):**
```bash
bash run_cui.sh            # or: snake_gui.sh for the browser UI
```

**Snake, wireless (controller BT→laptop, laptop BLE→R4, USB‑C power‑only):**
```bash
bash snake_gui_ble.sh      # needs snake_ble flashed + R4 BLE firmware
```

**Snake from the Flipper (no laptop in the loop):**
```bash
# Flipper: build + install the FAP
cd ~/CAN_Takeover/flipper_snake && ufbt launch
# Arduino: flash snake_flipper, then open ARD Snake on the Flipper
arduino-cli upload -p /dev/ttyACM0 --fqbn arduino:renesas_uno:unor4wifi snake_flipper
# If the Arduino won't connect: FULLY unplug/replug its USB-C (resets the radio)
```

**OpenArm over CAN FD from the laptop:**
```bash
bash install_openarm.sh --lerobot
bash run_openarm.sh
```

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| Matrix scrolls `BLE?` | R4 BLE firmware missing → IDE → Tools → Firmware Updater. |
| Arduino BLE won't connect after a reset/reflash | Radio wedged — **fully unplug USB‑C** ~10 s, replug (reset button won't do it). |
| `Permission denied` on `/dev/ttyACM*` | Join `dialout` group, log out/in. |
| Controller / GUI dead, or FUSE errors | You're in the VS Code **snap** terminal — use a native one. |
| Browser page never sees the pad | Snap browser — `sudo snap connect firefox:joystick` (gui.sh does this). |
| `arduino-cli`/`ufbt` not found | New shell or `source ~/.bashrc`. |

---

## Reference photos
`*.jpeg`, `img_*.jpg`, `ArduinoCANport.jpeg` — hardware shots (CAN adapter, DAMIAO connectors,
motor, Xbox controller box) referenced during setup.
