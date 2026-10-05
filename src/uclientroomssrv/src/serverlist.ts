const MASTER_URLS = [
	"https://master1.ddnet.org/ddnet/15/servers.json",
	"https://master2.ddnet.org/ddnet/15/servers.json",
] as const;
const PRESENCE_URL = "https://ddnet.under1111.com/api/presence";
const CACHE_MS = 20_000;
const HEADERS = {
	accept: "application/json",
	"user-agent": "UClient-Discord/1.0",
} as const;

type CacheRow = {at: number; data: unknown};
let serverCache: CacheRow | null = null;
let presenceCache: CacheRow | null = null;

export type ServerHit = {
	address: string;
	count: number;
	max: number;
	password: boolean;
};

export type FriendGroup = {
	address: string;
	count: number | null;
	max: number | null;
	names: string[];
};

type IndexedServer = ServerHit & {norm: string};

const PROTOCOL_RE = /^(?:tw-0\.[67]\+(?:udp|tcp):\/\/|ddnet:\/\/|ddnet:)/i;

export function stripAddress(raw: string): string {
	return raw.replace(PROTOCOL_RE, "").replace(/\/+$/, "").trim();
}

export function normalizeAddress(raw: string): string {
	return stripAddress(raw).toLowerCase();
}

export function normalizeName(name: string): string {
	return name
		.replace(/\u0019./g, "")
		.replace(/\|[0-9A-Fa-f]{6}\|/g, "")
		.replace(/[\u0000-\u001f]/g, "")
		.trim()
		.toLowerCase();
}

function serversFrom(data: unknown): Record<string, unknown>[] {
	const raw = Array.isArray(data)
		? data
		: data && typeof data === "object" && Array.isArray((data as {servers?: unknown}).servers)
			? (data as {servers: unknown[]}).servers
			: [];
	return raw.filter((item): item is Record<string, unknown> => !!item && typeof item === "object");
}

function serverHit(server: Record<string, unknown>): IndexedServer | null {
	const addresses = Array.isArray(server.addresses) ? server.addresses : [];
	const rawAddress = addresses.find((item): item is string => typeof item === "string" && stripAddress(item).length > 0);
	if(!rawAddress)
		return null;
	const info = server.info && typeof server.info === "object" ? server.info as Record<string, unknown> : {};
	const clients = Array.isArray(info.clients) ? info.clients : [];
	const maxClients = typeof info.max_clients === "number" ? info.max_clients : typeof info.max_players === "number" ? info.max_players : clients.length;
	return {
		address: stripAddress(rawAddress),
		norm: normalizeAddress(rawAddress),
		count: clients.length,
		max: maxClients,
		password: info.passworded === true,
	};
}

function clientNames(server: Record<string, unknown>): string[] {
	const info = server.info && typeof server.info === "object" ? server.info as Record<string, unknown> : {};
	const clients = Array.isArray(info.clients) ? info.clients : [];
	const names: string[] = [];
	for(const client of clients) {
		if(!client || typeof client !== "object")
			continue;
		const name = (client as {name?: unknown}).name;
		if(typeof name === "string" && normalizeName(name))
			names.push(name);
	}
	return names;
}

export function findPlayerServers(data: unknown, query: string): ServerHit[] {
	const needle = normalizeName(query);
	if(!needle)
		return [];
	const hits: ServerHit[] = [];
	for(const server of serversFrom(data)) {
		if(!clientNames(server).some(name => normalizeName(name) === needle))
			continue;
		const hit = serverHit(server);
		if(hit)
			hits.push({address: hit.address, count: hit.count, max: hit.max, password: hit.password});
	}
	return hits;
}

