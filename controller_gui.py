#!/usr/bin/env python3
"""
Controller GUI + BLE bridge.

  - Auto-detects a game controller already connected to the laptop (USB or BT).
  - Draws the gamepad and highlights buttons/sticks/triggers in real time.
  - Forwards input to the Arduino R4 (BLE peripheral "SnakeR4") so the snake
    game responds. The R4's USB-C is then power + flashing only.

Input  : evdev (transport-agnostic; works for USB- or Bluetooth-connected pads)
Display: pygame
BLE    : bleak (runs in a background thread; GUI still works if the R4 is absent)

Run from a NATIVE terminal (needs display + /dev/input access).
"""
import asyncio
import threading
import queue
import select

import pygame
from evdev import InputDevice, list_devices, ecodes
from bleak import BleakScanner, BleakClient

DEVICE_NAME = "SnakeR4"
CHR_UUID = "9a1e0002-1b2c-4f3d-8e5a-0123456789ab"
THRESH = 0.6

# evdev code -> label (Xbox physical layout: top=Y, left=X)
BTN_LABELS = {
    ecodes.BTN_SOUTH: "A", ecodes.BTN_EAST: "B", ecodes.BTN_NORTH: "Y", ecodes.BTN_WEST: "X",
    ecodes.BTN_TL: "LB", ecodes.BTN_TR: "RB", ecodes.BTN_SELECT: "Back", ecodes.BTN_START: "Start",
    ecodes.BTN_MODE: "Guide", ecodes.BTN_THUMBL: "L3", ecodes.BTN_THUMBR: "R3",
}
# label -> token sent to the R4 (snake only needs A/STR + arrows)
BTN_TOKEN = {"A": "A", "Start": "STR"}
ARROW = {"up": "^", "down": "v", "left": "<", "right": ">"}

# ---- shared state (main thread writes, BLE thread reads the queue) ----
tokens = queue.Queue()
stop_event = threading.Event()
ble_status = {"text": "searching..."}

# ---- colors ----
BG = (24, 26, 32); BODY = (44, 48, 58); EDGE = (90, 96, 110)
DIM = (60, 64, 74); TXT = (210, 214, 222)
A_C = (60, 200, 90); B_C = (220, 70, 70); X_C = (70, 130, 230); Y_C = (230, 200, 60)
HL = (250, 250, 255); ACC = (250, 190, 60)


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


# ============================== BLE thread ==============================
async def _ble_main():
    ble_status["text"] = "searching..."
    target = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=20.0)
    if not target:
        ble_status["text"] = "R4 not found (GUI only)"
        return
    ble_status["text"] = "connecting..."
    try:
        async with BleakClient(target) as client:
            ble_status["text"] = "connected"
            while not stop_event.is_set():
                try:
                    tok = tokens.get_nowait()
                except queue.Empty:
                    await asyncio.sleep(0.005)
                    continue
                try:
                    await client.write_gatt_char(CHR_UUID, tok.encode("ascii"), response=False)
                except Exception:
                    pass
    except Exception as e:
        ble_status["text"] = f"BLE error: {e}"


def ble_thread():
    try:
        asyncio.run(_ble_main())
    except Exception as e:
        ble_status["text"] = f"BLE thread: {e}"


# ============================== input ==============================
class State:
    def __init__(self):
        self.pressed = {}                 # label -> bool
        self.lstick = [0.0, 0.0]
        self.rstick = [0.0, 0.0]
        self.dpad = [0, 0]
        self.lt = 0.0
        self.rt = 0.0
        self.sdir = {"L": None, "R": None}


def make_norm(absinfo):
    def norm(code, value, centered=True):
        ai = absinfo.get(code)
        if not ai or ai.max == ai.min:
            return 0.0
        if centered:
            c = (ai.max + ai.min) / 2.0
            return max(-1.0, min(1.0, (value - c) / ((ai.max - ai.min) / 2.0)))
        return max(0.0, min(1.0, (value - ai.min) / (ai.max - ai.min)))
    return norm


