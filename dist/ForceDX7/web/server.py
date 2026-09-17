#!/usr/bin/env python3
"""Force DX7 web control panel — serves dx7_ui.html (an original page, not
reused from any upstream Move module since schwung-dx7 has no web UI of its
own) and bridges its actions to dx7_host's Unix control socket
(SET/GET/DESCRIBE/NOTE — see src/dx7_host.cpp's header comment for the
protocol), plus start/stop of the engine process itself (same pattern as
force-acid/force-jv880's own web panels).

Deliberately stdlib-only (http.server + socket): no pip install step needed
on-device, matching this project's other web panels.

Run: python3 server.py [--port N] [--ctrl-sock PATH]
"""
import json
import socket
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs

WEB_DIR = Path(__file__).resolve().parent
ADDON_DIR = WEB_DIR.parent          # .../AddOns/ForceDX7 on a deployed device
ENGINE_BIN = ADDON_DIR / "dx7_host"
CTRL_SOCK = "/tmp/dx7_ctrl.sock"
SOCK_TIMEOUT = 1.0
CONTROL_CHANNEL = "1"               # must match the .xtk template's track output channel
MIX_SLOT = "2"                      # 0 = force-maze, 1 = force-jv880 (see dx7_host.cpp)


def ctrl_request(line: str):
    """Send one line to dx7_host's control socket, return its reply (or
    None if the engine isn't reachable). Reads until EOF, not a single
    bounded recv() -- see force-jv880/DESIGN.md's own writeup of the
    8192-byte truncation bug this avoids from the start here."""
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
            s.settimeout(SOCK_TIMEOUT)
            s.connect(CTRL_SOCK)
            s.sendall((line.strip("\n") + "\n").encode("utf-8"))
            chunks = []
            while True:
                chunk = s.recv(65536)
                if not chunk:
                    break
                chunks.append(chunk)
            return b"".join(chunks).decode("utf-8", errors="replace").strip("\n")
    except OSError:
        return None


def engine_present():
    return ctrl_request("DESCRIBE") is not None


def engine_start():
    if engine_present():
        return True, "already running"
    if not ENGINE_BIN.exists():
        return False, f"binary not found: {ENGINE_BIN}"
    args = [str(ENGINE_BIN), "--module-dir", str(ADDON_DIR),
            "--ctrl-sock", CTRL_SOCK, "--control-channel", CONTROL_CHANNEL,
            "--mix-slot", MIX_SLOT]
    try:
        log = open("/tmp/dx7_host.log", "ab")
        proc = subprocess.Popen(args, stdout=log, stderr=log,
                                 stdin=subprocess.DEVNULL, start_new_session=True)
    except Exception as e:
        return False, str(e)
    threading.Thread(target=proc.wait, daemon=True).start()
    for _ in range(30):  # dx7_host's create_instance is synchronous (no ROM-load
        time.sleep(0.2)  # thread like jv_host's) so this should land fast; still
        if engine_present():  # generous in case of a slow bank scan.
            return True, "started"
    return False, "launched but control socket did not come up in time"


def engine_stop():
    subprocess.run(["killall", "dx7_host"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return True, "stopped"


class Handler(BaseHTTPRequestHandler):
    server_version = "ForceDX7Web/0.1"

    def log_message(self, fmt, *args):
        sys.stderr.write("[dx7-web] " + (fmt % args) + "\n")

    def _text(self, code, body, ctype="text/plain; charset=utf-8"):
        data = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def _json(self, code, obj):
        self._text(code, json.dumps(obj), "application/json; charset=utf-8")

    def _file(self, relpath, ctype):
        path = WEB_DIR / relpath
        try:
            data = path.read_bytes()
        except OSError:
            self.send_error(404, "not found")
            return
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        path = urlparse(self.path).path
        if path in ("/", "/index.html"):
            self._file("dx7_ui.html", "text/html; charset=utf-8")
            return

        if path == "/algorithms.js":
            self._file("algorithms.js", "application/javascript; charset=utf-8")
            return

        if path == "/param":
            qs = parse_qs(urlparse(self.path).query)
            key = (qs.get("key") or [""])[0]
            if not key:
                self._text(400, "missing key")
                return
            reply = ctrl_request(f"GET {key}")
            if reply is None:
                self._text(503, "engine not running")
            elif reply == "ERR":
                self._text(404, "unknown param")
            else:
                self._text(200, reply)
            return

        if path == "/describe":
            reply = ctrl_request("DESCRIBE")
            if reply is None:
                self._json(503, [])
            else:
                self._text(200, reply, "application/json; charset=utf-8")
            return

        if path == "/status":
            self._json(200, {"engine_running": engine_present()})
            return

        self.send_error(404, "not found")

    def do_POST(self):
        path = urlparse(self.path).path
        length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(length) if length else b"{}"
        try:
            body = json.loads(raw or b"{}")
        except json.JSONDecodeError:
            self._json(400, {"ok": False, "error": "bad json"})
            return

        if path == "/param":
            key, value = body.get("key"), body.get("value")
            if not key or value is None:
                self._json(400, {"ok": False, "error": "missing key/value"})
                return
            reply = ctrl_request(f"SET {key} {value}")
            self._json(200 if reply == "OK" else 503, {"ok": reply == "OK"})
            return

        if path == "/note":
            note = int(body.get("note", 60))
            vel = int(body.get("velocity", 100))
            reply = ctrl_request(f"NOTE {note} {vel}")
            self._json(200 if reply == "OK" else 503, {"ok": reply == "OK"})
            return

        if path == "/engine":
            action = body.get("action")
            if action == "start":
                ok, msg = engine_start()
            elif action == "stop":
                ok, msg = engine_stop()
            else:
                self._json(400, {"ok": False, "error": "action must be start|stop"})
                return
            self._json(200 if ok else 503, {"ok": ok, "message": msg})
            return

        self.send_error(404, "not found")


def main():
    global CTRL_SOCK
    port = 8307  # next free slot: 8303 acid, 8304 maze, 8305 maze-seq, 8306 jv880
    args = sys.argv[1:]
    if "--port" in args:
        port = int(args[args.index("--port") + 1])
    if "--ctrl-sock" in args:
        CTRL_SOCK = args[args.index("--ctrl-sock") + 1]

    srv = ThreadingHTTPServer(("0.0.0.0", port), Handler)
    print(f"[dx7-web] serving on http://0.0.0.0:{port}  (control socket: {CTRL_SOCK})")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
