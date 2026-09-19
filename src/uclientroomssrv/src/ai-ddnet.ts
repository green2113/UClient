export const DDNET_LOOKUP_TYPES = ["player", "map", "releases", "wiki", "mapper"] as const;

export type DdnetLookupType = (typeof DDNET_LOOKUP_TYPES)[number];

export type DdnetLookup = {
	type: DdnetLookupType;
	query: string;
};

const MAX_LOOKUPS = 2;
const MAX_QUERY = 64;
const FETCH_MS = 6000;
const RELEASES_TTL_MS = 10 * 60 * 1000;
const HEADERS = {
	accept: "application/json",
	"user-agent": "UClient-Assistant/1.0 (+https://uclient)",
} as const;

type CacheRow = {at: number; text: string};
let releasesCache: CacheRow | null = null;

function isLookupType(value: string): value is DdnetLookupType {
	return (DDNET_LOOKUP_TYPES as readonly string[]).includes(value);
}

export function parseDdnetLookups(value: unknown): DdnetLookup[] {
	if(!Array.isArray(value))
		return [];
	const out: DdnetLookup[] = [];
	const seen = new Set<string>();
	for(const item of value) {
		if(!item || typeof item !== "object")
			continue;
		const row = item as Record<string, unknown>;
		const type = String(row.type ?? "").trim().toLowerCase();
		if(!isLookupType(type))
			continue;
		const query = typeof row.query === "string" ? row.query.trim().slice(0, MAX_QUERY) : "";
		if(type !== "releases" && !query)
			continue;
		const key = `${type}:${query.toLowerCase()}`;
		if(seen.has(key))
			continue;
		seen.add(key);
		out.push({type, query});
		if(out.length >= MAX_LOOKUPS)
			break;
	}
	return out;
}

export function fallbackDdnetLookups(query: string): DdnetLookup[] {
	const text = query.trim();
	if(!text)
		return [];
	if(/릴리스|새로 나온 맵|최근 맵|map releases|recent maps/i.test(text))
		return [{type: "releases", query: ""}];
	if(/위키|wiki/i.test(text)) {
		const topic = text.replace(/위키|wiki|디디넷|디디|ddnet/gi, "").replace(/\s+/g, " ").trim().slice(0, MAX_QUERY);
		return topic ? [{type: "wiki", query: topic}] : [];
	}
	return [];
}

function fmtDate(sec: unknown): string {
	const n = typeof sec === "number" ? sec : Number(sec);
	if(!Number.isFinite(n) || n <= 0)
		return "";
	const ms = n > 1e12 ? n : n * 1000;
	return new Date(ms).toISOString().slice(0, 10);
}

function fmtRace(sec: unknown): string {
	const n = typeof sec === "number" ? sec : Number(sec);
	if(!Number.isFinite(n) || n < 0)
		return "";
	const m = Math.floor(n / 60);
	const r = n - m * 60;
	return m > 0 ? `${m}:${r.toFixed(2).padStart(5, "0")}` : r.toFixed(2);
}

function rankLine(label: string, value: unknown): string {
	if(!value || typeof value !== "object")
		return "";
	const row = value as Record<string, unknown>;
	if(row.rank == null)
		return `${label}: unranked`;
	const bits = [`${label}: rank ${row.rank}`];
	if(typeof row.points === "number")
		bits.push(`${row.points} points`);
	if(typeof row.total === "number")
		bits.push(`of ${row.total}`);
	return bits.join(", ");
}

function playerUrl(name: string): string {
	return `https://ddnet.org/players/${encodeURIComponent(name)}/`;
}

function mapUrl(name: string): string {
	return `https://ddnet.org/maps/${encodeURIComponent(name)}`;
}