def stick_dir_update(st, side):
    x, y = st.lstick if side == "L" else st.rstick
    d = None
    if abs(y) >= abs(x) and abs(y) > THRESH:
        d = "up" if y < 0 else "down"
    elif abs(x) > THRESH:
        d = "left" if x < 0 else "right"
    if d != st.sdir[side]:
        st.sdir[side] = d
        if d:
            tokens.put(side + ARROW[d])


def process_event(st, norm, ev):
    if ev.type == ecodes.EV_KEY:
        if ev.code in BTN_LABELS:
            label = BTN_LABELS[ev.code]
            st.pressed[label] = bool(ev.value)
            if ev.value == 1 and label in BTN_TOKEN:
                tokens.put(BTN_TOKEN[label])
    elif ev.type == ecodes.EV_ABS:
        c = ev.code
        if c == ecodes.ABS_X:   st.lstick[0] = norm(c, ev.value); stick_dir_update(st, "L")
        elif c == ecodes.ABS_Y: st.lstick[1] = norm(c, ev.value); stick_dir_update(st, "L")
        elif c == ecodes.ABS_RX: st.rstick[0] = norm(c, ev.value); stick_dir_update(st, "R")
        elif c == ecodes.ABS_RY: st.rstick[1] = norm(c, ev.value); stick_dir_update(st, "R")
        elif c == ecodes.ABS_Z:  st.lt = norm(c, ev.value, centered=False)
        elif c == ecodes.ABS_RZ: st.rt = norm(c, ev.value, centered=False)
        elif c == ecodes.ABS_HAT0X:
            st.dpad[0] = ev.value
            if ev.value < 0: tokens.put("D<")
            elif ev.value > 0: tokens.put("D>")
        elif c == ecodes.ABS_HAT0Y:
            st.dpad[1] = ev.value
            if ev.value < 0: tokens.put("D^")
            elif ev.value > 0: tokens.put("Dv")


# ============================== drawing ==============================
def draw_btn(scr, font, center, r, color, pressed, label):
    if pressed:
        pygame.draw.circle(scr, color, center, r)
        pygame.draw.circle(scr, HL, center, r, 2)
    else:
        pygame.draw.circle(scr, DIM, center, r)
        pygame.draw.circle(scr, color, center, r, 2)
    t = font.render(label, True, HL if pressed else TXT)
    scr.blit(t, t.get_rect(center=center))


def draw_pill(scr, font, rect, pressed, label):
    color = ACC if pressed else DIM
    pygame.draw.rect(scr, color, rect, border_radius=8)
    pygame.draw.rect(scr, EDGE, rect, 2, border_radius=8)
    t = font.render(label, True, (20, 20, 20) if pressed else TXT)
    scr.blit(t, t.get_rect(center=rect.center))


def draw_stick(scr, center, vec, pressed):
    outer = 46
    pygame.draw.circle(scr, DIM, center, outer)
    pygame.draw.circle(scr, EDGE, center, outer, 2)
    off = outer - 18
    knob = (int(center[0] + vec[0] * off), int(center[1] + vec[1] * off))
    pygame.draw.circle(scr, ACC if pressed else (150, 156, 168), knob, 16)
    pygame.draw.circle(scr, HL, knob, 16, 2)


