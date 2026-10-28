"""Local TLS front-end for the mihoyo SDK account endpoints.

The SDK talks HTTPS to passport-api.mihoyo.com through its own network stack, so the WinHTTP hooks
never see it. This script terminates TLS locally with a certificate signed by a CA installed in the
machine root store, and forwards each request as plain HTTP to the private server on 8088.

    python mitm_sdk.py
"""
import http.server
import json
import os
import ssl
import sys
import urllib.error
import urllib.request

LISTEN_PORT = 443
UPSTREAM = "http://127.0.0.1:8088"
CERT_DIR = os.path.dirname(os.path.abspath(__file__))


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        sys.stderr.write("[mitm] %s\n" % (fmt % args))
        sys.stderr.flush()

    def _forward(self, method):
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length) if length else None
        if body:
            try:
                preview = body[:400].decode("utf-8", "replace")
            except Exception:  # noqa: BLE001
                preview = repr(body[:80])
            sys.stderr.write("[mitm] BODY %s %s\n" % (self.path, preview))
            sys.stderr.flush()
        url = UPSTREAM + self.path
        req = urllib.request.Request(url, data=body, method=method)
        for k, v in self.headers.items():
            if k.lower() in ("host", "content-length", "connection", "accept-encoding"):
                continue
            req.add_header(k, v)
        try:
            with urllib.request.urlopen(req, timeout=15) as resp:
                data = resp.read()
                self.send_response(resp.status)
                for k, v in resp.headers.items():
                    if k.lower() in ("transfer-encoding", "connection", "content-length"):
                        continue
                    self.send_header(k, v)
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)
                sys.stderr.write("[mitm] %s %s -> %d (%d bytes)\n" % (method, self.path, resp.status, len(data)))
                if "verify" in self.path or "login" in self.path or "exchange" in self.path:
                    try:
                        sys.stderr.write("[mitm] RESP %s %s\n" % (self.path, data[:600].decode("utf-8", "replace")))
                    except Exception:  # noqa: BLE001
                        pass
                sys.stderr.flush()
        except urllib.error.HTTPError as e:
            data = e.read()
            self.send_response(e.code)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        except Exception as e:  # noqa: BLE001
            sys.stderr.write("[mitm] upstream error for %s: %r\n" % (self.path, e))
            sys.stderr.flush()
            payload = json.dumps({"retcode": -1, "message": "mitm upstream error"}).encode()
            self.send_response(502)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

    def do_GET(self):
        self._forward("GET")

    def do_POST(self):
        self._forward("POST")

    def do_PUT(self):
        self._forward("PUT")


def main():
    cert = os.path.join(CERT_DIR, "sdk_cert.pem")
    key = os.path.join(CERT_DIR, "sdk_key.pem")
    if not (os.path.isfile(cert) and os.path.isfile(key)):
        print("missing sdk_cert.pem / sdk_key.pem")
        return 1

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cert, key)
    ctx.set_alpn_protocols(["http/1.1"])

    srv = http.server.ThreadingHTTPServer(("0.0.0.0", LISTEN_PORT), Handler)
    srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
    print("listening on 0.0.0.0:%d -> %s" % (LISTEN_PORT, UPSTREAM))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
