#!/usr/bin/env python3
"""BMS reference server — Brad Mobile Services REST API (v1).

A loopback-only reference implementation of the BMS developer API
(src/bms/api_v1.md).  Python stdlib (http.server + json), no
third-party dependencies, no production claims: one BradID, in-memory
state, decorative payloads.  It exists so the API *surface* is real and
runnable the day the sheet-metal is ordered, not to pretend BMS is
deployed.

Usage:  python3 src/bms/reference_server.py   (port via BMS_PORT)
"""

import base64
import hashlib
import json
import os
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

VERSION = "v1"
REFERENCE_USER = "demo@brad.dev"
DEMO_TOKEN = "bms-demo-token"
RATE_LIMIT_PER_MIN = {"auth": 1000, "default": 100}
TRANSFER_FEE_CENTS = 30  # BMS_SPEC 4.3: 1.5% + $0.30 on merchant txns


class State:
    def __init__(self):
        self.wallet_cents = 150000
        self.transactions = []
        self.files = {}
        self.notifications = []
        self.message_seq = 1000
        self.txn_seq = 0

STATE = State()


def now_iso():
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def json_body(body):
    try:
        return json.loads(body.decode("utf-8"))
    except (ValueError, UnicodeDecodeError):
        return None


class BMSHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):  # quiet
        pass

    # ── helpers ────────────────────────────────────────────────

    def _send(self, status, payload):
        data = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("X-BMS-Reference", "loopback reference server")
        self.end_headers()
        self.wfile.write(data)

    def _err(self, code, message, status=400, details=None):
        self._send(status, {"error": {"code": code, "message": message, "details": details}})

    def _authed(self):
        auth = self.headers.get("Authorization", "")
        if auth != f"Bearer {DEMO_TOKEN}":
            self._err("AUTH_REQUIRED", "a Bearer token is required", 401)
            return False
        return True

    # ── routes ─────────────────────────────────────────────────

    def do_GET(self):
        path = self.path.split("?")[0].rstrip("/") or "/"

        if path == "/healthz":
            self._send(200, {"status": "healthy", "services": {
                "auth": "up", "mail": "up", "hub": "up", "pay": "up",
                "cloud": "up", "geo": "up", "notify": "up",
            }})
            return

        if path == "/v1" or path == "":
            self._send(200, {
                "service": "BMS reference API",
                "version": VERSION,
                "base": "http://127.0.0.1:8085/v1",
                "endpoints": [
                    "POST /auth/token", "GET /auth/me",
                    "GET /mail/messages[(/{id})?]",
                    "PATCH /mail/messages/{id}",
                    "GET /hub/apps",
                    "GET /pay/wallet",
                    "GET/POST /cloud/files[(/{id})?]",
                    "GET /geo/tile/{z}/{x}/{y}",
                    "POST /notify/send",
                ],
                "disclaimer": "reference server — one BradID, in-memory state",
            })
            return

        if not path.startswith("/v1/"):
            self._err("NOT_FOUND", "unknown service", 404)
            return

        seg = path[len("/v1/"):].split("/")

        if seg[0] == "auth" and seg[1:] == ["me"]:
            if not self._authed():
                return
            self._send(200, {
                "brad_id": REFERENCE_USER,
                "display_name": "Brad (reference demo)",
                "mailbox": "demo@bradmail.com",
                "cloud_quota_bytes": 5 * 1024 ** 3,
                "regions": ["ke-1"],
            })
            return

        if seg[0] == "mail" and seg[1:] == ["messages"]:
            if not self._authed():
                return
            self._send(200, {"messages": [
                {"id": "msg-0001", "from": "system@brad.dev",
                 "to": ["demo@bradmail.com"], "subject": "Your inbox is live",
                 "preview": "This reference mailbox is ready. PATCH it read.",
                 "unread": True, "received": now_iso()},
            ], "total": 1})
            return

        if seg[0] == "mail" and len(seg) == 3 and seg[1] == "messages":
            if not self._authed():
                return
            self._send(200, {
                "id": seg[2], "from": "system@brad.dev",
                "to": ["demo@bradmail.com"], "subject": "Your inbox is live",
                "body": "Reference body — BMS_SPEC.md 4.1, rendered here.",
                "unread": False, "received": now_iso(),
            })
            return

        if seg[0] == "hub" and seg[1:] == ["apps"]:
            self._send(200, {"apps": [
                {"id": "app-bradmail", "name": "BradMail",
                 "publisher": "Brad Devices", "size_mb": 214,
                 "tier": "free", "rating": 4.9},
                {"id": "app-brados", "name": "BradOS Utilities",
                 "publisher": "Brad Devices", "size_mb": 88,
                 "tier": "free", "rating": 4.7},
            ]})
            return

        if seg[0] == "pay" and seg[1:] == ["wallet"]:
            if not self._authed():
                return
            self._send(200, {
                "brad_id": REFERENCE_USER,
                "balance_cents": STATE.wallet_cents,
                "currency": "KSh",
                "transactions": STATE.transactions[-5:],
            })
            return

        if seg[0] == "cloud" and seg[1:] == ["files"]:
            if not self._authed():
                return
            self._send(200, {"files": STATE.files, "quota_bytes": 5 * 1024 ** 3})
            return

        if seg[0] == "cloud" and len(seg) == 3 and seg[1] == "files":
            if not self._authed():
                return
            f = STATE.files.get(seg[2])
            if not f:
                self._err("NOT_FOUND", "no such object", 404)
                return
            self._send(200, f)
            return

        if seg[0] == "geo" and len(seg) == 5 and seg[1] == "tile":
            if not self._authed():
                return
            try:
                z, x, y = int(seg[2]), int(seg[3]), int(seg[4])
            except ValueError:
                self._err("BAD_REQUEST", "tile coords must be integers")
                return
            self._send(200, {"z": z, "x": x, "y": y, "status": "PLACEHOLDER",
                             "note": "no map data in the reference universe"})
            return

        self._err("NOT_FOUND", f"no route GET /v1/{'/'.join(seg)}", 404)

    def do_POST(self):
        path = self.path.split("?")[0].rstrip("/")
        seg = path[len("/v1/"):].split("/") if path.startswith("/v1/") else []

        if path == "/v1/auth/token":
            body = json_body(self.rfile.read(int(self.headers.get("Content-Length", 0))))
            if not body or body.get("brad_id") != REFERENCE_USER or \
               body.get("password") != "reference-demo":
                self._err("BAD_CREDENTIALS", "unknown BradID or wrong password", 401)
                return
            scope = [s for s in (body.get("scope") or "mail cloud pay").split()
                     if s in ("mail", "cloud", "pay", "hub", "notify", "geo")]
            self._send(200, {"token": DEMO_TOKEN, "expires_in": 900, "scope": scope})
            return

        if not self._authed():
            return
        body = json_body(self.rfile.read(int(self.headers.get("Content-Length", 0))))

        if seg[:1] == ["pay"] and seg[1:] == ["transfer"]:
            to = body.get("to") if body else None
            amt = body.get("amount_cents") if body else None
            if not to or not isinstance(amt, int) or amt <= 0:
                self._err("BAD_REQUEST", "to (str) and amount_cents (int>0) required")
                return
            if amt + TRANSFER_FEE_CENTS > STATE.wallet_cents:
                self._err("BAD_REQUEST", "insufficient BradPay balance", 402)
                return
            STATE.wallet_cents -= amt + TRANSFER_FEE_CENTS
            STATE.txn_seq += 1
            txn = {"id": f"txn-{STATE.txn_seq:06d}", "from": REFERENCE_USER, "to": to,
                   "amount_cents": amt, "fee_cents": TRANSFER_FEE_CENTS,
                   "ledger_balance_cents": STATE.wallet_cents,
                   "status": "SETTLED", "created": now_iso()}
            STATE.transactions.append(txn)
            self._send(201, txn)
            return

        if seg[:1] == ["cloud"] and seg[1:] == ["files"]:
            if not body or not body.get("name") or not body.get("base64"):
                self._err("BAD_REQUEST", "name and base64 required")
                return
            raw = base64.b64decode(body["base64"])
            digest = hashlib.blake2b(raw, digest_size=16).hexdigest()
            oid = uuid.uuid4().hex[:12]
            STATE.files[oid] = {"id": oid, "name": body["name"],
                                "mime": body.get("mime", "application/octet-stream"),
                                "size_bytes": len(raw), "blake3_hex": digest[:32],
                                "created": now_iso()}
            self._send(201, STATE.files[oid])
            return

        if seg[:1] == ["notify"] and seg[1:] == ["send"]:
            if not body or not body.get("title") or not body.get("body"):
                self._err("BAD_REQUEST", "title and body required")
                return
            prio = body.get("priority", "normal")
            if prio not in ("critical", "high", "normal"):
                self._err("BAD_REQUEST", "priority must be critical|high|normal")
                return
            nid = f"ntf-{len(STATE.notifications) + 1:04d}"
            ntf = {"notification_id": nid, "priority": prio,
                   "delivered_to": [body.get("device_token", "demo-device")],
                   "created": now_iso()}
            STATE.notifications.append(ntf)
            self._send(201, ntf)
            return

        self._err("NOT_FOUND", f"no route POST /v1/{'/'.join(seg)}", 404)

    def do_PATCH(self):
        seg = self.path.split("?")[0].rstrip("/").replace("/v1/", "").split("/")
        if seg[:2] == ["mail", "messages"] and len(seg) == 3:
            if not self._authed():
                return
            body = json_body(self.rfile.read(int(self.headers.get("Content-Length", 0))))
            if not body or "unread" not in body or not isinstance(body["unread"], bool):
                self._err("BAD_REQUEST", "unread (bool) required")
                return
            self._send(200, {"id": seg[2], "unread": body["unread"],
                             "updated": now_iso()})
            return
        self._err("NOT_FOUND", "no PATCH route", 404)


def main():
    port = int(os.environ.get("BMS_PORT", "8085"))
    srv = ThreadingHTTPServer(("127.0.0.1", port), BMSHandler)
    print(f"BMS reference API v1 -> http://127.0.0.1:{port}/v1")
    print("demo: POST /v1/auth/token  {brad_id: demo@brad.dev, password: reference-demo}")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()