use constant_time_eq::constant_time_eq;
use hmac::{Hmac, Mac};
use hyper::server::conn::Http;
use serde::Serialize;
use sha2::{Digest, Sha256};
use std::collections::HashMap;
use std::convert::Infallible;
use std::env;
use std::fs;
use std::io;
use std::net::{IpAddr, SocketAddr};
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};
use tokio::net::{TcpListener, UdpSocket};
use tokio::time;
use tokio_rustls::{rustls::ServerConfig, TlsAcceptor};
use warp::Filter;

const PROTOCOL_MAGIC: [u8; 4] = [0x55, 0x43, 0x50, 0x31]; // UCP1
const PROTOCOL_VERSION_V1: u8 = 1;
const PROTOCOL_VERSION_V2: u8 = 2;
const DEFAULT_UDP_BIND: &str = "0.0.0.0:8778";
const DEFAULT_WEB_HOST: &str = "0.0.0.0";
const DEFAULT_WEB_PORT: u16 = 8780;
const DEFAULT_SYNC_URL: &str = "https://ddnet.under1111.com/api/presence/sync";
const MAX_UDP_PACKET_SIZE: usize = 2048;
const DEFAULT_TLS_HANDSHAKE_TIMEOUT: Duration = Duration::from_secs(5);
const DEFAULT_HTTP_HEADER_TIMEOUT: Duration = Duration::from_secs(5);
const DEFAULT_HTTPS_CONNECTION_TIMEOUT: Duration = Duration::from_secs(15);
const PROOF_SIZE: usize = 32;
const NONCE_RETENTION: Duration = Duration::from_secs(60);
const HEARTBEAT_TIMEOUT: Duration = Duration::from_secs(120);
const CLEANUP_INTERVAL: Duration = Duration::from_secs(1);
const DEFAULT_HEARTBEAT_SYNC_DEBOUNCE: Duration = Duration::from_secs(10);
const MAX_ENTRIES: usize = 5000;
const MAX_NONCES: usize = 20000;

const PACKET_JOIN: u8 = 1;
const PACKET_HEARTBEAT: u8 = 2;
const PACKET_LEAVE: u8 = 3;
const PACKET_SWITCH: u8 = 4;
const PACKET_PEER_STATE: u8 = 5;
const PACKET_PEER_REMOVE: u8 = 6;
const PACKET_PEER_LIST: u8 = 7;
const PACKET_REACTION: u8 = 8;
const PACKET_REACTION_BROADCAST: u8 = 9;
const PACKET_CURSOR: u8 = 10;
const PACKET_CURSOR_BROADCAST: u8 = 11;
const PACKET_CHAT: u8 = 12;
const PACKET_CHAT_BROADCAST: u8 = 13;
const PACKET_READ: u8 = 14;
const PACKET_READ_BROADCAST: u8 = 15;
const CHAT_SCOPE_SAME_SERVER: u8 = 0;
const CHAT_SCOPE_GLOBAL: u8 = 1;
const CHAT_MESSAGE_MAX_BYTES: usize = 512;
const CHAT_MIN_INTERVAL: Duration = Duration::from_millis(400);

#[derive(Clone)]
struct Config {
    udp_bind: SocketAddr,
    web_bind: SocketAddr,
    shared_token: String,
    json_path: PathBuf,
    tls_cert_file: Option<PathBuf>,
    tls_key_file: Option<PathBuf>,
    tls_handshake_timeout: Duration,
    http_header_timeout: Duration,
    https_connection_timeout: Duration,
    sync_enabled: bool,
    sync_url: String,
    sync_secret: String,
    heartbeat_sync_debounce: Duration,
}

#[derive(Clone, Hash, Eq, PartialEq)]
struct SessionKey {
    player_id: String,
    session_id: String,
}

#[derive(Clone)]
struct PresencePacket {
    packet_type: u8,
    player_id: String,
    session_id: [u8; 16],
    nonce: [u8; 16],
    timestamp: u64,
    server_address: String,
    player_name: String,
    client_id: i16,
    client_version: String,
    from_server_address: Option<String>,
}

#[derive(Clone)]
struct PresenceEntry {
    key: SessionKey,
    server_address: String,
    player_name: String,
    client_id: i16,
    client_version: String,
    last_seen: Instant,
    last_seen_ms: u64,
    return_addr: SocketAddr,
}

impl PresenceEntry {
    fn last_seen_unix(&self) -> u64 {
        if self.last_seen_ms > 0 {
            self.last_seen_ms / 1000
        } else {
            unix_timestamp()
        }
    }
}

struct UdpOutcome {
    sync_jobs: Vec<SyncJob>,
    outbound: Vec<(SocketAddr, Vec<u8>)>,
    dirty: bool,
}

#[derive(Serialize)]
struct SyncPayload<'a> {
    event: &'a str,
    playerId: &'a str,
    sessionId: &'a str,
    #[serde(skip_serializing_if = "Option::is_none")]
    server: Option<&'a str>,
    #[serde(skip_serializing_if = "Option::is_none")]
    name: Option<&'a str>,
    #[serde(skip_serializing_if = "Option::is_none")]
    clientId: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    version: Option<&'a str>,
    #[serde(skip_serializing_if = "Option::is_none")]
    fromServer: Option<&'a str>,
    #[serde(skip_serializing_if = "Option::is_none")]
    toServer: Option<&'a str>,
    #[serde(skip_serializing_if = "Option::is_none")]
    timestampMs: Option<u64>,
}

struct ServerState {
    entries: HashMap<SessionKey, PresenceEntry>,
    recent_nonces: HashMap<([u8; 16], [u8; 16]), Instant>,
    last_heartbeat_sync: HashMap<SessionKey, Instant>,
    last_chat_send: HashMap<SessionKey, Instant>,
    invalid_rate_by_ip: HashMap<IpAddr, Instant>,
    json_path: PathBuf,
}

impl ServerState {
    fn new(json_path: PathBuf) -> Self {
        Self {
            entries: HashMap::new(),
            recent_nonces: HashMap::new(),
            last_heartbeat_sync: HashMap::new(),
            last_chat_send: HashMap::new(),
            invalid_rate_by_ip: HashMap::new(),
            json_path,
        }
    }

    fn allow_chat_send(&mut self, key: &SessionKey, now: Instant) -> bool {
        match self.last_chat_send.get(key) {
            Some(last) if now.saturating_duration_since(*last) < CHAT_MIN_INTERVAL => false,
            _ => {
                self.last_chat_send.insert(key.clone(), now);
                true
            }
        }
    }

    fn remember_nonce(&mut self, session_id: [u8; 16], nonce: [u8; 16], now: Instant) -> bool {
        if self.recent_nonces.len() >= MAX_NONCES {
            self.cleanup_nonces(now);
            if self.recent_nonces.len() >= MAX_NONCES {
                return false;
            }
        }
        let key = (session_id, nonce);
        if let Some(seen) = self.recent_nonces.get(&key) {
            if now.saturating_duration_since(*seen) <= NONCE_RETENTION {
                return false;
            }
        }
        self.recent_nonces.insert(key, now);
        true
    }

    fn cleanup_nonces(&mut self, now: Instant) {
        self.recent_nonces
            .retain(|_, seen| now.saturating_duration_since(*seen) <= NONCE_RETENTION);
    }

    fn log_invalid(&mut self, ip: IpAddr, now: Instant, reason: &str) {
        let should_log = self
            .invalid_rate_by_ip
            .get(&ip)
            .map_or(true, |last| now.saturating_duration_since(*last) >= Duration::from_secs(10));
        if should_log {
            eprintln!("invalid uclient presence packet from {ip}: {reason}");
            self.invalid_rate_by_ip.insert(ip, now);
        }
    }