export function summarizePlayer(data: unknown, query: string): string {
	if(!data || typeof data !== "object" || Array.isArray(data))
		return `No official DDNet player named "${query}".`;
	const row = data as Record<string, unknown>;
	const name = typeof row.player === "string" && row.player ? row.player : query;
	if(!row.points && !row.rank && !Array.isArray(row.last_finishes))
		return `No official DDNet player named "${query}".`;
	const lines = [`Player: ${name}`];
	const points = rankLine("Points", row.points);
	if(points)
		lines.push(points);
	const rank = rankLine("Rank", row.rank);
	if(rank)
		lines.push(rank);
	const team = rankLine("Team rank", row.team_rank);
	if(team)
		lines.push(team);
	const month = rankLine("Last month", row.points_last_month);
	if(month)
		lines.push(month);
	const week = rankLine("Last week", row.points_last_week);
	if(week)
		lines.push(week);
	const fav = row.favorite_server;
	if(fav && typeof fav === "object" && typeof (fav as {server?: unknown}).server === "string")
		lines.push(`Favorite server: ${(fav as {server: string}).server}`);
	const first = row.first_finish;
	if(first && typeof first === "object") {
		const fin = first as Record<string, unknown>;
		const map = typeof fin.map === "string" ? fin.map : "";
		const when = fmtDate(fin.timestamp);
		if(map)
			lines.push(`First finish: ${map}${when ? ` (${when})` : ""}`);
	}
	if(Array.isArray(row.last_finishes) && row.last_finishes.length) {
		const recent = row.last_finishes.slice(0, 5).map((item) => {
			if(!item || typeof item !== "object")
				return "";
			const fin = item as Record<string, unknown>;
			const map = typeof fin.map === "string" ? fin.map : "?";
			const type = typeof fin.type === "string" ? fin.type : "";
			const when = fmtDate(fin.timestamp);
			const time = fmtRace(fin.time);
			return `- ${map}${type ? ` [${type}]` : ""}${time ? ` ${time}` : ""}${when ? ` ${when}` : ""}`;
		}).filter(Boolean);
		if(recent.length)
			lines.push("Recent finishes:", ...recent);
	}
	lines.push(`More: ${playerUrl(name)}`);
	return lines.join("\n");
}

export function summarizePlayerMatches(data: unknown, query: string): string {
	if(!Array.isArray(data) || !data.length)
		return `No official DDNet player named "${query}".`;
	const rows = data.slice(0, 8).map((item) => {
		if(!item || typeof item !== "object")
			return "";
		const row = item as Record<string, unknown>;
		const name = typeof row.name === "string" ? row.name : "";
		if(!name)
			return "";
		const points = typeof row.points === "number" ? ` (${row.points} points)` : "";
		return `- ${name}${points} ${playerUrl(name)}`;
	}).filter(Boolean);
	if(!rows.length)
		return `No official DDNet player named "${query}".`;
	return [`Player name matches for "${query}":`, ...rows].join("\n");
}

export function summarizeMap(data: unknown, query: string): string {
	if(!data || typeof data !== "object" || Array.isArray(data))
		return `No official DDNet map named "${query}".`;
	const row = data as Record<string, unknown>;
	const name = typeof row.name === "string" && row.name ? row.name : query;
	if(!name || (row.mapper == null && row.type == null && row.points == null))
		return `No official DDNet map named "${query}".`;
	const lines = [`Map: ${name}`];
	const meta = [
		typeof row.type === "string" ? row.type : "",
		typeof row.mapper === "string" && row.mapper ? `mapper ${row.mapper}` : "",
		typeof row.points === "number" ? `${row.points} points` : "",
		typeof row.difficulty === "number" ? `difficulty ${row.difficulty}` : "",
	].filter(Boolean);
	if(meta.length)
		lines.push(meta.join(" · "));
	const released = fmtDate(row.release);
	if(released)
		lines.push(`Released: ${released}`);
	if(typeof row.finishes === "number" || typeof row.finishers === "number")
		lines.push(`Finishes: ${row.finishes ?? "?"} by ${row.finishers ?? "?"} players`);
	if(Array.isArray(row.ranks) && row.ranks.length) {
		const top = row.ranks.slice(0, 5).map((item) => {
			if(!item || typeof item !== "object")
				return "";
			const rank = item as Record<string, unknown>;
			const player = typeof rank.player === "string" ? rank.player : "?";
			const time = fmtRace(rank.time);
			return `- #${rank.rank ?? "?"} ${player}${time ? ` ${time}` : ""}`;
		}).filter(Boolean);
		if(top.length)
			lines.push("Top ranks:", ...top);
	}
	lines.push(`More: ${typeof row.website === "string" && row.website ? row.website : mapUrl(name)}`);
	return lines.join("\n");
}