def draw_dpad(scr, center, dpad):
    cx, cy = center
    arm, w = 26, 18
    rects = {
        "up":    pygame.Rect(cx - w // 2, cy - arm, w, arm),
        "down":  pygame.Rect(cx - w // 2, cy, w, arm),
        "left":  pygame.Rect(cx - arm, cy - w // 2, arm, w),
        "right": pygame.Rect(cx, cy - w // 2, arm, w),
    }
    act = {"up": dpad[1] < 0, "down": dpad[1] > 0, "left": dpad[0] < 0, "right": dpad[0] > 0}
    for k, rc in rects.items():
        pygame.draw.rect(scr, ACC if act[k] else DIM, rc)
        pygame.draw.rect(scr, EDGE, rc, 1)


def draw_trigger(scr, font, x, y, value, label):
    w, h = 70, 16
    pygame.draw.rect(scr, DIM, (x, y, w, h), border_radius=6)
    pygame.draw.rect(scr, ACC, (x, y, int(w * value), h), border_radius=6)
    pygame.draw.rect(scr, EDGE, (x, y, w, h), 2, border_radius=6)
    scr.blit(font.render(label, True, TXT), (x + w + 8, y - 1))


def draw(scr, font, big, st, dev_name):
    scr.fill(BG)
    pygame.draw.rect(scr, BODY, (60, 90, 640, 300), border_radius=40)
    pygame.draw.rect(scr, EDGE, (60, 90, 640, 300), 2, border_radius=40)

    # triggers + bumpers
    draw_trigger(scr, font, 120, 70, st.lt, "LT")
    draw_trigger(scr, font, 510, 70, st.rt, "RT")
    draw_pill(scr, font, pygame.Rect(120, 100, 80, 22), st.pressed.get("LB"), "LB")
    draw_pill(scr, font, pygame.Rect(560, 100, 80, 22), st.pressed.get("RB"), "RB")

    # sticks
    draw_stick(scr, (170, 180), st.lstick, st.pressed.get("L3"))
    draw_stick(scr, (470, 300), st.rstick, st.pressed.get("R3"))

    # d-pad
    draw_dpad(scr, (290, 300))

    # face buttons (diamond)
    cx, cy, gap = 560, 230, 38
    draw_btn(scr, font, (cx, cy + gap), 20, A_C, st.pressed.get("A"), "A")
    draw_btn(scr, font, (cx + gap, cy), 20, B_C, st.pressed.get("B"), "B")
    draw_btn(scr, font, (cx - gap, cy), 20, X_C, st.pressed.get("X"), "X")
    draw_btn(scr, font, (cx, cy - gap), 20, Y_C, st.pressed.get("Y"), "Y")

    # center buttons
    draw_btn(scr, font, (340, 175), 14, (150, 156, 168), st.pressed.get("Back"), "")
    draw_btn(scr, font, (420, 175), 14, (150, 156, 168), st.pressed.get("Start"), "")
    draw_btn(scr, font, (380, 150), 16, (150, 156, 168), st.pressed.get("Guide"), "")

    scr.blit(big.render("Controller", True, TXT), (60, 20))
    scr.blit(font.render(dev_name, True, (150, 156, 168)), (62, 410))
    scr.blit(font.render(f"R4: {ble_status['text']}", True, (150, 156, 168)), (520, 410))


# ============================== main ==============================
def main():
    dev = find_gamepad()
    if not dev:
        raise SystemExit("No controller found. Connect the 8BitDo (USB or Bluetooth) and retry.")
    absinfo = dict(dev.capabilities().get(ecodes.EV_ABS, []))
    norm = make_norm(absinfo)
    st = State()

    threading.Thread(target=ble_thread, daemon=True).start()

    pygame.init()
    screen = pygame.display.set_mode((760, 440))
    pygame.display.set_caption("Controller — live")
    font = pygame.font.SysFont("dejavusans", 16, bold=True)
    big = pygame.font.SysFont("dejavusans", 22, bold=True)
    clock = pygame.time.Clock()

    running = True
    while running:
        for e in pygame.event.get():
            if e.type == pygame.QUIT:
                running = False

        try:
            r, _, _ = select.select([dev.fd], [], [], 0)
            if r:
                for ev in dev.read():
                    process_event(st, norm, ev)
        except (BlockingIOError, OSError):
            nd = find_gamepad()                  # controller dropped? try to re-grab
            if nd:
                dev = nd
                absinfo = dict(dev.capabilities().get(ecodes.EV_ABS, []))
                norm = make_norm(absinfo)

        draw(screen, font, big, st, dev.name)
        pygame.display.flip()
        clock.tick(60)

    stop_event.set()
    pygame.quit()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        stop_event.set()