    fn upsert_entry(
        &mut self,
        packet: &PresencePacket,
        from: SocketAddr,
        now: Instant,
        key: &SessionKey,
    ) -> Option<PresenceEntry> {
        if packet.client_id < 0 || packet.server_address.is_empty() || packet.player_id.is_empty() {
            return None;
        }
        if self.entries.len() >= MAX_ENTRIES && !self.entries.contains_key(key) {
            return None;
        }
        let entry = PresenceEntry {
            key: key.clone(),
            server_address: packet.server_address.clone(),
            player_name: packet.player_name.clone(),
            client_id: packet.client_id,
            client_version: packet.client_version.clone(),
            last_seen: now,
            last_seen_ms: packet.timestamp.saturating_mul(1000),
            return_addr: from,
        };
        self.entries.insert(key.clone(), entry.clone());
        Some(entry)
    }

    fn remove_entry(&mut self, key: &SessionKey) -> Option<PresenceEntry> {
        let removed = self.entries.remove(key);
        if removed.is_some() {
            self.last_heartbeat_sync.remove(key);
        }
        removed
    }

    /// A single running client (same player_id + same session instance uuid) can
    /// only be on one game server at a time. When it appears on `keep_server`,
    /// drop any leftover entries it still has on other servers (e.g. after a
    /// server switch where the server-assigned client id changed), notifying the
    /// peers that remain on those old servers. Returns true if anything changed.
    fn purge_sessions_on_other_servers(
        &mut self,
        player_id: &str,
        session_id: &str,
        keep_server: &str,
        outbound: &mut Vec<(SocketAddr, Vec<u8>)>,
    ) -> bool {
        let instance = session_instance_of(session_id);
        let stale_keys: Vec<SessionKey> = self
            .entries
            .iter()
            .filter(|(candidate, entry)| {
                candidate.player_id == player_id
                    && entry.server_address != keep_server
                    && session_instance_of(&candidate.session_id) == instance
            })
            .map(|(candidate, _)| candidate.clone())
            .collect();
        if stale_keys.is_empty() {
            return false;
        }
        let mut removed_entries = Vec::with_capacity(stale_keys.len());
        for key in &stale_keys {
            if let Some(entry) = self.entries.remove(key) {
                self.last_heartbeat_sync.remove(key);
                removed_entries.push(entry);
            }
        }
        for entry in &removed_entries {
            append_peer_remove_notifications(self, entry, outbound);
        }
        !removed_entries.is_empty()
    }

    fn peers_on_server<'a>(
        &'a self,
        server_address: &str,
        except: Option<&SessionKey>,
    ) -> Vec<&'a PresenceEntry> {
        self.entries
            .iter()
            .filter_map(|(key, entry)| {
                if entry.server_address != server_address {
                    return None;
                }
                if except.is_some_and(|skip| skip == key) {
                    return None;
                }
                Some(entry)
            })
            .collect()
    }

    fn peers_all<'a>(&'a self, except: Option<&SessionKey>) -> Vec<&'a PresenceEntry> {
        self.entries
            .iter()
            .filter_map(|(key, entry)| {
                if except.is_some_and(|skip| skip == key) {
                    return None;
                }
                Some(entry)
            })
            .collect()
    }

    fn cleanup(&mut self, now: Instant) -> Vec<PresenceEntry> {
        let mut removed = Vec::new();
        self.entries.retain(|_, entry| {
            let expired = now.saturating_duration_since(entry.last_seen) > HEARTBEAT_TIMEOUT;
            if expired {
                removed.push(entry.clone());
            }
            !expired
        });
        for entry in &removed {
            self.last_heartbeat_sync.remove(&entry.key);
        }
        self.cleanup_nonces(now);
        self.invalid_rate_by_ip
            .retain(|_, last| now.saturating_duration_since(*last) <= Duration::from_secs(120));
        removed
    }

    fn should_sync_heartbeat(&mut self, key: &SessionKey, now: Instant, debounce: Duration) -> bool {
        match self.last_heartbeat_sync.get(key) {
            Some(last) if now.saturating_duration_since(*last) < debounce => false,
            _ => {
                self.last_heartbeat_sync.insert(key.clone(), now);
                true
            }
        }
    }

    fn snapshot_json(&self) -> String {
        let mut servers: HashMap<&str, Vec<&PresenceEntry>> = HashMap::new();
        for entry in self.entries.values() {
            servers.entry(entry.server_address.as_str()).or_default().push(entry);
        }

        let mut server_addresses: Vec<&str> = servers.keys().copied().collect();
        server_addresses.sort_unstable();

        let mut snapshot = Vec::with_capacity(server_addresses.len());
        for server_address in server_addresses {
            let mut players = servers.remove(server_address).unwrap_or_default();
            players.sort_by(|left, right| {
                left.player_name
                    .cmp(&right.player_name)
                    .then(left.client_id.cmp(&right.client_id))
            });
            let player_snapshots: Vec<PlayerSnapshot<'_>> = players
                .into_iter()
                .map(|entry| PlayerSnapshot {
                    name: entry.player_name.as_str(),
                    client_id: entry.client_id,
                    last_seen: entry.last_seen_unix(),
                    version: if entry.client_version.is_empty() {
                        None
                    } else {
                        Some(entry.client_version.as_str())
                    },
                })
                .collect();
            let mut server_object = serde_json::Map::new();
            server_object.insert(
                server_address.to_string(),
                serde_json::json!({ "players": player_snapshots }),
            );
            snapshot.push(serde_json::Value::Object(server_object));
        }
        serde_json::to_string(&snapshot).unwrap_or_else(|_| "[]".to_string())
    }

    fn write_snapshot(&self) -> io::Result<()> {
        write_file_atomically(&self.json_path, &self.snapshot_json())
    }
}

#[derive(Serialize)]
struct PlayerSnapshot<'a> {
    name: &'a str,
    #[serde(rename = "client_id")]
    client_id: i16,
    last_seen: u64,
    #[serde(skip_serializing_if = "Option::is_none")]
    version: Option<&'a str>,
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let _ = dotenvy::from_filename("src/uclientpresencesrv/.env");
    let _ = dotenvy::dotenv();
    let config = Arc::new(Config::load()?);
    let state = Arc::new(Mutex::new(ServerState::new(config.json_path.clone())));
    {
        let state = state.lock().unwrap();
        let _ = state.write_snapshot();
    }
    let client = reqwest::Client::builder()
        .timeout(Duration::from_secs(10))
        .build()?;

    let udp_socket = Arc::new(UdpSocket::bind(config.udp_bind).await?);
    eprintln!("uclient presence UDP listening on {}", config.udp_bind);
    let web_scheme = if config.tls_cert_file.is_some() && config.tls_key_file.is_some() {
        "https"
    } else {
        "http"
    };
    eprintln!(
        "uclient presence web listening on {}://{}",
        web_scheme, config.web_bind
    );
    eprintln!("uclient presence JSON path {}", config.json_path.display());
    if config.sync_enabled {
        eprintln!("uclient presence legacy KV sync enabled: {}", config.sync_url);
    } else {
        eprintln!("uclient presence legacy KV sync disabled (JSON snapshot mode)");
    }

    let udp_task = tokio::spawn(udp_loop(
        Arc::clone(&udp_socket),
        Arc::clone(&state),
        Arc::clone(&config),
        client.clone(),
    ));
    let cleanup_task = tokio::spawn(cleanup_loop(
        Arc::clone(&udp_socket),
        Arc::clone(&state),
        Arc::clone(&config),
        client.clone(),
    ));
    let web_task = tokio::spawn(web_loop(Arc::clone(&state), Arc::clone(&config)));

    tokio::select! {
        result = udp_task => result??,
        result = cleanup_task => result??,
        result = web_task => result??,
        _ = tokio::signal::ctrl_c() => {},
    }
    Ok(())
}