export function summarizeReleases(data: unknown, mapper = ""): string {
	if(!Array.isArray(data) || !data.length)
		return mapper ? `No official DDNet releases found for mapper "${mapper}".` : "No official DDNet map releases found.";
	const needle = mapper.trim().toLowerCase();
	const rows = data
		.filter((item): item is Record<string, unknown> => !!item && typeof item === "object")
		.filter((item) => {
			if(!needle)
				return true;
			const name = typeof item.mapper === "string" ? item.mapper.toLowerCase() : "";
			return name.includes(needle);
		})
		.slice()
		.sort((a, b) => {
			const ta = typeof a.release === "string" ? Date.parse(a.release) : Number(a.release) || 0;
			const tb = typeof b.release === "string" ? Date.parse(b.release) : Number(b.release) || 0;
			return tb - ta;
		})
		.slice(0, needle ? 12 : 8);
	if(!rows.length)
		return mapper ? `No official DDNet releases found for mapper "${mapper}".` : "No official DDNet map releases found.";
	const title = needle ? `Maps by mapper matching "${mapper}":` : "Recent official DDNet map releases:";
	const lines = rows.map((item) => {
		const name = typeof item.name === "string" ? item.name : "?";
		const who = typeof item.mapper === "string" && item.mapper ? item.mapper : "unknown mapper";
		const type = typeof item.type === "string" ? item.type : "";
		const points = typeof item.points === "number" ? `${item.points} pts` : "";
		const when = typeof item.release === "string" ? item.release.slice(0, 10) : fmtDate(item.release);
		return `- ${name} by ${who}${type ? ` [${type}]` : ""}${points ? ` ${points}` : ""}${when ? ` ${when}` : ""} ${mapUrl(name)}`;
	});
	return [title, ...lines].join("\n");
}

export function summarizeWikiSearch(data: unknown, query: string): string {
	if(!data || typeof data !== "object")
		return `No DDNet wiki result for "${query}".`;
	const queryObj = (data as {query?: {search?: unknown}}).query;
	const hits = Array.isArray(queryObj?.search) ? queryObj.search : [];
	if(!hits.length)
		return `No DDNet wiki result for "${query}".`;
	const lines = [`DDNet wiki search for "${query}":`];
	for(const item of hits.slice(0, 3)) {
		if(!item || typeof item !== "object")
			continue;
		const row = item as Record<string, unknown>;
		const title = typeof row.title === "string" ? row.title : "";
		if(!title)
			continue;
		const snippet = typeof row.snippet === "string"
			? row.snippet.replace(/<[^>]+>/g, "").replace(/&quot;/g, "\"").replace(/&amp;/g, "&").slice(0, 220)
			: "";
		lines.push(`- ${title}: https://wiki.ddnet.org/wiki/${encodeURIComponent(title.replace(/ /g, "_"))}`);
		if(snippet)
			lines.push(`  ${snippet}`);
	}
	return lines.length > 1 ? lines.join("\n") : `No DDNet wiki result for "${query}".`;
}

export function summarizeWikiExtract(data: unknown, title: string): string {
	if(!data || typeof data !== "object")
		return "";
	const pages = (data as {query?: {pages?: Record<string, unknown>}}).query?.pages;
	if(!pages)
		return "";
	for(const page of Object.values(pages)) {
		if(!page || typeof page !== "object")
			continue;
		const row = page as Record<string, unknown>;
		const extract = typeof row.extract === "string" ? row.extract.replace(/\s+/g, " ").trim().slice(0, 700) : "";
		const name = typeof row.title === "string" ? row.title : title;
		if(!extract)
			continue;
		return `Wiki ${name}: ${extract}\nhttps://wiki.ddnet.org/wiki/${encodeURIComponent(name.replace(/ /g, "_"))}`;
	}
	return "";
}

