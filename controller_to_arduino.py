#!/usr/bin/env python3
"""
Bridge a Bluetooth game controller -> Arduino UNO R4 WiFi over USB serial.

The controller is read with evdev (Linux). The "last input" is sent as a short
newline-terminated label over the serial port; the Arduino shows it on its 12x8
LED matrix.

Labels:
  Buttons : A B X Y  LB RB  L3 R3  SEL STR HOME  LT RT
  Sticks  : "L^/Lv/L</L>"  (left stick)   "R^/Rv/R</R>" (right stick)
  D-pad   : "D^/Dv/D</D>"
  (trailing ^ v < > are interpreted as arrows by the Arduino)

Usage:
  python3 controller_to_arduino.py [/dev/ttyACM0]

Run it from a NATIVE terminal (not the VS Code snap terminal) so it can read
/dev/input/* and the serial port.
"""
import sys
import glob
import time
import serial
from evdev import InputDevice, list_devices, ecodes

BAUD = 115200
THRESH = 0.6  # stick deflection (0..1) needed to register a direction

# Xbox physical-layout labels (top=Y, left=X, right=B, bottom=A)
BTN_LABELS = {
    ecodes.BTN_SOUTH:  "A",
    ecodes.BTN_EAST:   "B",
    ecodes.BTN_NORTH:  "Y",
    ecodes.BTN_WEST:   "X",
    ecodes.BTN_TL:     "LB",
    ecodes.BTN_TR:     "RB",
    ecodes.BTN_SELECT: "SEL",
    ecodes.BTN_START:  "STR",
    ecodes.BTN_MODE:   "HOME",
    ecodes.BTN_THUMBL: "L3",
    ecodes.BTN_THUMBR: "R3",
}
# Some pads report the d-pad as buttons instead of a hat:
DPAD_BTN = {
    ecodes.BTN_DPAD_UP:    "D^",
    ecodes.BTN_DPAD_DOWN:  "Dv",
    ecodes.BTN_DPAD_LEFT:  "D<",
    ecodes.BTN_DPAD_RIGHT: "D>",
}
ARROW = {"up": "^", "down": "v", "left": "<", "right": ">"}


def find_gamepad():
    for path in list_devices():
        try:
            dev = InputDevice(path)
        except Exception:
            continue
        keys = dev.capabilities().get(ecodes.EV_KEY, [])
        if ecodes.BTN_GAMEPAD in keys or ecodes.BTN_SOUTH in keys:
            return dev
    return None


def find_port():
    ports = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))
    return ports[0] if ports else None


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else find_port()
    if not port:
        sys.exit("No Arduino serial port found (looked for /dev/ttyACM* and /dev/ttyUSB*).")

    dev = find_gamepad()
    if not dev:
        sys.exit("No game controller found. Is it connected? Try `evtest` to check.")

    print(f"Controller: {dev.path}  ({dev.name})")
    print(f"Arduino:    {port} @ {BAUD}")

    ser = serial.Serial(port, BAUD, timeout=1)
    time.sleep(2.0)  # the R4 resets when the port opens; wait for it

    # Cache absinfo (min/max) for the analog axes we care about
    caps_abs = dict(dev.capabilities().get(ecodes.EV_ABS, []))

    def norm(code, value):
        ai = caps_abs.get(code)
        if not ai or ai.max == ai.min:
            return 0.0
        center = (ai.max + ai.min) / 2.0
        half = (ai.max - ai.min) / 2.0
        return (value - center) / half  # -1.0 .. +1.0

    last_sent = [None]

    def send(label):
        last_sent[0] = label
        ser.write((label + "\n").encode())
        print("->", label)

    stick_val = {"L": [0.0, 0.0], "R": [0.0, 0.0]}  # [x, y]
    stick_dir = {"L": None, "R": None}
    hat = [0, 0]

    def stick_update(side):
        x, y = stick_val[side]
        d = None
        if abs(y) >= abs(x) and abs(y) > THRESH:
            d = "up" if y < 0 else "down"   # up is negative on most pads
        elif abs(x) > THRESH:
            d = "left" if x < 0 else "right"
        if d != stick_dir[side]:
            stick_dir[side] = d
            if d is not None:
                send(side + ARROW[d])

    print("Bridge running. Press buttons / move sticks. Ctrl+C to quit.\n")
    for ev in dev.read_loop():
        if ev.type == ecodes.EV_KEY and ev.value == 1:           # key down
            if ev.code in BTN_LABELS:
                send(BTN_LABELS[ev.code])
            elif ev.code in DPAD_BTN:
                send(DPAD_BTN[ev.code])
        elif ev.type == ecodes.EV_ABS:
            c = ev.code
            if c == ecodes.ABS_X:
                stick_val["L"][0] = norm(c, ev.value); stick_update("L")
            elif c == ecodes.ABS_Y:
                stick_val["L"][1] = norm(c, ev.value); stick_update("L")
            elif c == ecodes.ABS_RX:
                stick_val["R"][0] = norm(c, ev.value); stick_update("R")
            elif c == ecodes.ABS_RY:
                stick_val["R"][1] = norm(c, ev.value); stick_update("R")
            elif c == ecodes.ABS_HAT0X:
                if ev.value != hat[0]:
                    hat[0] = ev.value
                    if ev.value < 0: send("D<")
                    elif ev.value > 0: send("D>")
            elif c == ecodes.ABS_HAT0Y:
                if ev.value != hat[1]:
                    hat[1] = ev.value
                    if ev.value < 0: send("D^")
                    elif ev.value > 0: send("Dv")
            elif c == ecodes.ABS_Z:    # left trigger (analog)
                if norm(c, ev.value) > 0.5 and last_sent[0] != "LT": send("LT")
            elif c == ecodes.ABS_RZ:   # right trigger (analog)
                if norm(c, ev.value) > 0.5 and last_sent[0] != "RT": send("RT")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nbye")