impl Config {
    fn load() -> Result<Self, Box<dyn std::error::Error + Send + Sync>> {
        let state_dir = env::var("STATE_DIR").unwrap_or_else(|_| "run/uclientpresencesrv".to_string());
        let token_path = env::var("TOKEN_PATH").unwrap_or_else(|_| format!("{state_dir}/shared-token.txt"));
        let shared_token = env::var("UC_PRESENCE_UDP_SHARED_TOKEN")
            .ok()
            .filter(|token| !token.trim().is_empty())
            .unwrap_or_else(|| fs::read_to_string(&token_path).unwrap_or_default())
            .trim()
            .to_string();
        if shared_token.is_empty() {
            return Err("UC_PRESENCE_UDP_SHARED_TOKEN or TOKEN_PATH must provide a non-empty shared token".into());
        }

        let sync_url = env::var("PRESENCE_SYNC_URL")
            .unwrap_or_default()
            .trim()
            .to_string();
        let sync_secret = env::var("PRESENCE_UDP_SYNC_SECRET")
            .unwrap_or_default()
            .trim()
            .to_string();
        let sync_enabled = !sync_url.is_empty() && !sync_secret.is_empty();
        if !sync_url.is_empty() && sync_secret.is_empty() {
            return Err("PRESENCE_UDP_SYNC_SECRET must be set when PRESENCE_SYNC_URL is configured".into());
        }

        let udp_bind = env::var("UDP_BIND")
            .unwrap_or_else(|_| DEFAULT_UDP_BIND.to_string())
            .parse()?;
        let web_host = env::var("WEB_HOST").unwrap_or_else(|_| DEFAULT_WEB_HOST.to_string());
        let web_port: u16 = env::var("WEB_PORT")
            .ok()
            .and_then(|value| value.parse().ok())
            .unwrap_or(DEFAULT_WEB_PORT);
        let web_bind = format!("{web_host}:{web_port}").parse()?;
        let json_path = env::var("JSON_PATH")
            .unwrap_or_else(|_| format!("{state_dir}/presence.json"))
            .into();
        // TLS is optional: when both cert and key are provided the web server
        // serves HTTPS, otherwise it falls back to plain HTTP (e.g. behind a
        // Cloudflare Tunnel that terminates TLS at the edge).
        let tls_cert_file = env::var("TLS_CERT_FILE")
            .ok()
            .map(|value| value.trim().to_string())
            .filter(|value| !value.is_empty())
            .map(PathBuf::from);
        let tls_key_file = env::var("TLS_KEY_FILE")
            .ok()
            .map(|value| value.trim().to_string())
            .filter(|value| !value.is_empty())
            .map(PathBuf::from);
        let tls_handshake_timeout =
            env_duration_ms("WEB_TLS_HANDSHAKE_TIMEOUT_MS", DEFAULT_TLS_HANDSHAKE_TIMEOUT);
        let http_header_timeout =
            env_duration_ms("WEB_HTTP_HEADER_TIMEOUT_MS", DEFAULT_HTTP_HEADER_TIMEOUT);
        let https_connection_timeout =
            env_duration_ms("WEB_HTTPS_CONNECTION_TIMEOUT_MS", DEFAULT_HTTPS_CONNECTION_TIMEOUT);
        let heartbeat_sync_debounce = env::var("HEARTBEAT_SYNC_DEBOUNCE_SEC")
            .ok()
            .and_then(|value| value.parse::<u64>().ok())
            .map(Duration::from_secs)
            .unwrap_or(DEFAULT_HEARTBEAT_SYNC_DEBOUNCE);

        Ok(Self {
            udp_bind,
            web_bind,
            shared_token,
            json_path,
            tls_cert_file,
            tls_key_file,
            tls_handshake_timeout,
            http_header_timeout,
            https_connection_timeout,
            sync_enabled,
            sync_url,
            sync_secret,
            heartbeat_sync_debounce,
        })
    }
}

fn env_duration_ms(name: &str, default: Duration) -> Duration {
    env::var(name)
        .ok()
        .and_then(|value| value.parse::<u64>().ok())
        .map(Duration::from_millis)
        .unwrap_or(default)
}

async fn udp_loop(
    socket: Arc<UdpSocket>,
    state: Arc<Mutex<ServerState>>,
    config: Arc<Config>,
    client: reqwest::Client,
) -> io::Result<()> {
    let mut buf = [0u8; MAX_UDP_PACKET_SIZE];
    loop {
        let (size, from) = socket.recv_from(&mut buf).await?;
        let data = &buf[..size];
        let outcome = {
            let mut state = state.lock().unwrap();
            let outcome = handle_udp_packet(
                &mut state,
                &config.shared_token,
                from.ip(),
                from,
                data,
                config.heartbeat_sync_debounce,
                config.sync_enabled,
            );
            if outcome.dirty {
                if let Err(err) = state.write_snapshot() {
                    eprintln!("presence snapshot write failed: {err}");
                }
            }
            outcome
        };
        if config.sync_enabled {
            for job in outcome.sync_jobs {
                if let Err(err) = post_sync(&client, &config, job).await {
                    eprintln!("presence sync failed: {err}");
                }
            }
        }
        for (addr, packet) in outcome.outbound {
            if let Err(err) = socket.send_to(&packet, addr).await {
                eprintln!("presence peer notify failed for {addr}: {err}");
            }
        }
    }
}

async fn cleanup_loop(
    socket: Arc<UdpSocket>,
    state: Arc<Mutex<ServerState>>,
    config: Arc<Config>,
    client: reqwest::Client,
) -> io::Result<()> {
    let mut interval = time::interval(CLEANUP_INTERVAL);
    loop {
        interval.tick().await;
        let (removed_count, outbound, sync_jobs) = {
            let mut state = state.lock().unwrap();
            let removed = state.cleanup(Instant::now());
            let removed_count = removed.len();
            let mut packets = Vec::new();
            let sync_jobs: Vec<SyncJob> = removed
                .iter()
                .map(|entry| {
                    let timestamp_ms = if entry.last_seen_ms > 0 {
                        entry.last_seen_ms
                    } else {
                        entry.last_seen_unix().saturating_mul(1000)
                    };
                    SyncJob::leave(entry.clone(), timestamp_ms)
                })
                .collect();
            for entry in removed {
                append_peer_remove_notifications(&state, &entry, &mut packets);
            }
            if removed_count > 0 {
                if let Err(err) = state.write_snapshot() {
                    eprintln!("presence snapshot write failed during cleanup: {err}");
                }
            }
            (removed_count, packets, sync_jobs)
        };
        if removed_count > 0 {
            eprintln!(
                "presence cleanup evicted {} stale udp entries and refreshed presence.json",
                removed_count
            );
        }
        if config.sync_enabled {
            for job in sync_jobs {
                if let Err(err) = post_sync(&client, &config, job).await {
                    eprintln!("presence leave sync failed: {err}");
                }
            }
        }
        for (addr, packet) in outbound {
            if let Err(err) = socket.send_to(&packet, addr).await {
                eprintln!("presence cleanup notify failed for {addr}: {err}");
            }
        }
    }
}

struct SyncJob {
    event: &'static str,
    player_id: String,
    session_id: String,
    server: Option<String>,
    name: Option<String>,
    client_id: Option<i16>,
    version: Option<String>,
    from_server: Option<String>,
    to_server: Option<String>,
    timestamp_ms: u64,
    force: bool,
}