async function fetchJson(url: string): Promise<unknown> {
	const upstream = await fetch(url, {
		headers: HEADERS,
		signal: AbortSignal.timeout(FETCH_MS),
	});
	if(!upstream.ok)
		throw new Error(`http_${upstream.status}`);
	return upstream.json();
}

async function fetchReleases(): Promise<unknown> {
	if(releasesCache && Date.now() - releasesCache.at < RELEASES_TTL_MS) {
		try {
			return JSON.parse(releasesCache.text);
		}
		catch {
			releasesCache = null;
		}
	}
	const data = await fetchJson("https://ddnet.org/releases/maps.json");
	try {
		releasesCache = {at: Date.now(), text: JSON.stringify(data)};
	}
	catch {
		releasesCache = null;
	}
	return data;
}

async function lookupPlayer(query: string): Promise<string> {
	try {
		const exact = await fetchJson(`https://ddnet.org/players/?json2=${encodeURIComponent(query)}`);
		const summary = summarizePlayer(exact, query);
		if(!summary.startsWith("No official"))
			return summary;
	}
	catch {
		// try prefix search next
	}
	try {
		const matches = await fetchJson(`https://ddnet.org/players/?query=${encodeURIComponent(query)}`);
		return summarizePlayerMatches(matches, query);
	}
	catch(errorValue) {
		return `Could not reach ddnet.org player lookup for "${query}". ${errorValue instanceof Error ? errorValue.message : ""}`.trim();
	}
}

async function lookupMap(query: string): Promise<string> {
	try {
		const data = await fetchJson(`https://ddnet.org/maps/?json=${encodeURIComponent(query)}`);
		return summarizeMap(data, query);
	}
	catch(errorValue) {
		return `Could not reach ddnet.org map lookup for "${query}". ${errorValue instanceof Error ? errorValue.message : ""}`.trim();
	}
}

async function lookupWiki(query: string): Promise<string> {
	try {
		const search = await fetchJson(`https://wiki.ddnet.org/w/api.php?action=query&list=search&srsearch=${encodeURIComponent(query)}&srlimit=3&format=json`);
		const listing = summarizeWikiSearch(search, query);
		const first = (search as {query?: {search?: Array<{title?: string}>}}).query?.search?.[0]?.title;
		if(!first)
			return listing;
		try {
			const extract = await fetchJson(`https://wiki.ddnet.org/w/api.php?action=query&prop=extracts&exintro=1&explaintext=1&exchars=700&titles=${encodeURIComponent(first)}&format=json`);
			const body = summarizeWikiExtract(extract, first);
			return body ? `${listing}\n\n${body}` : listing;
		}
		catch {
			return listing;
		}
	}
	catch(errorValue) {
		return `Could not reach the DDNet wiki for "${query}". ${errorValue instanceof Error ? errorValue.message : ""}`.trim();
	}
}

async function runLookup(item: DdnetLookup): Promise<string> {
	if(item.type === "player")
		return lookupPlayer(item.query);
	if(item.type === "map")
		return lookupMap(item.query);
	if(item.type === "wiki")
		return lookupWiki(item.query);
	try {
		const releases = await fetchReleases();
		return summarizeReleases(releases, item.type === "mapper" ? item.query : "");
	}
	catch(errorValue) {
		return `Could not reach ddnet.org map releases. ${errorValue instanceof Error ? errorValue.message : ""}`.trim();
	}
}

export async function fetchDdnetLookups(lookups: DdnetLookup[]): Promise<string> {
	const items = lookups.slice(0, MAX_LOOKUPS);
	if(!items.length)
		return "";
	const parts = await Promise.all(items.map(async (item) => {
		try {
			return await runLookup(item);
		}
		catch(errorValue) {
			return `Official DDNet lookup failed (${item.type}${item.query ? ` ${item.query}` : ""}). ${errorValue instanceof Error ? errorValue.message : ""}`.trim();
		}
	}));
	return [
		"Official DDNet lookup (ddnet.org / wiki.ddnet.org). Use only this for ranks, maps, mappers, releases, and wiki facts. Do not invent missing ranks.",
		...parts.filter(Boolean),
	].join("\n\n");
}
