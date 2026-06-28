# UClient Presence UDP Server

UDP relay for UClient presence packets (`UCP1`). Validated packets are forwarded to the
Cloudflare Pages `/api/presence/sync` endpoint over HTTPS with an HMAC-authenticated body.

Data flow:

`UClient -> UDP :8778 -> Rust relay -> HTTPS /api/presence/sync -> PRESENCE_KV`

## Config

Copy `.env.example` to `.env` for local deployment.

Important variables:

- `UC_PRESENCE_UDP_SHARED_TOKEN`: client UDP proof secret. If empty, `TOKEN_PATH` is used.
- `PRESENCE_SYNC_URL`: Worker sync endpoint (default `https://ddnet.under1111.com/api/presence/sync`).
- `PRESENCE_UDP_SYNC_SECRET`: HMAC secret for the sync request header `X-UClient-Presence-Sync`.
- `UDP_BIND`: UDP bind address (default `0.0.0.0:8778`).
- `HEARTBEAT_SYNC_DEBOUNCE_SEC`: minimum interval between heartbeat sync posts per player/session (default `60`).

## Run

```bash
cd ~/BestClient
./src/uclientpresencesrv/run.sh start
./src/uclientpresencesrv/run.sh status
```

Stop:

```bash
./src/uclientpresencesrv/run.sh stop
```

## Client Config

```txt
uc_presence_udp_server_address presence-udp.ddnet.under1111.com:8778
uc_presence_udp_shared_token <same value as UC_PRESENCE_UDP_SHARED_TOKEN>
uc_install_uuid <generated install uuid>
```

When UDP presence is configured, join/leave/switch/heartbeat events use UDP and HTTP is used only for the public GET list.