impl SyncJob {
    fn from_packet(packet: &PresencePacket) -> Self {
        let timestamp_ms = if packet.timestamp > 0 {
            packet.timestamp.saturating_mul(1000)
        } else {
            now_ms()
        };
        let event = match packet.packet_type {
            PACKET_JOIN => "join",
            PACKET_HEARTBEAT => "heartbeat",
            PACKET_LEAVE => "leave",
            PACKET_SWITCH => "switch",
            _ => "heartbeat",
        };
        Self {
            event,
            player_id: packet.player_id.clone(),
            session_id: session_id_for_packet(&packet),
            server: if packet.packet_type == PACKET_SWITCH {
                packet.from_server_address.clone()
            } else {
                Some(packet.server_address.clone())
            },
            name: Some(packet.player_name.clone()),
            client_id: Some(packet.client_id),
            version: if packet.client_version.is_empty() {
                None
            } else {
                Some(packet.client_version.clone())
            },
            from_server: packet.from_server_address.clone(),
            to_server: if packet.packet_type == PACKET_SWITCH {
                Some(packet.server_address.clone())
            } else {
                None
            },
            timestamp_ms,
            force: packet.packet_type != PACKET_HEARTBEAT,
        }
    }

    fn leave(entry: PresenceEntry, timestamp_ms: u64) -> Self {
        Self {
            event: "leave",
            player_id: entry.key.player_id,
            session_id: entry.key.session_id,
            server: Some(entry.server_address),
            name: Some(entry.player_name),
            client_id: Some(entry.client_id),
            version: if entry.client_version.is_empty() {
                None
            } else {
                Some(entry.client_version)
            },
            from_server: None,
            to_server: None,
            timestamp_ms,
            force: true,
        }
    }
}

fn handle_udp_packet(
    state: &mut ServerState,
    shared_token: &str,
    ip: IpAddr,
    from: SocketAddr,
    data: &[u8],
    _heartbeat_sync_debounce: Duration,
    sync_enabled: bool,
) -> UdpOutcome {
    let mut outcome = UdpOutcome {
        sync_jobs: Vec::new(),
        outbound: Vec::new(),
        dirty: false,
    };
    let now = Instant::now();
    if data.len() > MAX_UDP_PACKET_SIZE {
        return outcome;
    }
    let Some(packet_type) = packet_type(data) else {
        state.log_invalid(ip, now, "bad header");
        return outcome;
    };

    // Chat reactions are relayed to every UC peer on the same game server.
    if packet_type == PACKET_REACTION {
        let Some(reaction) = read_reaction_packet(data) else {
            state.log_invalid(ip, now, "bad reaction payload");
            return outcome;
        };
        if !validate_proof(shared_token, data) {
            state.log_invalid(ip, now, "invalid reaction proof");
            return outcome;
        }
        if !state.remember_nonce(reaction.session_id, reaction.nonce, now) {
            state.log_invalid(ip, now, "reaction nonce replay");
            return outcome;
        }
        let sender_key = SessionKey {
            player_id: reaction.player_id.clone(),
            session_id: format!(
                "{}:{}",
                format_uuid(reaction.session_id),
                reaction.reactor_client_id
            ),
        };
        let packet = encode_reaction_broadcast(
            &reaction.server_address,
            &reaction.reactor_name,
            reaction.reactor_client_id,
            reaction.target_client_id,
            reaction.message_hash,
            &reaction.emoji,
            reaction.action,
        );
        for peer in state.peers_on_server(&reaction.server_address, Some(&sender_key)) {
            outcome.outbound.push((peer.return_addr, packet.clone()));
        }
        return outcome;
    }

    // Live cursor sharing is relayed to every UC peer on the same game server. This runs at
    // ~25Hz per active sender, so we intentionally skip the nonce replay check (the cache would
    // thrash and replaying a stale cursor position is harmless). Only the proof is validated.
    if packet_type == PACKET_CURSOR {
        let Some(cursor) = read_cursor_packet(data) else {
            state.log_invalid(ip, now, "bad cursor payload");
            return outcome;
        };
        if !validate_proof(shared_token, data) {
            state.log_invalid(ip, now, "invalid cursor proof");
            return outcome;
        }
        let sender_key = SessionKey {
            player_id: cursor.player_id.clone(),
            session_id: format!(
                "{}:{}",
                format_uuid(cursor.session_id),
                cursor.sender_client_id
            ),
        };
        let packet = encode_cursor_broadcast(
            &cursor.server_address,
            &cursor.sender_name,
            cursor.sender_client_id,
            cursor.active,
            cursor.world_x,
            cursor.world_y,
        );
        for peer in state.peers_on_server(&cursor.server_address, Some(&sender_key)) {
            outcome.outbound.push((peer.return_addr, packet.clone()));
        }
        return outcome;
    }

    // UClient chat: same-server or global relay depending on scope.
    if packet_type == PACKET_CHAT {
        let Some(chat) = read_chat_packet(data) else {
            state.log_invalid(ip, now, "bad chat payload");
            return outcome;
        };
        if !validate_proof(shared_token, data) {
            state.log_invalid(ip, now, "invalid chat proof");
            return outcome;
        }
        if chat.message.is_empty() || chat.message.len() > CHAT_MESSAGE_MAX_BYTES {
            state.log_invalid(ip, now, "invalid chat message length");
            return outcome;
        }
        if chat.scope != CHAT_SCOPE_SAME_SERVER && chat.scope != CHAT_SCOPE_GLOBAL {
            state.log_invalid(ip, now, "invalid chat scope");
            return outcome;
        }
        if !state.remember_nonce(chat.session_id, chat.nonce, now) {
            state.log_invalid(ip, now, "chat nonce replay");
            return outcome;
        }
        let sender_key = SessionKey {
            player_id: chat.player_id.clone(),
            session_id: format!(
                "{}:{}",
                format_uuid(chat.session_id),
                chat.sender_client_id
            ),
        };
        if !state.allow_chat_send(&sender_key, now) {
            state.log_invalid(ip, now, "chat rate limited");
            return outcome;
        }
        let packet = encode_chat_broadcast(
            &chat.server_address,
            &chat.sender_name,
            chat.sender_client_id,
            chat.scope,
            &chat.message,
            &chat.skin_name,
            chat.use_custom_color,
            chat.color_body,
            chat.color_feet,
            chat.message_id,
        );
        let peers = if chat.scope == CHAT_SCOPE_SAME_SERVER {
            state.peers_on_server(&chat.server_address, Some(&sender_key))
        } else {
            state.peers_all(Some(&sender_key))
        };
        for peer in peers {
            outcome.outbound.push((peer.return_addr, packet.clone()));
        }
        return outcome;
    }

    // UClient chat read receipts: always relayed globally (the message author may be on a
    // different game server). Self-echo is suppressed on the client by reader key.
    if packet_type == PACKET_READ {
        let Some(read) = read_read_packet(data) else {
            state.log_invalid(ip, now, "bad read payload");
            return outcome;
        };
        if !validate_proof(shared_token, data) {
            state.log_invalid(ip, now, "invalid read proof");
            return outcome;
        }
        if !state.remember_nonce(read.session_id, read.nonce, now) {
            state.log_invalid(ip, now, "read nonce replay");
            return outcome;
        }
        let packet = encode_read_broadcast(
            &read.server_address,
            &read.reader_name,
            read.reader_key,
            read.message_id,
        );
        for peer in state.peers_all(None) {
            outcome.outbound.push((peer.return_addr, packet.clone()));
        }
        return outcome;
    }

    if !matches!(
        packet_type,
        PACKET_JOIN | PACKET_HEARTBEAT | PACKET_LEAVE | PACKET_SWITCH
    ) {
        state.log_invalid(ip, now, "unsupported packet type");
        return outcome;
    }

    let Some(packet) = read_presence_packet(data) else {
        state.log_invalid(ip, now, "bad presence payload");
        return outcome;
    };
    if !validate_proof(shared_token, data) {
        state.log_invalid(ip, now, "invalid presence proof");
        return outcome;
    }
    if !state.remember_nonce(packet.session_id, packet.nonce, now) {
        state.log_invalid(ip, now, "nonce replay");
        return outcome;
    }

    let key = SessionKey {
        player_id: packet.player_id.clone(),
        session_id: session_id_for_packet(&packet),
    };
    let previous = state.entries.get(&key).cloned();

    let mut job = SyncJob::from_packet(&packet);
    if packet.packet_type == PACKET_HEARTBEAT {
        job.force = true;
    }
    if sync_enabled && job.force {
        outcome.sync_jobs.push(job);
    }

    match packet.packet_type {
        PACKET_LEAVE => {
            if let Some(entry) = state.remove_entry(&key) {
                append_peer_remove_notifications(state, &entry, &mut outcome.outbound);
                outcome.dirty = true;
            }
        }
        PACKET_SWITCH => {
            if let Some(prev) = previous.as_ref() {
                if prev.server_address != packet.server_address {
                    append_peer_remove_notifications(state, prev, &mut outcome.outbound);
                }
            }
            if let Some(entry) = state.upsert_entry(&packet, from, now, &key) {
                state.purge_sessions_on_other_servers(
                    &packet.player_id,
                    &key.session_id,
                    &entry.server_address,
                    &mut outcome.outbound,
                );
                append_join_notifications(state, &entry, from, &key, &mut outcome.outbound);
                outcome.dirty = true;
            }
        }
        PACKET_JOIN | PACKET_HEARTBEAT => {
            let was_new = previous.is_none();
            if let Some(entry) = state.upsert_entry(&packet, from, now, &key) {
                outcome.dirty = true;
                state.purge_sessions_on_other_servers(
                    &packet.player_id,
                    &key.session_id,
                    &entry.server_address,
                    &mut outcome.outbound,
                );
                if packet.packet_type == PACKET_JOIN || was_new {
                    // Tell the joiner about every UC peer already on this game server,
                    // and tell those peers about the joiner.
                    append_join_notifications(state, &entry, from, &key, &mut outcome.outbound);
                } else {
                    // Heartbeat refresh: PEER_LIST can be lost over UDP, so resend the
                    // current UC peer snapshot to this client on every heartbeat.
                    append_peer_list_to(
                        state,
                        &entry.server_address,
                        Some(&key),
                        from,
                        &mut outcome.outbound,
                    );

                    if let Some(prev) = previous {
                        if prev.server_address == entry.server_address {
                            if prev.client_id != entry.client_id {
                                append_peer_remove_for_entry(
                                    state,
                                    &prev.server_address,
                                    prev.client_id,
                                    &prev.player_name,
                                    Some(&key),
                                    &mut outcome.outbound,
                                );
                                append_peer_state_notifications(
                                    state,
                                    &entry,
                                    Some(&key),
                                    &mut outcome.outbound,
                                );
                            } else if prev.player_name != entry.player_name {
                                append_peer_state_notifications(
                                    state,
                                    &entry,
                                    Some(&key),
                                    &mut outcome.outbound,
                                );
                            }
                        }
                    }
                }
            }
        }
        _ => {}
    }

    outcome
}

