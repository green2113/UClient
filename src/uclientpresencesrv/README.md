# UClient Presence UDP Server

UDP relay for UClient presence packets (`UCP1`). Validated packets update an in-memory
presence map, export a live JSON snapshot, and serve it over HTTPS.

Data flow:

`UClient -> UDP :8778 -> Rust relay -> presence.json + GET /api/presence`

Optional legacy flow (disabled by default):

`Rust relay -> HTTPS /api/presence/sync -> Cloudflare KV`

When a client joins, leaves, or changes name/client id on a game server, the relay
notifies the other UC clients on that server in real time (`PEER_STATE`,
`PEER_REMOVE`, `PEER_LIST`). Clients track UC players by **game server client id**,
so renamed players keep the UC indicator.

## Config

Copy `.env.example` to `.env` for local deployment.

Important variables:

- `UC_PRESENCE_UDP_SHARED_TOKEN`: client UDP proof secret. If empty, `TOKEN_PATH` is used.
- `JSON_PATH`: exported live presence JSON (default `run/uclientpresencesrv/presence.json`).
- `UDP_BIND`, `WEB_HOST`, `WEB_PORT`: UDP and HTTPS bind addresses (default web port `8780`).
- `TLS_CERT_FILE`, `TLS_KEY_FILE`: HTTPS certificate files.
- `PRESENCE_SYNC_URL` / `PRESENCE_UDP_SYNC_SECRET`: optional legacy KV sync. Leave empty for JSON-only mode.

## Run

```bash
cd ~/BestClient
./src/uclientpresencesrv/run.sh start
./src/uclientpresencesrv/run.sh status
curl -k https://127.0.0.1:8780/healthz
curl -k https://127.0.0.1:8780/api/presence
```

Stop:

```bash
./src/uclientpresencesrv/run.sh stop
```

## Deployment

Point your public presence API at this service, for example:

- reverse-proxy `https://ddnet.under1111.com/api/presence` -> `https://127.0.0.1:8780/api/presence`

Or change the client config:

```txt
uc_presence_api_base_url https://presence-udp.ddnet.under1111.com:8780/api/presence
uc_presence_udp_server_address presence-udp.ddnet.under1111.com:8778
uc_presence_udp_shared_token <same value as UC_PRESENCE_UDP_SHARED_TOKEN>
uc_install_uuid <generated install uuid>
```

When UDP presence is configured, join/leave/switch/heartbeat events use UDP and HTTP is used only for the public GET list and as a 5-minute browser fallback. Same-server UC detection in-game uses UDP peer notifications.
