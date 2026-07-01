#!/usr/bin/env python3
"""
Laptop-side BLE bridge for the Snake game (laptop = BLE CENTRAL).

  wired 8BitDo (evdev) -> this script -> BLE write -> Arduino R4 "SnakeR4"
                                                       (snake_ble.ino, peripheral)

The R4 advertises as "SnakeR4" with a writable characteristic; this script
connects to it and writes short ASCII tokens as you use the controller:
    ^ v < >   directions (d-pad or either stick)
    A / STR   start / restart

The R4's USB-C is then power + flashing only.

Setup (Ubuntu):
    sudo apt install bluez
    python3 -m venv ~/snakepad-venv
    ~/snakepad-venv/bin/pip install bleak evdev
    ~/snakepad-venv/bin/python controller_to_ble.py
Run from a NATIVE terminal (not the VS Code snap terminal).
"""
import asyncio

from evdev import InputDevice, list_devices, ecodes
from bleak import BleakScanner, BleakClient

DEVICE_NAME = "SnakeR4"
CHR_UUID = "9a1e0002-1b2c-4f3d-8e5a-0123456789ab"   # must match snake_ble.ino
THRESH = 0.6

BTN_LABELS = {
    ecodes.BTN_SOUTH: "A", ecodes.BTN_EAST: "B", ecodes.BTN_NORTH: "Y", ecodes.BTN_WEST: "X",
    ecodes.BTN_TL: "LB", ecodes.BTN_TR: "RB", ecodes.BTN_SELECT: "SEL", ecodes.BTN_START: "STR",
    ecodes.BTN_MODE: "HOME", ecodes.BTN_THUMBL: "L3", ecodes.BTN_THUMBR: "R3",
}
DPAD_BTN = {
    ecodes.BTN_DPAD_UP: "D^", ecodes.BTN_DPAD_DOWN: "Dv",
    ecodes.BTN_DPAD_LEFT: "D<", ecodes.BTN_DPAD_RIGHT: "D>",
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


async def pump(dev, client):
    """Read controller events and write tokens to the R4."""
    caps_abs = dict(dev.capabilities().get(ecodes.EV_ABS, []))

    def norm(code, value):
        ai = caps_abs.get(code)
        if not ai or ai.max == ai.min:
            return 0.0
        center = (ai.max + ai.min) / 2.0
        return (value - center) / ((ai.max - ai.min) / 2.0)

    stick = {"L": [0.0, 0.0], "R": [0.0, 0.0]}
    sdir = {"L": None, "R": None}
    hat = [0, 0]

    async def send(tok):
        try:
            await client.write_gatt_char(CHR_UUID, tok.encode("ascii"), response=False)
            print("->", tok)
        except Exception as e:
            print("write failed:", e)

    async def stick_update(side):
        x, y = stick[side]
        d = None
        if abs(y) >= abs(x) and abs(y) > THRESH:
            d = "up" if y < 0 else "down"
        elif abs(x) > THRESH:
            d = "left" if x < 0 else "right"
        if d != sdir[side]:
            sdir[side] = d
            if d:
                await send(side + ARROW[d])

    async for ev in dev.async_read_loop():
        if ev.type == ecodes.EV_KEY and ev.value == 1:
            if ev.code in BTN_LABELS:
                await send(BTN_LABELS[ev.code])
            elif ev.code in DPAD_BTN:
                await send(DPAD_BTN[ev.code])
        elif ev.type == ecodes.EV_ABS:
            c = ev.code
            if c == ecodes.ABS_X:    stick["L"][0] = norm(c, ev.value); await stick_update("L")
            elif c == ecodes.ABS_Y:  stick["L"][1] = norm(c, ev.value); await stick_update("L")
            elif c == ecodes.ABS_RX: stick["R"][0] = norm(c, ev.value); await stick_update("R")
            elif c == ecodes.ABS_RY: stick["R"][1] = norm(c, ev.value); await stick_update("R")
            elif c == ecodes.ABS_HAT0X:
                if ev.value != hat[0]:
                    hat[0] = ev.value
                    if ev.value < 0: await send("D<")
                    elif ev.value > 0: await send("D>")
            elif c == ecodes.ABS_HAT0Y:
                if ev.value != hat[1]:
                    hat[1] = ev.value
                    if ev.value < 0: await send("D^")
                    elif ev.value > 0: await send("Dv")


async def main():
    dev = find_gamepad()
    if not dev:
        raise SystemExit("No game controller found. Plug in the 8BitDo (XInput) and retry.")
    print(f"Controller: {dev.path}  ({dev.name})")

    print(f"Scanning for BLE peripheral '{DEVICE_NAME}' ...")
    target = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=20.0)
    if not target:
        raise SystemExit(f"'{DEVICE_NAME}' not found. Is the R4 powered and running snake_ble?")

    print(f"Connecting to {target.address} ...")
    async with BleakClient(target) as client:
        print("Connected. Use the controller (Ctrl+C to quit).")
        await pump(dev, client)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nbye")