fn append_peer_list_to(
    state: &ServerState,
    server_address: &str,
    except: Option<&SessionKey>,
    to_addr: SocketAddr,
    outbound: &mut Vec<(SocketAddr, Vec<u8>)>,
) {
    let peers = state.peers_on_server(server_address, except);
    let mut list_entries = Vec::with_capacity(peers.len());
    for peer in &peers {
        list_entries.push((peer.client_id, peer.player_name.clone()));
    }
    outbound.push((to_addr, encode_peer_list(server_address, &list_entries)));
}

fn append_join_notifications(
    state: &ServerState,
    entry: &PresenceEntry,
    joiner_addr: SocketAddr,
    joiner_key: &SessionKey,
    outbound: &mut Vec<(SocketAddr, Vec<u8>)>,
) {
    let peers = state.peers_on_server(&entry.server_address, Some(joiner_key));

    // Always send the current peer snapshot to the joiner (may be empty).
    // Also send individual PEER_STATE packets so a lost PEER_LIST still leaves partial info.
    let mut list_entries = Vec::with_capacity(peers.len());
    for peer in &peers {
        list_entries.push((peer.client_id, peer.player_name.clone()));
        outbound.push((
            joiner_addr,
            encode_peer_state(&entry.server_address, &peer.player_name, peer.client_id),
        ));
    }
    outbound.push((
        joiner_addr,
        encode_peer_list(&entry.server_address, &list_entries),
    ));

    // Tell everyone already on the server about the joiner.
    if !peers.is_empty() {
        let join_packet =
            encode_peer_state(&entry.server_address, &entry.player_name, entry.client_id);
        for peer in peers {
            outbound.push((peer.return_addr, join_packet.clone()));
        }
    }
}

fn append_peer_state_notifications(
    state: &ServerState,
    entry: &PresenceEntry,
    except: Option<&SessionKey>,
    outbound: &mut Vec<(SocketAddr, Vec<u8>)>,
) {
    let packet = encode_peer_state(&entry.server_address, &entry.player_name, entry.client_id);
    for peer in state.peers_on_server(&entry.server_address, except) {
        outbound.push((peer.return_addr, packet.clone()));
    }
    if except.is_none() {
        outbound.push((entry.return_addr, packet));
    }
}

fn append_peer_remove_for_entry(
    state: &ServerState,
    server_address: &str,
    client_id: i16,
    player_name: &str,
    except: Option<&SessionKey>,
    outbound: &mut Vec<(SocketAddr, Vec<u8>)>,
) {
    let packet = encode_peer_remove(server_address, player_name, client_id);
    for peer in state.peers_on_server(server_address, except) {
        outbound.push((peer.return_addr, packet.clone()));
    }
}

fn append_peer_remove_notifications(
    state: &ServerState,
    entry: &PresenceEntry,
    outbound: &mut Vec<(SocketAddr, Vec<u8>)>,
) {
    append_peer_remove_for_entry(
        state,
        &entry.server_address,
        entry.client_id,
        &entry.player_name,
        Some(&entry.key),
        outbound,
    );
}

fn encode_peer_state(server_address: &str, player_name: &str, client_id: i16) -> Vec<u8> {
    let mut out = Vec::new();
    write_header(&mut out, PACKET_PEER_STATE);
    write_string(&mut out, server_address);
    write_string(&mut out, player_name);
    out.extend_from_slice(&client_id.to_be_bytes());
    out
}

fn encode_peer_remove(server_address: &str, player_name: &str, client_id: i16) -> Vec<u8> {
    let mut out = Vec::new();
    write_header(&mut out, PACKET_PEER_REMOVE);
    write_string(&mut out, server_address);
    write_string(&mut out, player_name);
    out.extend_from_slice(&client_id.to_be_bytes());
    out
}

fn encode_reaction_broadcast(
    server_address: &str,
    reactor_name: &str,
    reactor_client_id: i16,
    target_client_id: i16,
    message_hash: u64,
    emoji: &str,
    action: u8,
) -> Vec<u8> {
    let mut out = Vec::new();
    write_header(&mut out, PACKET_REACTION_BROADCAST);
    write_string(&mut out, server_address);
    write_string(&mut out, reactor_name);
    out.extend_from_slice(&reactor_client_id.to_be_bytes());
    out.extend_from_slice(&target_client_id.to_be_bytes());
    out.extend_from_slice(&message_hash.to_be_bytes());
    write_string(&mut out, emoji);
    out.push(action);
    out
}

