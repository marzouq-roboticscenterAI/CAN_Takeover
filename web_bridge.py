#!/usr/bin/env python3
"""
web_bridge.py — serve controller_web.html AND forward its tokens to the Arduino.

  browser (Gamepad API) --POST /send--> this server --serial--> R4 (snake_led)

The browser never touches the serial port (avoids snap/permission issues); this
process does, from a native shell with /dev/ttyACM* access.

  PORT=8000 python3 web_bridge.py [/dev/ttyACM0]
"""
import os
import sys
import glob
import time
import threading
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

try:
    import serial  # pyserial
except ImportError:
    serial = None

DIR = os.path.dirname(os.path.abspath(__file__))
PORT = int(os.environ.get("PORT", "8000"))
BAUD = 115200

ser = None
ser_lock = threading.Lock()


def find_port():
    ports = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))
    return ports[0] if ports else None


class Handler(SimpleHTTPRequestHandler):
    def do_POST(self):
        if self.path.rstrip("/") == "/send":
            n = int(self.headers.get("Content-Length", 0) or 0)
            tok = self.rfile.read(n).decode("ascii", "ignore").strip()
            ok = False
            if tok and ser is not None:
                with ser_lock:
                    try:
                        ser.write((tok + "\n").encode())
                        ok = True
                    except Exception:
                        ok = False
            self.send_response(200 if ok else 503)
            self.send_header("Content-Length", "2")
            self.end_headers()
            self.wfile.write(b"ok" if ok else b"no")
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, *a):
        pass  # keep the console quiet


def main():
    global ser
    port = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] else find_port()
    if serial is None:
        print("!! pyserial not installed — GUI will visualize only (no snake control).")
    elif not port:
        print("!! no /dev/ttyACM* found — GUI will visualize only.")
    else:
        try:
            ser = serial.Serial(port, BAUD, timeout=1)
            time.sleep(2)  # let the R4 settle after the port opens
            print(f"Serial connected: {port} @ {BAUD}")
        except Exception as e:
            print(f"!! could not open {port}: {e} — visualize-only.")

    httpd = ThreadingHTTPServer(("127.0.0.1", PORT), partial(Handler, directory=DIR))
    print(f"Serving {DIR} at http://localhost:{PORT}/controller_web.html  (Ctrl+C to stop)")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nbye")


if __name__ == "__main__":
    main()
