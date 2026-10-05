# BMS reference API — Brad Mobile Services (v1)

A **loopback reference** implementation of the BMS developer API.
Business intent and service architecture live in
`Brad Software/OS/BMS_SPEC.md`; this directory makes the API **concrete
and runnable**:

| File | What it is |
|------|-----------|
| `api_v1.md` | The REST v1 specification (routes, payloads, error envelope) |
| `reference_server.py` | Python-stdlib reference server, no dependencies |

```sh
python3 src/bms/reference_server.py        # http://127.0.0.1:8085/v1
```

Verify:

```sh
curl -s -X POST localhost:8085/v1/auth/token \
     -d '{"brad_id":"demo@brad.dev","password":"reference-demo"}' -H 'Content-Type: application/json'
# -> {"token":"bms-demo-token","expires_in":900,"scope":["mail","cloud","pay"]}

curl -s localhost:8085/v1/healthz
# -> {"status":"healthy","services":{"auth":"up",...}}
```

## Honest boundaries

- **One BradID.** There is no sign-up; only `demo@brad.dev` /
  `reference-demo` exists. No regions, no persistence.
- **In-memory state.** The BradPay ledger, cloud file table and
  notification log reset on restart.
- **Decorative payloads.** Mail subjects, hub ratings and map tiles are
  placeholders that exercise the API shape, not real data.
- **No TLS, no PII.** Bind is `127.0.0.1` only. This is a contract
  sample, not infrastructure — the gateway these routes describe is
  [Gen1] software for BradOS, not something running today.

Rate limits, auth model and service scopes follow `BMS_SPEC.md` §5.1.