fn encode_cursor_broadcast(
    server_address: &str,
    sender_name: &str,
    sender_client_id: i16,
    active: u8,
    world_x: i32,
    world_y: i32,
) -> Vec<u8> {
    let mut out = Vec::new();
    write_header(&mut out, PACKET_CURSOR_BROADCAST);
    write_string(&mut out, server_address);
    write_string(&mut out, sender_name);
    out.extend_from_slice(&sender_client_id.to_be_bytes());
    out.push(active);
    out.extend_from_slice(&world_x.to_be_bytes());
    out.extend_from_slice(&world_y.to_be_bytes());
    out
}

fn encode_chat_broadcast(
    server_address: &str,
    sender_name: &str,
    sender_client_id: i16,
    scope: u8,
    message: &str,
    skin_name: &str,
    use_custom_color: u8,
    color_body: i32,
    color_feet: i32,
    message_id: [u8; 16],
) -> Vec<u8> {
    let mut out = Vec::new();
    write_header(&mut out, PACKET_CHAT_BROADCAST);
    write_string(&mut out, server_address);
    write_string(&mut out, sender_name);
    out.extend_from_slice(&sender_client_id.to_be_bytes());
    out.push(scope);
    write_string(&mut out, message);
    write_string(&mut out, skin_name);
    out.push(use_custom_color);
    out.extend_from_slice(&color_body.to_be_bytes());
    out.extend_from_slice(&color_feet.to_be_bytes());
    out.extend_from_slice(&message_id);
    out
}

fn encode_read_broadcast(
    server_address: &str,
    reader_name: &str,
    reader_key: [u8; 16],
    message_id: [u8; 16],
) -> Vec<u8> {
    let mut out = Vec::new();
    write_header(&mut out, PACKET_READ_BROADCAST);
    write_string(&mut out, server_address);
    write_string(&mut out, reader_name);
    out.extend_from_slice(&reader_key);
    out.extend_from_slice(&message_id);
    out
}

fn encode_peer_list(server_address: &str, peers: &[(i16, String)]) -> Vec<u8> {
    let mut out = Vec::new();
    write_header(&mut out, PACKET_PEER_LIST);
    write_string(&mut out, server_address);
    write_u16(&mut out, peers.len() as u16);
    for (client_id, player_name) in peers {
        out.extend_from_slice(&client_id.to_be_bytes());
        write_string(&mut out, player_name);
    }
    out
}

fn write_header(out: &mut Vec<u8>, packet_type: u8) {
    out.extend_from_slice(&PROTOCOL_MAGIC);
    out.push(packet_type);
    out.push(PROTOCOL_VERSION_V2);
}

fn write_u16(out: &mut Vec<u8>, value: u16) {
    out.extend_from_slice(&value.to_be_bytes());
}

fn write_string(out: &mut Vec<u8>, value: &str) {
    let bytes = value.as_bytes();
    write_u16(out, bytes.len() as u16);
    out.extend_from_slice(bytes);
}

async fn post_sync(client: &reqwest::Client, config: &Config, job: SyncJob) -> Result<(), reqwest::Error> {
    if !config.sync_enabled {
        return Ok(());
    }
    let client_id = job.client_id.map(|value| value.to_string());
    let payload = SyncPayload {
        event: job.event,
        playerId: &job.player_id,
        sessionId: &job.session_id,
        server: job.server.as_deref(),
        name: job.name.as_deref(),
        clientId: client_id,
        version: job.version.as_deref(),
        fromServer: job.from_server.as_deref(),
        toServer: job.to_server.as_deref(),
        timestampMs: Some(job.timestamp_ms),
    };
    let body = serde_json::to_string(&payload).unwrap_or_else(|_| "{}".to_string());
    let signature = sign_body(&config.sync_secret, &body);
    client
        .post(&config.sync_url)
        .header("content-type", "application/json; charset=utf-8")
        .header("X-UClient-Presence-Sync", signature)
        .body(body)
        .send()
        .await?
        .error_for_status()?;
    Ok(())
}

fn wire_protocol_version(data: &[u8]) -> Option<u8> {
    if data.len() < 6 || data[..4] != PROTOCOL_MAGIC {
        return None;
    }
    // Chat packets reuse the v2 header; accept v1/v2 only (same as presence).
    let version = data[5];
    if version != PROTOCOL_VERSION_V1 && version != PROTOCOL_VERSION_V2 {
        return None;
    }
    Some(version)
}

fn packet_type(data: &[u8]) -> Option<u8> {
    wire_protocol_version(data)?;
    Some(data[4])
}

fn read_presence_packet(data: &[u8]) -> Option<PresencePacket> {
    let packet_type = packet_type(data)?;
    let wire_version = wire_protocol_version(data)?;
    if data.len() < PROOF_SIZE {
        return None;
    }
    let payload_len = data.len().checked_sub(PROOF_SIZE)?;
    let mut reader = Reader::new(&data[..payload_len]);
    reader.skip(6)?;
    let player_id = reader.string()?;
    let session_id = reader.uuid()?;
    let nonce = reader.uuid()?;
    let timestamp = reader.u64()?;
    let server_address = reader.string()?;
    let player_name = reader.string()?;
    let client_id = reader.i16()?;
    let client_version = if wire_version >= PROTOCOL_VERSION_V2 {
        reader.string()?
    } else {
        String::new()
    };
    let from_server_address = if packet_type == PACKET_SWITCH {
        Some(reader.string()?)
    } else {
        None
    };
    if reader.remaining() != 0 {
        return None;
    }
    Some(PresencePacket {
        packet_type,
        player_id,
        session_id,
        nonce,
        timestamp,
        server_address,
        player_name,
        client_id,
        client_version,
        from_server_address,
    })
}

struct ReactionPacket {
    player_id: String,
    session_id: [u8; 16],
    nonce: [u8; 16],
    server_address: String,
    reactor_name: String,
    reactor_client_id: i16,
    target_client_id: i16,
    message_hash: u64,
    emoji: String,
    action: u8,
}

fn read_reaction_packet(data: &[u8]) -> Option<ReactionPacket> {
    if packet_type(data)? != PACKET_REACTION {
        return None;
    }
    if data.len() < PROOF_SIZE {
        return None;
    }
    let payload_len = data.len().checked_sub(PROOF_SIZE)?;
    let mut reader = Reader::new(&data[..payload_len]);
    reader.skip(6)?;
    let player_id = reader.string()?;
    let session_id = reader.uuid()?;
    let nonce = reader.uuid()?;
    let _timestamp = reader.u64()?;
    let server_address = reader.string()?;
    let reactor_name = reader.string()?;
    let reactor_client_id = reader.i16()?;
    let target_client_id = reader.i16()?;
    let message_hash = reader.u64()?;
    let emoji = reader.string()?;
    let action = reader.u8()?;
    if reader.remaining() != 0 {
        return None;
    }
    Some(ReactionPacket {
        player_id,
        session_id,
        nonce,
        server_address,
        reactor_name,
        reactor_client_id,
        target_client_id,
        message_hash,
        emoji,
        action,
    })
}

struct CursorPacket {
    player_id: String,
    session_id: [u8; 16],
    server_address: String,
    sender_name: String,
    sender_client_id: i16,
    active: u8,
    world_x: i32,
    world_y: i32,
}