function presenceLocations(presence: unknown): Map<string, {address: string; seen: number}> {
	const found = new Map<string, {address: string; seen: number}>();
	const blocks = Array.isArray(presence) ? presence : presence && typeof presence === "object" ? [presence] : [];
	for(const block of blocks) {
		if(!block || typeof block !== "object")
			continue;
		for(const [rawAddress, body] of Object.entries(block as Record<string, unknown>)) {
			if(!body || typeof body !== "object")
				continue;
			const players = (body as {players?: unknown}).players;
			if(!Array.isArray(players))
				continue;
			for(const player of players) {
				if(!player || typeof player !== "object")
					continue;
				const row = player as {name?: unknown; last_seen?: unknown};
				const key = typeof row.name === "string" ? normalizeName(row.name) : "";
				if(!key)
					continue;
				const seen = typeof row.last_seen === "number" ? row.last_seen : 0;
				const previous = found.get(key);
				if(!previous || seen >= previous.seen)
					found.set(key, {address: stripAddress(rawAddress), seen});
			}
		}
	}
	return found;
}

export function groupOnlineFriends(servers: unknown, presence: unknown, friendNames: string[]): FriendGroup[] {
	const byAddress = new Map<string, IndexedServer>();
	const nameServers = new Map<string, string[]>();
	for(const server of serversFrom(servers)) {
		const hit = serverHit(server);
		if(!hit || byAddress.has(hit.norm))
			continue;
		byAddress.set(hit.norm, hit);
		for(const name of clientNames(server)) {
			const key = normalizeName(name);
			const list = nameServers.get(key) ?? [];
			if(!list.includes(hit.norm))
				list.push(hit.norm);
			nameServers.set(key, list);
		}
	}
	const presenceHits = presenceLocations(presence);
	const groups = new Map<string, FriendGroup>();
	const seenFriends = new Set<string>();
	for(const rawName of friendNames) {
		const key = normalizeName(rawName);
		if(!key || seenFriends.has(key))
			continue;
		seenFriends.add(key);
		const presenceHit = presenceHits.get(key);
		const addresses = presenceHit ? [presenceHit.address] : (nameServers.get(key) ?? []).map(norm => byAddress.get(norm)?.address ?? norm);
		for(const address of addresses) {
			const norm = normalizeAddress(address);
			const server = byAddress.get(norm);
			let group = groups.get(norm);
			if(!group) {
				group = {
					address: server?.address ?? stripAddress(address),
					count: server ? server.count : null,
					max: server ? server.max : null,
					names: [],
				};
				groups.set(norm, group);
			}
			group.names.push(rawName.trim());
		}
	}
	return [...groups.values()]
		.map(group => ({...group, names: [...group.names].sort((left, right) => left < right ? -1 : left > right ? 1 : 0)}))
		.sort((left, right) => left.address < right.address ? -1 : left.address > right.address ? 1 : 0);
}

export function formatPlayerLines(hits: ServerHit[]): string {
	return hits
		.slice(0, 25)
		.map(hit => `${hit.address} (${hit.count}/${hit.max})${hit.password ? " password" : ""}`)
		.join("\n");
}

export function formatFriendGroups(groups: FriendGroup[]): string {
	const parts: string[] = [];
	let length = 0;
	for(const group of groups) {
		const head = group.count == null || group.max == null ? group.address : `${group.address} (${group.count}/${group.max})`;
		const block = `${head}\n${group.names.join(", ")}`;
		if(length + block.length + 2 > 3900) {
			parts.push("...");
			break;
		}
		parts.push(block);
		length += block.length + 2;
	}
	return parts.join("\n\n");
}

async function fetchJson(url: string): Promise<unknown> {
	const response = await fetch(url, {headers: HEADERS, signal: AbortSignal.timeout(10000)});
	if(!response.ok)
		throw new Error(`http_${response.status}`);
	return response.json();
}

export async function loadPublicServers(): Promise<unknown> {
	if(serverCache && Date.now() - serverCache.at < CACHE_MS)
		return serverCache.data;
	let lastError: unknown;
	for(const url of MASTER_URLS) {
		try {
			const data = await fetchJson(url);
			serverCache = {at: Date.now(), data};
			return data;
		}
		catch(errorValue) {
			lastError = errorValue;
		}
	}
	throw lastError instanceof Error ? lastError : new Error("master_unreachable");
}

export async function loadPresence(): Promise<unknown> {
	if(presenceCache && Date.now() - presenceCache.at < CACHE_MS)
		return presenceCache.data;
	const data = await fetchJson(PRESENCE_URL);
	presenceCache = {at: Date.now(), data};
	return data;
}
