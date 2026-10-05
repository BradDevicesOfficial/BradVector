# BMS REST API — v1 (reference specification)

> Brad Mobile Services API, **version 1**. Companion to
> `Brad Software/OS/BMS_SPEC.md` §5 (the developer API surface).
> This document makes that surface **concrete**: exact routes, request
> and response JSON, auth and error envelopes for a reference
> implementation.
>
> **Honest scope:** this is a *reference* API and reference server, not
> deployed infrastructure. There is exactly one BradID ("demo"), zero
> regions, and every payload is decorative until BradOS ships. The
> design rules (auth, scopes, rate limits, error shape) are the ones a
> real BMS gateway would use.

---

## 1. Conventions

| Rule | Value |
|------|-------|
| Base URL (reference) | `http://127.0.0.1:8085/v1` |
| Base URL (production intent) | `https://api.brad.dev/v1` |
| Content type | `application/json; charset=utf-8` |
| Auth | `Authorization: Bearer <token>` |
| Extra identity header | `X-Brad-Device: <device-fingerprint>` |
| Versioning | path-segment major (`/v1`); additive changes only within a major |
| Rate limits | per BMS_SPEC §5.1 (reference server enforces and returns `429`) |

### Error envelope

```json
{
  "error": {
    "code": "AUTH_REQUIRED",
    "message": "a Bearer token is required",
    "details": null
  }
}
```

Common codes: `AUTH_REQUIRED`, `INVALID_TOKEN`, `RATE_LIMITED`,
`NOT_FOUND`, `BAD_REQUEST`, `PAYLOAD_TOO_LARGE`.

### Date/time

ISO-8601 UTC, e.g. `2026-09-13T19:00:00Z`.

---

## 2. BradID auth

`POST /auth/token` — exchange BradID credentials for a session token.

```jsonc
// request
{ "brad_id": "demo@brad.dev", "password": "reference-demo", "scope": "mail cloud pay" }
// response 200
{ "token": "bms-demo-token", "expires_in": 900, "scope": ["mail", "cloud", "pay"] }
// response 401
{ "error": { "code": "BAD_CREDENTIALS", "message": "unknown BradID or wrong password" } }
```

`GET /auth/me` — current BradID, with the auto-provisioned mailbox.

```jsonc
{
  "brad_id": "demo@brad.dev",
  "display_name": "Brad (reference demo)",
  "mailbox": "demo@bradmail.com",
  "cloud_quota_bytes": 5368709120,
  "regions": ["ke-1"]
}
```

---

## 3. BradMail

`GET /mail/messages?limit=10&unread=1`

```jsonc
{
  "messages": [
    {
      "id": "msg-0001",
      "from": "system@brad.dev",
      "to": ["demo@bradmail.com"],
      "subject": "Your inbox is live",
      "preview": "This reference mailbox is ready.",
      "unread": true,
      "received": "2026-09-13T19:00:00Z"
    }
  ],
  "total": 1
}
```

`GET /mail/messages/{id}` — full message.

`PATCH /mail/messages/{id}` — mark read/unread: `{ "unread": false }`.

---

## 4. BradHub

`GET /hub/apps?tier=free`

```jsonc
{
  "apps": [
    {
      "id": "app-bradmail",
      "name": "BradMail",
      "publisher": "Brad Devices",
      "size_mb": 214,
      "tier": "free",
      "rating": 4.9
    }
  ]
}
```

---

## 5. BradPay

`POST /pay/transfer` — P2P wallet transfer (ledger is in-memory).

```jsonc
// request
{ "to": "friend@brad.dev", "amount_cents": 500, "note": "lunch" }
// response 201
{
  "id": "txn-000001",
  "from": "demo@brad.dev",
  "to": "friend@brad.dev",
  "amount_cents": 500,
  "ledger_balance_cents": 149500,
  "status": "SETTLED"
}
```

`GET /pay/wallet` — balance + last transactions.

---

## 6. BradCloud

`POST /cloud/files` — store a named blob (body is JSON `{base64, name, mime}`;
unlimited for the reference, honest header everywhere).

`GET /cloud/files` — file listing.

`GET /cloud/files/{id}` — metadata + size.

---

## 7. BradGeo

`GET /geo/tile/{z}/{x}/{y}` — vector tile slot. The reference returns an
honest placeholder: `{ "z":16, "x":0, "y":0, "status":"PLACEHOLDER",
"note":"no map data in the reference universe" }`.

---

## 8. BradNotify

`POST /notify/send` — deliver one notification (logged + returned).

```jsonc
// request
{ "device_token": "demo-device", "title": "hi", "body": "reference", "priority": "normal" }
// response 201
{ "notification_id": "ntf-0001", "delivered_to": ["demo-device"], "priority": "normal" }
```

Priorities: `critical | high | normal`.

---

## 9. Service status

`GET /` — version + service map.

`GET /healthz` — `{ "status": "healty", "services": { "mail": "up", ... } }`.

---

## 10. Reference server

`python3 src/bms/reference_server.py` runs the loopback server on
`127.0.0.1:8085` (variable `BMS_PORT` to change). Python stdlib only,
no dependency install. Every endpoint above is implemented; state is
in-memory and resets on restart. See `README.md` in this directory.