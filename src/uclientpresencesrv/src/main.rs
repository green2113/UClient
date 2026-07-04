use constant_time_eq::constant_time_eq;
use hmac::{Hmac, Mac};
use serde::Serialize;
use sha2::{Digest, Sha256};
use std::collections::HashMap;
use std::env;
use std::fs;
use std::io;
use std::net::{IpAddr, SocketAddr};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};
use tokio::net::UdpSocket;
use tokio::time;

const PROTOCOL_MAGIC: [u8; 4] = [0x55, 0x43, 0x50, 0x31]; // UCP1
const PROTOCOL_VERSION_V1: u8 = 1;
const PROTOCOL_VERSION_V2: u8 = 2;
const DEFAULT_UDP_BIND: &str = "0.0.0.0:8778";
const DEFAULT_SYNC_URL: &str = "https://ddnet.under1111.com/api/presence/sync";
const MAX_UDP_PACKET_SIZE: usize = 2048;
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

#[derive(Clone)]
struct Config {
    udp_bind: SocketAddr,
    shared_token: String,
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

struct UdpOutcome {
    sync_jobs: Vec<SyncJob>,
    outbound: Vec<(SocketAddr, Vec<u8>)>,
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
    invalid_rate_by_ip: HashMap<IpAddr, Instant>,
}

impl ServerState {
    fn new() -> Self {
        Self {
            entries: HashMap::new(),
            recent_nonces: HashMap::new(),
            last_heartbeat_sync: HashMap::new(),
            invalid_rate_by_ip: HashMap::new(),
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
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let _ = dotenvy::from_filename("src/uclientpresencesrv/.env");
    let _ = dotenvy::dotenv();
    let config = Arc::new(Config::load()?);
    let state = Arc::new(Mutex::new(ServerState::new()));
    let client = reqwest::Client::builder()
        .timeout(Duration::from_secs(10))
        .build()?;

    let udp_socket = Arc::new(UdpSocket::bind(config.udp_bind).await?);
    eprintln!("uclient presence UDP listening on {}", config.udp_bind);
    eprintln!("uclient presence sync target {}", config.sync_url);

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
        client,
    ));

    tokio::select! {
        result = udp_task => result??,
        result = cleanup_task => result??,
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

        let sync_secret = env::var("PRESENCE_UDP_SYNC_SECRET")
            .unwrap_or_default()
            .trim()
            .to_string();
        if sync_secret.is_empty() {
            return Err("PRESENCE_UDP_SYNC_SECRET must be set".into());
        }

        let sync_url = env::var("PRESENCE_SYNC_URL")
            .unwrap_or_else(|_| DEFAULT_SYNC_URL.to_string())
            .trim()
            .to_string();
        let udp_bind = env::var("UDP_BIND")
            .unwrap_or_else(|_| DEFAULT_UDP_BIND.to_string())
            .parse()?;
        let heartbeat_sync_debounce = env::var("HEARTBEAT_SYNC_DEBOUNCE_SEC")
            .ok()
            .and_then(|value| value.parse::<u64>().ok())
            .map(Duration::from_secs)
            .unwrap_or(DEFAULT_HEARTBEAT_SYNC_DEBOUNCE);

        Ok(Self {
            udp_bind,
            shared_token,
            sync_url,
            sync_secret,
            heartbeat_sync_debounce,
        })
    }
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
            handle_udp_packet(
                &mut state,
                &config.shared_token,
                from.ip(),
                from,
                data,
                config.heartbeat_sync_debounce,
            )
        };
        for job in outcome.sync_jobs {
            if let Err(err) = post_sync(&client, &config, job).await {
                eprintln!("presence sync failed: {err}");
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
    _config: Arc<Config>,
    _client: reqwest::Client,
) -> io::Result<()> {
    let mut interval = time::interval(CLEANUP_INTERVAL);
    loop {
        interval.tick().await;
        let outbound = {
            let mut state = state.lock().unwrap();
            let removed = state.cleanup(Instant::now());
            let removed_count = removed.len();
            let mut packets = Vec::new();
            for entry in removed {
                append_peer_remove_notifications(&state, &entry, &mut packets);
            }
            (removed_count, packets)
        };
        if outbound.0 > 0 {
            eprintln!(
                "presence cleanup evicted {} stale udp entries (kv left unchanged)",
                outbound.0
            );
        }
        for (addr, packet) in outbound.1 {
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
) -> UdpOutcome {
    let mut outcome = UdpOutcome {
        sync_jobs: Vec::new(),
        outbound: Vec::new(),
    };
    let now = Instant::now();
    if data.len() > MAX_UDP_PACKET_SIZE {
        return outcome;
    }
    let Some(packet_type) = packet_type(data) else {
        state.log_invalid(ip, now, "bad header");
        return outcome;
    };
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
    if job.force {
        outcome.sync_jobs.push(job);
    }

    match packet.packet_type {
        PACKET_LEAVE => {
            if let Some(entry) = state.remove_entry(&key) {
                append_peer_remove_notifications(state, &entry, &mut outcome.outbound);
            }
        }
        PACKET_SWITCH => {
            if let Some(prev) = previous.as_ref() {
                if prev.server_address != packet.server_address {
                    append_peer_remove_notifications(state, prev, &mut outcome.outbound);
                }
            }
            if let Some(entry) = state.upsert_entry(&packet, from, now, &key) {
                append_join_notifications(state, &entry, from, &key, &mut outcome.outbound);
            }
        }
        PACKET_JOIN | PACKET_HEARTBEAT => {
            let was_new = previous.is_none();
            if let Some(entry) = state.upsert_entry(&packet, from, now, &key) {
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

    fn u16(&mut self) -> Option<u16> {
        Some(u16::from_be_bytes(self.bytes(2)?.try_into().ok()?))
    }

    fn i16(&mut self) -> Option<i16> {
        Some(i16::from_be_bytes(self.bytes(2)?.try_into().ok()?))
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

fn now_ms() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as u64
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