fn read_cursor_packet(data: &[u8]) -> Option<CursorPacket> {
    if packet_type(data)? != PACKET_CURSOR {
        return None;
    }
    if data.len() < PROOF_SIZE {
        return None;
    }
    let payload_len = data.len().checked_sub(PROOF_SIZE)?;
    let mut reader = Reader::new(&data[..payload_len]);
    reader.skip(6)?;
    let player_id = reader.string()?;
    let session_id = reader.uuid()?;
    let _nonce = reader.uuid()?;
    let _timestamp = reader.u64()?;
    let server_address = reader.string()?;
    let sender_name = reader.string()?;
    let sender_client_id = reader.i16()?;
    let active = reader.u8()?;
    let world_x = reader.i32()?;
    let world_y = reader.i32()?;
    if reader.remaining() != 0 {
        return None;
    }
    Some(CursorPacket {
        player_id,
        session_id,
        server_address,
        sender_name,
        sender_client_id,
        active,
        world_x,
        world_y,
    })
}

struct ChatPacket {
    player_id: String,
    session_id: [u8; 16],
    nonce: [u8; 16],
    server_address: String,
    sender_name: String,
    sender_client_id: i16,
    scope: u8,
    message: String,
    skin_name: String,
    use_custom_color: u8,
    color_body: i32,
    color_feet: i32,
    message_id: [u8; 16],
}

fn read_chat_packet(data: &[u8]) -> Option<ChatPacket> {
    if packet_type(data)? != PACKET_CHAT {
        return None;
    }
    if data.len() < PROOF_SIZE {
        return None;
    }
    let payload_len = data.len().checked_sub(PROOF_SIZE)?;
    let mut reader = Reader::new(&data[..payload_len]);
    reader.skip(6)?;
    let player_id = reader.string()?;
    let session_id = reader.uuid()?;
    let nonce = reader.uuid()?;
    let _timestamp = reader.u64()?;
    let server_address = reader.string()?;
    let sender_name = reader.string()?;
    let sender_client_id = reader.i16()?;
    let scope = reader.u8()?;
    let message = reader.string()?;
    let skin_name = reader.string()?;
    let use_custom_color = reader.u8()?;
    let color_body = reader.i32()?;
    let color_feet = reader.i32()?;
    // Message id is optional so pre-read-receipt clients still parse.
    let message_id = if reader.remaining() >= 16 {
        reader.uuid()?
    } else {
        [0u8; 16]
    };
    if reader.remaining() != 0 {
        return None;
    }
    if skin_name.len() > 64 {
        return None;
    }
    Some(ChatPacket {
        player_id,
        session_id,
        nonce,
        server_address,
        sender_name,
        sender_client_id,
        scope,
        message,
        skin_name,
        use_custom_color,
        color_body,
        color_feet,
        message_id,
    })
}

struct ReadPacket {
    session_id: [u8; 16],
    nonce: [u8; 16],
    server_address: String,
    reader_name: String,
    reader_key: [u8; 16],
    message_id: [u8; 16],
}

fn read_read_packet(data: &[u8]) -> Option<ReadPacket> {
    if packet_type(data)? != PACKET_READ {
        return None;
    }
    if data.len() < PROOF_SIZE {
        return None;
    }
    let payload_len = data.len().checked_sub(PROOF_SIZE)?;
    let mut reader = Reader::new(&data[..payload_len]);
    reader.skip(6)?;
    let _player_id = reader.string()?;
    let session_id = reader.uuid()?;
    let nonce = reader.uuid()?;
    let _timestamp = reader.u64()?;
    let server_address = reader.string()?;
    let reader_name = reader.string()?;
    let reader_key = reader.uuid()?;
    let message_id = reader.uuid()?;
    if reader.remaining() != 0 {
        return None;
    }
    Some(ReadPacket {
        session_id,
        nonce,
        server_address,
        reader_name,
        reader_key,
        message_id,
    })
}

struct Reader<'a> {
    data: &'a [u8],
    offset: usize,
}

impl<'a> Reader<'a> {
    fn new(data: &'a [u8]) -> Self {
        Self { data, offset: 0 }
    }

    fn skip(&mut self, count: usize) -> Option<()> {
        self.offset = self.offset.checked_add(count)?;
        (self.offset <= self.data.len()).then_some(())
    }

    fn bytes(&mut self, count: usize) -> Option<&'a [u8]> {
        let end = self.offset.checked_add(count)?;
        if end > self.data.len() {
            return None;
        }
        let out = &self.data[self.offset..end];
        self.offset = end;
        Some(out)
    }

    fn uuid(&mut self) -> Option<[u8; 16]> {
        self.bytes(16)?.try_into().ok()
    }

    fn u8(&mut self) -> Option<u8> {
        Some(self.bytes(1)?[0])
    }

    fn u16(&mut self) -> Option<u16> {
        Some(u16::from_be_bytes(self.bytes(2)?.try_into().ok()?))
    }

    fn i16(&mut self) -> Option<i16> {
        Some(i16::from_be_bytes(self.bytes(2)?.try_into().ok()?))
    }

    fn i32(&mut self) -> Option<i32> {
        Some(i32::from_be_bytes(self.bytes(4)?.try_into().ok()?))
    }

    fn u64(&mut self) -> Option<u64> {
        Some(u64::from_be_bytes(self.bytes(8)?.try_into().ok()?))
    }

    fn string(&mut self) -> Option<String> {
        let len = self.u16()? as usize;
        let bytes = self.bytes(len)?;
        Some(String::from_utf8_lossy(bytes).into_owned())
    }

    fn remaining(&self) -> usize {
        self.data.len().saturating_sub(self.offset)
    }
}

fn validate_proof(shared_token: &str, data: &[u8]) -> bool {
    if data.len() < PROOF_SIZE {
        return false;
    }
    let payload_len = data.len() - PROOF_SIZE;
    let mut sha = Sha256::new();
    sha.update(shared_token.as_bytes());
    sha.update(&data[..payload_len]);
    let expected = sha.finalize();
    constant_time_eq(&expected, &data[payload_len..])
}

fn sign_body(secret: &str, body: &str) -> String {
    let mut mac = Hmac::<Sha256>::new_from_slice(secret.as_bytes()).expect("hmac key");
    mac.update(body.as_bytes());
    hex::encode(mac.finalize().into_bytes())
}

fn format_uuid(uuid: [u8; 16]) -> String {
    format!(
        "{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
        uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7],
        uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]
    )
}

fn session_id_for_packet(packet: &PresencePacket) -> String {
    format!("{}:{}", format_uuid(packet.session_id), packet.client_id)
}

/// The session id is formatted as `<instance-uuid>:<client_id>`. The instance
/// uuid identifies one running client, while the trailing client id changes per
/// game server. Strip the client id so we can match all entries that belong to
/// the same running client regardless of which server-assigned id they carry.
fn session_instance_of(session_id: &str) -> &str {
    match session_id.rsplit_once(':') {
        Some((instance, _client_id)) => instance,
        None => session_id,
    }
}

fn now_ms() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as u64
}

fn unix_timestamp() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs()
}

fn write_file_atomically(path: &Path, contents: &str) -> io::Result<()> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }
    let tmp = path.with_extension("tmp");
    fs::write(&tmp, contents)?;
    fs::rename(tmp, path)
}

