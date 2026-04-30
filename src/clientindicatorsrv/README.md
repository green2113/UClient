# BestClient Indicator Server

Rust replacement for the old C++ UDP daemon plus Node.js HTTPS frontend.

Data flow:

`BestClient client -> HTTPS :8779/token.json -> UDP :8778 -> Rust service -> users.json -> HTTPS :8779/users.json -> clients/server browser`

Default ports:

- UDP presence: `8778`
- HTTPS web: `8779`

## Config

The service reads environment variables and `.env` files. Copy `.env.example` to `.env` for local deployment.

Important variables:

- `BC_CLIENT_INDICATOR_SHARED_TOKEN`: normal BestClient presence token. If empty, `TOKEN_PATH` is used.
- `BC_CLIENT_INDICATOR_SECRET_KEY`: developer icon secret. Alias: `BcClientIndicatorSecretKey`.
- `TOKEN_PATH`: fallback shared-token file.
- `JSON_PATH`: exported users JSON.
- `UDP_BIND`, `WEB_HOST`, `WEB_PORT`: bind addresses.
- `TLS_CERT_FILE`, `TLS_KEY_FILE`: HTTPS certificate files.

The developer secret is never exposed through `/token.json` or `/users.json`.

## Run

```bash
cd ~/BestClient
./src/clientindicatorsrv/run.sh start
./src/clientindicatorsrv/run.sh status
curl -k https://127.0.0.1:8779/healthz
curl -k https://127.0.0.1:8779/users.json
curl -k https://127.0.0.1:8779/token.json
```

Stop:

```bash
./src/clientindicatorsrv/run.sh stop
```

## Client Config

```txt
bc_client_indicator_server_address 150.241.70.188:8778
bc_client_indicator_browser_url https://150.241.70.188:8779/users.json
bc_client_indicator_token_url https://150.241.70.188:8779/token.json
bc_client_indicator_secret_key <developer secret, optional>
```

If `bc_client_indicator_secret_key` is empty, the client only registers normal BestClient presence. If it matches `BC_CLIENT_INDICATOR_SECRET_KEY`, the server marks that player as developer and includes `developer: true` in `/users.json`.
