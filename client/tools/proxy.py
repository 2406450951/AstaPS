import os
"""Logging reverse proxy for dispatch debugging.

Listens on 127.0.0.1:LISTEN and forwards every request to 127.0.0.1:UPSTREAM,
dumping method / path / status / body prefix to a log file and stdout.
"""
import http.client
import socketserver
import sys
import time
from http.server import BaseHTTPRequestHandler

LISTEN = int(sys.argv[1]) if len(sys.argv) > 1 else 8089
UPSTREAM = int(sys.argv[2]) if len(sys.argv) > 2 else 8088
LOGFILE = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "proxy.log")


def log(msg):
    line = f"[{time.strftime('%H:%M:%S')}] {msg}"
    with open(LOGFILE, "a", encoding="utf-8") as f:
        f.write(line + "\n")
    print(line, flush=True)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def _proxy(self, method):
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length) if length else b""
        log(f"--> {method} {self.path}  (body {len(body)}B) {body[:300]!r}")
        try:
            conn = http.client.HTTPConnection("127.0.0.1", UPSTREAM, timeout=20)
            headers = {k: v for k, v in self.headers.items()
                       if k.lower() not in ("host", "connection", "content-length")}
            conn.request(method, self.path, body=body, headers=headers)
            resp = conn.getresponse()
            data = resp.read()
            conn.close()
        except Exception as exc:  # noqa: BLE001
            log(f"!!! upstream error {exc}")
            self.send_response(502)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return

        log(f"<-- {resp.status} {self.path}  ({len(data)}B) {data[:400]!r}")
        self.send_response(resp.status)
        for k, v in resp.getheaders():
            if k.lower() in ("content-length", "transfer-encoding", "connection"):
                continue
            self.send_header(k, v)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        self._proxy("GET")

    def do_POST(self):
        self._proxy("POST")

    def do_PUT(self):
        self._proxy("PUT")

    def log_message(self, *args):
        pass


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    open(LOGFILE, "w", encoding="utf-8").close()
    log(f"proxy {LISTEN} -> {UPSTREAM}")
    with Server(("127.0.0.1", LISTEN), Handler) as srv:
        srv.serve_forever()