async fn web_loop(
    state: Arc<Mutex<ServerState>>,
    config: Arc<Config>,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let healthz = warp::path("healthz")
        .and(warp::get())
        .map(|| warp::reply::json(&serde_json::json!({ "ok": true })));

    let presence_state_api = Arc::clone(&state);
    let api_presence = warp::path!("api" / "presence")
        .and(warp::get())
        .and_then(move || {
            let presence_state = Arc::clone(&presence_state_api);
            async move {
                let body = presence_state.lock().unwrap().snapshot_json();
                Ok::<_, Infallible>(warp::reply::with_header(
                    body,
                    "content-type",
                    "application/json; charset=utf-8",
                ))
            }
        });

    let presence_state_file = Arc::clone(&state);
    let presence_file = warp::path("presence.json")
        .and(warp::path::end())
        .and(warp::get())
        .and_then(move || {
            let presence_state = Arc::clone(&presence_state_file);
            async move {
                let body = presence_state.lock().unwrap().snapshot_json();
                Ok::<_, Infallible>(warp::reply::with_header(
                    body,
                    "content-type",
                    "application/json; charset=utf-8",
                ))
            }
        });

    let routes = healthz
        .or(api_presence)
        .or(presence_file)
        .with(warp::reply::with::header("cache-control", "no-store"))
        .with(warp::reply::with::header("connection", "close"));

    let listener = TcpListener::bind(config.web_bind).await?;
    let http_header_timeout = config.http_header_timeout;
    let https_connection_timeout = config.https_connection_timeout;

    let tls_acceptor = match (&config.tls_cert_file, &config.tls_key_file) {
        (Some(_), Some(_)) => Some(load_tls_acceptor(&config)?),
        _ => None,
    };

    if tls_acceptor.is_none() {
        eprintln!(
            "uclient presence web server listening on http://{} (plain HTTP, no TLS configured)",
            config.web_bind
        );
    }

    let tls_handshake_timeout = config.tls_handshake_timeout;

    loop {
        let (stream, peer_addr) = listener.accept().await?;
        let acceptor = tls_acceptor.clone();
        let routes = routes.clone();
        tokio::spawn(async move {
            let service = warp::service(routes);
            match acceptor {
                Some(acceptor) => {
                    let handshake =
                        tokio::time::timeout(tls_handshake_timeout, acceptor.accept(stream)).await;
                    let Ok(Ok(tls_stream)) = handshake else {
                        eprintln!("uclient presence HTTPS handshake failed from {peer_addr}");
                        return;
                    };
                    let connection = tokio::time::timeout(
                        https_connection_timeout,
                        Http::new()
                            .http1_header_read_timeout(http_header_timeout)
                            .serve_connection(tls_stream, service),
                    )
                    .await;
                    match connection {
                        Ok(Ok(())) => {}
                        Ok(Err(err)) => {
                            eprintln!("uclient presence HTTPS request failed from {peer_addr}: {err}")
                        }
                        Err(_) => {
                            eprintln!("uclient presence HTTPS request timeout from {peer_addr}")
                        }
                    }
                }
                None => {
                    let connection = tokio::time::timeout(
                        https_connection_timeout,
                        Http::new()
                            .http1_header_read_timeout(http_header_timeout)
                            .serve_connection(stream, service),
                    )
                    .await;
                    match connection {
                        Ok(Ok(())) => {}
                        Ok(Err(err)) => {
                            eprintln!("uclient presence HTTP request failed from {peer_addr}: {err}")
                        }
                        Err(_) => {
                            eprintln!("uclient presence HTTP request timeout from {peer_addr}")
                        }
                    }
                }
            }
        });
    }
}

fn load_tls_acceptor(
    config: &Config,
) -> Result<TlsAcceptor, Box<dyn std::error::Error + Send + Sync>> {
    let cert_path = config
        .tls_cert_file
        .as_ref()
        .ok_or("TLS_CERT_FILE is not configured")?;
    let key_path = config
        .tls_key_file
        .as_ref()
        .ok_or("TLS_KEY_FILE is not configured")?;

    let cert_file = fs::File::open(cert_path)?;
    let mut cert_reader = io::BufReader::new(cert_file);
    let certs = rustls_pemfile::certs(&mut cert_reader).collect::<Result<Vec<_>, _>>()?;
    if certs.is_empty() {
        return Err(format!("no certificates found in {}", cert_path.display()).into());
    }

    let key_file = fs::File::open(key_path)?;
    let mut key_reader = io::BufReader::new(key_file);
    let Some(key) = rustls_pemfile::private_key(&mut key_reader)? else {
        return Err(format!("no private key found in {}", key_path.display()).into());
    };

    let tls_config = ServerConfig::builder()
        .with_no_client_auth()
        .with_single_cert(certs, key)?;
    Ok(TlsAcceptor::from(Arc::new(tls_config)))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn write_presence_packet(packet_type: u8, secret: &str, from_server: Option<&str>) -> Vec<u8> {
        let mut out = Vec::new();
        out.extend_from_slice(&PROTOCOL_MAGIC);
        out.push(packet_type);
        out.push(PROTOCOL_VERSION_V2);
        write_string(&mut out, "player-1");
        out.extend_from_slice(&[1; 16]);
        out.extend_from_slice(&[2; 16]);
        out.extend_from_slice(&123u64.to_be_bytes());
        write_string(&mut out, "127.0.0.1:8303");
        write_string(&mut out, "dev");
        out.extend_from_slice(&4i16.to_be_bytes());
        write_string(&mut out, "2.4.0");
        if let Some(value) = from_server {
            write_string(&mut out, value);
        }
        let mut sha = Sha256::new();
        sha.update(secret.as_bytes());
        sha.update(&out);
        out.extend_from_slice(&sha.finalize());
        out
    }

    fn write_string(out: &mut Vec<u8>, value: &str) {
        let bytes = value.as_bytes();
        out.extend_from_slice(&(bytes.len() as u16).to_be_bytes());
        out.extend_from_slice(bytes);
    }

    #[test]
    fn parses_ucp1_presence_packet() {
        let packet = write_presence_packet(PACKET_JOIN, "shared", None);
        let parsed = read_presence_packet(&packet).unwrap();
        assert_eq!(parsed.packet_type, PACKET_JOIN);
        assert_eq!(parsed.client_id, 4);
        assert_eq!(parsed.client_version, "2.4.0");
        assert!(validate_proof("shared", &packet));
    }

    #[test]
    fn parses_v1_presence_packet_without_version() {
        let mut out = Vec::new();
        out.extend_from_slice(&PROTOCOL_MAGIC);
        out.push(PACKET_JOIN);
        out.push(PROTOCOL_VERSION_V1);
        write_string(&mut out, "player-1");
        out.extend_from_slice(&[1; 16]);
        out.extend_from_slice(&[2; 16]);
        out.extend_from_slice(&123u64.to_be_bytes());
        write_string(&mut out, "127.0.0.1:8303");
        write_string(&mut out, "dev");
        out.extend_from_slice(&4i16.to_be_bytes());
        let mut sha = Sha256::new();
        sha.update("shared".as_bytes());
        sha.update(&out);
        out.extend_from_slice(&sha.finalize());
        let parsed = read_presence_packet(&out).unwrap();
        assert_eq!(parsed.client_version, "");
    }

    #[test]
    fn session_ids_differ_by_client_id() {
        let mut packet_a = read_presence_packet(&write_presence_packet(PACKET_JOIN, "shared", None)).unwrap();
        packet_a.client_id = 1;
        let mut packet_b = packet_a.clone();
        packet_b.client_id = 2;
        assert_ne!(session_id_for_packet(&packet_a), session_id_for_packet(&packet_b));
    }

    #[test]
    fn parses_switch_with_from_server() {
        let packet = write_presence_packet(PACKET_SWITCH, "shared", Some("127.0.0.1:8302"));
        let parsed = read_presence_packet(&packet).unwrap();
        assert_eq!(parsed.packet_type, PACKET_SWITCH);
        assert_eq!(parsed.from_server_address.as_deref(), Some("127.0.0.1:8302"));
    }
}
