#!/usr/bin/env python3
"""
web_bridge_ble.py — serve controller_web.html AND forward its tokens to the R4
over BLE (so the USB-C cable is power-only).

  browser (Gamepad API) --POST /send--> this server --BLE--> R4 "SnakeR4" (snake_ble)

Needs: bleak.  The R4 must be running snake_ble (BLE peripheral) with BLE
firmware enabled (Arduino IDE -> Tools -> Firmware Updater).

  PORT=8000 python3 web_bridge_ble.py
"""
import os
import json
import asyncio
import threading
import queue
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

from bleak import BleakScanner, BleakClient

DIR = os.path.dirname(os.path.abspath(__file__))
PORT = int(os.environ.get("PORT", "8000"))
DEVICE_NAME = "SnakeR4"
CHR_UUID = "9a1e0002-1b2c-4f3d-8e5a-0123456789ab"     # laptop -> R4
STATUS_UUID = "9a1e0003-1b2c-4f3d-8e5a-0123456789ab"  # R4 -> laptop

tokens = queue.Queue()
stop = threading.Event()
status = {"text": "starting"}

latest_status = {}
status_lock = threading.Lock()


def decode_status(data):
    if len(data) < 16:
        return None
    snake = []
    for idx in range(96):                    # 8 rows x 12 cols
        if data[4 + (idx >> 3)] & (1 << (idx & 7)):
            snake.append([idx % 12, idx // 12])   # [col, row]
    return {"state": data[0], "score": data[1],
            "food": [data[2], data[3]], "snake": snake}


def on_status(_sender, data):
    s = decode_status(bytes(data))
    if s is not None:
        with status_lock:
            latest_status.clear()
            latest_status.update(s)


class Handler(SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path.rstrip("/") == "/status":
            with status_lock:
                body = json.dumps(latest_status or {}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            super().do_GET()

    def do_POST(self):
        if self.path.rstrip("/") == "/send":
            n = int(self.headers.get("Content-Length", 0) or 0)
            tok = self.rfile.read(n).decode("ascii", "ignore").strip()
            if tok:
                tokens.put(tok)
            self.send_response(200)
            self.send_header("Content-Length", "2")
            self.end_headers()
            self.wfile.write(b"ok")
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, *a):
        pass


async def ble_main():
    status["text"] = "scanning for SnakeR4"
    print("Scanning for BLE 'SnakeR4' ...")
    dev = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=20.0)
    if not dev:
        status["text"] = "SnakeR4 not found"
        print("!! SnakeR4 not found — is the R4 powered and running snake_ble (BLE firmware)?")
        return
    async with BleakClient(dev) as client:
        status["text"] = "connected"
        print(f"BLE connected to {dev.address}")
        try:
            await client.start_notify(STATUS_UUID, on_status)   # R4 -> laptop status
            print("Subscribed to game status.")
        except Exception as e:
            print("!! could not subscribe to status:", e)
        while not stop.is_set():
            try:
                tok = tokens.get_nowait()
            except queue.Empty:
                await asyncio.sleep(0.005)
                continue
            try:
                await client.write_gatt_char(CHR_UUID, tok.encode("ascii"), response=False)
            except Exception as e:
                print("BLE write error:", e)


def ble_thread():
    try:
        asyncio.run(ble_main())
    except Exception as e:
        print("BLE thread error:", e)


def main():
    threading.Thread(target=ble_thread, daemon=True).start()
    httpd = ThreadingHTTPServer(("127.0.0.1", PORT), partial(Handler, directory=DIR))
    print(f"Serving {DIR} at http://localhost:{PORT}/controller_web.html  (Ctrl+C to stop)")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        stop.set()
        print("\nbye")


if __name__ == "__main__":
    main()
