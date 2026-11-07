#!/usr/bin/env python3
"""Fog client presence server.

Fog clients tell this server which DDNet server they play on and how their name should look;
in return they get the other Fog players on the same DDNet server. Nothing is stored on disk:
entries live in memory and expire when a client stops sending heartbeats.

Run:  python3 server.py --host 0.0.0.0 --port 8787
Then set fog_presence_url "http://<host>:8787" in every Fog client (Settings -> Fog -> Players).

Protocol (JSON over HTTP):
  POST /v1/heartbeat  {"id": "<install id>", "name": "nick", "server": "ip:port",
                       "style": {"mode": 0-5, "c1": "rrggbb", "c2": "rrggbb", "speed": 25-400, "badge": true,
                                 "tee": "1|...|...|..."  (optional tee studio look)}}
  -> 200 {"players": [{"name": "nick", "style": {...}}, ...]}   (same server, without yourself)
  GET  /v1/health      -> 200 {"ok": true, "players": <count>}
"""

import argparse
import json
import re
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

TTL = 75.0  # seconds without a heartbeat until a player disappears
MAX_ENTRIES = 50000
MAX_BODY = 4096
MIN_INTERVAL = 4.0  # per install id
HEX = re.compile(r"^[0-9a-fA-F]{6}$")
ID = re.compile(r"^[0-9a-zA-Z]{8,64}$")
SERVER = re.compile(r"^[0-9a-zA-Z\[\]:.\-]{3,64}$")
TEE = re.compile(r"^[0-9a-zA-Z_,;|.\-]{0,1024}$")  # tee studio look: numbers and accessory ids

lock = threading.Lock()
entries = {}  # id -> {"name", "server", "style", "seen"}


def clean_style(style):
    if not isinstance(style, dict):
        style = {}
    def color(key, default):
        value = style.get(key, default)
        return value if isinstance(value, str) and HEX.match(value) else default
    mode = style.get("mode", 0)
    speed = style.get("speed", 100)
    return {
        "mode": mode if isinstance(mode, int) and 0 <= mode <= 5 else 0,
        "c1": color("c1", "c8d4e0"),
        "c2": color("c2", "9fb0c8"),
        "speed": speed if isinstance(speed, int) and 25 <= speed <= 400 else 100,
        "badge": bool(style.get("badge", True)),
        **({"tee": style["tee"]} if isinstance(style.get("tee"), str) and TEE.match(style["tee"]) else {}),
    }


def expire(now):
    for key in [k for k, v in entries.items() if now - v["seen"] > TTL]:
        del entries[key]


class Handler(BaseHTTPRequestHandler):
    server_version = "fog-presence/1"

    verbose = False

    def log_message(self, fmt, *args):  # quiet unless --verbose
        if Handler.verbose:
            print("%s %s" % (self.address_string(), fmt % args), flush=True)

    def reply(self, code, payload):
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/v1/health":
            with lock:
                expire(time.monotonic())
                count = len(entries)
            self.reply(200, {"ok": True, "players": count})
        else:
            self.reply(404, {"error": "not found"})

    def do_POST(self):
        if self.path != "/v1/heartbeat":
            self.reply(404, {"error": "not found"})
            return
        length = int(self.headers.get("Content-Length") or 0)
        if length <= 0 or length > MAX_BODY:
            self.reply(413, {"error": "body too large"})
            return
        try:
            data = json.loads(self.rfile.read(length).decode("utf-8"))
        except (ValueError, UnicodeDecodeError):
            self.reply(400, {"error": "bad json"})
            return
        install = data.get("id")
        name = data.get("name")
        server = data.get("server")
        if not (isinstance(install, str) and ID.match(install) and isinstance(name, str) and 0 < len(name) <= 32 and isinstance(server, str) and SERVER.match(server)):
            if Handler.verbose:
                print("rejected:", {"id": install, "name": name, "server": server}, flush=True)
            self.reply(400, {"error": "bad fields"})
            return
        now = time.monotonic()
        with lock:
            expire(now)
            previous = entries.get(install)
            if previous and now - previous["seen"] < MIN_INTERVAL and previous["server"] == server:
                too_fast = True
            else:
                too_fast = False
                if previous is None and len(entries) >= MAX_ENTRIES:
                    self.reply(503, {"error": "full"})
                    return
                entries[install] = {"name": name, "server": server, "style": clean_style(data.get("style")), "seen": now}
            players = [{"name": v["name"], "style": v["style"]} for k, v in entries.items() if v["server"] == server and k != install]
        self.reply(429 if too_fast else 200, {"players": players})


def main():
    parser = argparse.ArgumentParser(description="Fog client presence server")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8787)
    parser.add_argument("--verbose", action="store_true", help="log every request")
    args = parser.parse_args()
    Handler.verbose = args.verbose
    httpd = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"fog presence server on http://{args.host}:{args.port}")
    httpd.serve_forever()


if __name__ == "__main__":
    main()
