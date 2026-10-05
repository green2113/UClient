const JSON_HEADERS = {
	"content-type": "application/json; charset=utf-8",
	"cache-control": "no-store",
} as const;

const SNOWFLAKE_RE = /^\d{5,22}$/;
const TOKEN_RE = /^[A-Za-z0-9_-]{20,128}$/;
const LINK_TTL_SECONDS = 10 * 60;
const GAME_ONLINE_SECONDS = 8;
const MAX_INGEST_LINES = 20;
const MAX_DISCORD_CONTENT = 2000;
const MAX_GAME_CHAT = 255;
const MAX_TOPIC = 1024;
const EPHEMERAL_FLAG = 64;

export const DISCORD_TEXT = {
	notLoggedIn: "The launcher is not signed in. Sign in, then try again.",
	noEmail: "This account does not have an email and password. In the launcher, open Settings > Account and create an email and password.",
	notLinked: "Your UClient account is not linked. Use /link first.",
	noChannel: "There is no message channel.",
	channelExists: "A message channel already exists. Delete it with /delete-message-channel first.",
	notInGame: "You are not connected to a game server.",
	noRoom: "That room is not available.",
	modeRoomConflict: "Clear the mode, or set it to UClient, when a room is selected.",
} as const;

export function linkedMessage(installId: string): string {
	return `${installId} is now linked to your UClient account.`;
}

export interface DiscordEnv {
	DB: D1Database;
	DISCORD_BOT_TOKEN?: string;
	DISCORD_INTERNAL_SECRET?: string;
}

export interface DiscordAuth {
	installId: string;
}

interface ChallengeRow {
	token: string;
	discord_user_id: string;
	application_id: string;
	interaction_token: string;
	guild_id: string;
	expires_at: number;
	consumed: number;
}

interface LinkRow {
	discord_user_id: string;
	install_id: string;
	game_online_at: number;
}

interface ChannelRow {
	install_id: string;
	discord_user_id: string;
	guild_id: string;
	channel_id: string;
}

function json(body: unknown, status = 200): Response {
	return new Response(JSON.stringify(body), {status, headers: JSON_HEADERS});
}

function error(status: number, code: string, message: string): Response {
	return json({error: code, message}, status);
}

function nowSeconds(): number {
	return Math.floor(Date.now() / 1000);
}

function validText(value: unknown, min: number, max: number): value is string {
	return typeof value === "string" && value.trim().length >= min && value.trim().length <= max;
}

async function readJson<T>(request: Request): Promise<T | null> {
	try {
		return await request.json<T>();
	}
	catch {
		return null;
	}
}

function timingSafeEqual(left: string, right: string): boolean {
	const leftBytes = new TextEncoder().encode(left);
	const rightBytes = new TextEncoder().encode(right);
	if(leftBytes.byteLength !== rightBytes.byteLength)
		return false;
	let different = 0;
	for(let index = 0; index < leftBytes.byteLength; index++)
		different |= leftBytes[index]! ^ rightBytes[index]!;
	return different === 0;
}

async function internalAuthorized(request: Request, env: DiscordEnv): Promise<boolean> {
	const expected = env.DISCORD_INTERNAL_SECRET ?? "";
	if(!expected)
		return false;
	const authorization = request.headers.get("authorization") ?? "";
	const provided = authorization.startsWith("Bearer ") ? authorization.slice(7) : "";
	if(!provided)
		return false;
	const encoder = new TextEncoder();
	const [providedHash, expectedHash] = await Promise.all([
		crypto.subtle.digest("SHA-256", encoder.encode(provided)),
		crypto.subtle.digest("SHA-256", encoder.encode(expected)),
	]);
	const providedHex = [...new Uint8Array(providedHash)].map(value => value.toString(16).padStart(2, "0")).join("");
	const expectedHex = [...new Uint8Array(expectedHash)].map(value => value.toString(16).padStart(2, "0")).join("");
	return timingSafeEqual(providedHex, expectedHex);
}

function base64Url(bytes: Uint8Array): string {
	let binary = "";
	for(const value of bytes)
		binary += String.fromCharCode(value);
	return btoa(binary).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/g, "");
}

export function neutralizeMentions(text: string): string {
	return text.replace(/@(everyone|here)/gi, "@\u200b$1");
}

export function discordChatPayload(line: string): {content: string; allowed_mentions: {parse: []}} {
	return {
		content: neutralizeMentions(line).slice(0, MAX_DISCORD_CONTENT),
		allowed_mentions: {parse: []},
	};
}

interface DiscordHttpResult {
	ok: boolean;
	retryAfterSeconds: number;
	resetAfterSeconds: number;
	remaining: number | null;
}

function retryAfterSeconds(response: Response, body: {retry_after?: number} | null): number {
	const header = Number(response.headers.get("retry-after"));
	if(Number.isFinite(header) && header >= 0)
		return Math.max(1, Math.ceil(header));
	const value = body?.retry_after;
	if(typeof value !== "number" || !Number.isFinite(value) || value < 0)
		return 30;
	const seconds = value > 3600 ? value / 1000 : value;
	return Math.max(1, Math.ceil(seconds));
}

async function discordFetch(url: string, token: string, body: unknown, method = "POST"): Promise<DiscordHttpResult> {
	try {
		const response = await fetch(url, {
			method,
			headers: {
				authorization: `Bot ${token}`,
				"content-type": "application/json",
			},
			body: JSON.stringify(body),
			signal: AbortSignal.timeout(8000),
		});
		if(response.ok) {
			const remaining = Number(response.headers.get("x-ratelimit-remaining"));
			const resetAfter = Number(response.headers.get("x-ratelimit-reset-after"));
			return {
				ok: true,
				retryAfterSeconds: 0,
				resetAfterSeconds: Number.isFinite(resetAfter) && resetAfter > 0 ? Math.ceil(resetAfter) : 0,
				remaining: Number.isFinite(remaining) ? remaining : null,
			};
		}
		const errorBody = await response.json().catch(() => null) as {retry_after?: number} | null;
		return {
			ok: false,
			retryAfterSeconds: retryAfterSeconds(response, errorBody),
			resetAfterSeconds: 0,
			remaining: null,
		};
	}
	catch(errorValue) {
		console.error(JSON.stringify({
			event: "discord_request_failed",
			message: errorValue instanceof Error ? errorValue.message : String(errorValue),
		}));
		return {ok: false, retryAfterSeconds: 30, resetAfterSeconds: 0, remaining: null};
	}
}

async function notifyInteraction(row: ChallengeRow, content: string): Promise<boolean> {
	const url = `https://discord.com/api/v10/webhooks/${encodeURIComponent(row.application_id)}/${encodeURIComponent(row.interaction_token)}`;
	try {
		const response = await fetch(url, {
			method: "POST",
			headers: {"content-type": "application/json"},
			body: JSON.stringify({content, flags: EPHEMERAL_FLAG}),
			signal: AbortSignal.timeout(8000),
		});
		return response.ok;
	}
	catch(errorValue) {
		console.error(JSON.stringify({
			event: "discord_followup_failed",
			message: errorValue instanceof Error ? errorValue.message : String(errorValue),
		}));
		return false;
	}
}

async function loadChallenge(env: DiscordEnv, token: string): Promise<ChallengeRow | null> {
	if(!TOKEN_RE.test(token))
		return null;
	return await env.DB.prepare(
		`SELECT token, discord_user_id, application_id, interaction_token, guild_id, expires_at, consumed
		 FROM discord_link_challenges WHERE token = ?1`,
	).bind(token).first<ChallengeRow>();
}

async function consumeChallenge(env: DiscordEnv, token: string, now: number): Promise<boolean> {
	const result = await env.DB.prepare(
		`UPDATE discord_link_challenges SET consumed = 1
		 WHERE token = ?1 AND consumed = 0 AND expires_at > ?2`,
	).bind(token, now).run();
	return (result.meta.changes ?? 0) > 0;
}

function challengeFailure(row: ChallengeRow | null, now: number): Response | null {
	if(!row)
		return error(404, "challenge_not_found", "This link is invalid or has expired.");
	if(row.consumed)
		return error(410, "challenge_consumed", "This link is invalid or has expired.");
	if(row.expires_at <= now)
		return error(410, "challenge_expired", "This link is invalid or has expired.");
	return null;
}

function linkPage(token: string, expired: boolean): Response {
	const deepLink = `uclient://discord/link/${token}`;
	const body = expired
		? `<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><title>UClient</title></head><body><p>This link is invalid or has expired.</p></body></html>`
		: `<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><title>Link UClient</title></head><body><p>Opening UClient…</p><p><a id="open" href="${deepLink}">Open UClient</a></p><script>(function(){var deepLink=${JSON.stringify(deepLink)};var frame=document.createElement("iframe");frame.setAttribute("aria-hidden","true");frame.style.cssText="position:absolute;width:0;height:0;border:0;visibility:hidden";frame.src=deepLink;document.body.appendChild(frame);setTimeout(function(){frame.remove();},3000);window.location.href=deepLink;})();</script></body></html>`;
	return new Response(body, {
		status: expired ? 410 : 200,
		headers: {"content-type": "text/html; charset=utf-8", "cache-control": "no-store"},
	});
}

async function startLink(request: Request, env: DiscordEnv): Promise<Response> {
	if(!await internalAuthorized(request, env))
		return error(401, "invalid_bot_secret", "Bot authentication failed.");
	const body = await readJson<{
		discord_user_id?: string;
		application_id?: string;
		interaction_token?: string;
		guild_id?: string;
	}>(request);
	if(!body || !SNOWFLAKE_RE.test(body.discord_user_id ?? "") || !SNOWFLAKE_RE.test(body.application_id ?? "") || !validText(body.interaction_token, 20, 512))
		return error(400, "invalid_request", "Discord link details are invalid.");
	const guildId = body.guild_id && SNOWFLAKE_RE.test(body.guild_id) ? body.guild_id : "";
	const token = base64Url(crypto.getRandomValues(new Uint8Array(32)));
	const now = nowSeconds();
	await env.DB.prepare(
		`INSERT INTO discord_link_challenges
		 (token, discord_user_id, application_id, interaction_token, guild_id, created_at, expires_at, consumed)
		 VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, 0)`,
	).bind(token, body.discord_user_id, body.application_id, body.interaction_token, guildId, now, now + LINK_TTL_SECONDS).run();
	const url = new URL(`/discord/link/${token}`, request.url);
	return json({url: url.toString(), expires_at: now + LINK_TTL_SECONDS});
}

async function rejectLink(env: DiscordEnv, token: string, request: Request): Promise<Response> {
	const now = nowSeconds();
	const row = await loadChallenge(env, token);
	const failure = challengeFailure(row, now);
	if(failure || !row)
		return failure ?? error(404, "challenge_not_found", "This link is invalid or has expired.");
	const body = await readJson<{reason?: string}>(request);
	const reason = body?.reason;
	const message = reason === "not_logged_in" ? DISCORD_TEXT.notLoggedIn : reason === "no_email" ? DISCORD_TEXT.noEmail : "";
	if(!message)
		return error(400, "invalid_request", "Link rejection reason is invalid.");
	if(!await consumeChallenge(env, token, now))
		return error(410, "challenge_consumed", "This link is invalid or has expired.");
	const notified = await notifyInteraction(row, message);
	return json({ok: true, message, notified});
}

async function confirmLink(env: DiscordEnv, token: string, authenticate: (request: Request) => Promise<DiscordAuth | Response>, request: Request): Promise<Response> {
	const now = nowSeconds();
	const row = await loadChallenge(env, token);
	const failure = challengeFailure(row, now);
	if(failure || !row)
		return failure ?? error(404, "challenge_not_found", "This link is invalid or has expired.");
	const authenticated = await authenticate(request);
	if(authenticated instanceof Response)
		return authenticated;
	const account = await env.DB.prepare(
		"SELECT email_normalized FROM accounts WHERE install_id = ?1",
	).bind(authenticated.installId).first<{email_normalized: string | null}>();
	if(!account?.email_normalized)
		return error(403, "email_required", DISCORD_TEXT.noEmail);
	if(!await consumeChallenge(env, token, now))
		return error(410, "challenge_consumed", "This link is invalid or has expired.");

	const previous = await env.DB.prepare(
		"SELECT install_id FROM discord_links WHERE discord_user_id = ?1 OR install_id = ?2",
	).bind(row.discord_user_id, authenticated.installId).all<{install_id: string}>();
	const installIds = [...new Set(previous.results.map(value => value.install_id).concat(authenticated.installId))];
	const statements: D1PreparedStatement[] = [
		env.DB.prepare("DELETE FROM discord_message_channels WHERE discord_user_id = ?1 OR install_id = ?2")
			.bind(row.discord_user_id, authenticated.installId),
		env.DB.prepare("DELETE FROM discord_links WHERE discord_user_id = ?1 OR install_id = ?2")
			.bind(row.discord_user_id, authenticated.installId),
		env.DB.prepare(
			`INSERT INTO discord_links (discord_user_id, install_id, linked_at, game_online_at)
			 VALUES (?1, ?2, ?3, 0)`,
		).bind(row.discord_user_id, authenticated.installId, now),
	];
	for(const installId of installIds) {
		statements.push(env.DB.prepare("DELETE FROM discord_outbound_messages WHERE install_id = ?1").bind(installId));
	}
	await env.DB.batch(statements);
	const message = linkedMessage(authenticated.installId);
	const notified = await notifyInteraction(row, message);
	return json({ok: true, message, notified});
}

async function linkForInstall(env: DiscordEnv, installId: string): Promise<LinkRow | null> {
	return await env.DB.prepare(
		"SELECT discord_user_id, install_id, game_online_at FROM discord_links WHERE install_id = ?1",
	).bind(installId).first<LinkRow>();
}

async function linkForDiscordUser(env: DiscordEnv, discordUserId: string): Promise<(LinkRow & ChannelRow) | LinkRow | null> {
	return await env.DB.prepare(
		`SELECT l.discord_user_id, l.install_id, l.game_online_at,
		        c.guild_id, c.channel_id
		 FROM discord_links l
		 LEFT JOIN discord_message_channels c ON c.install_id = l.install_id
		 WHERE l.discord_user_id = ?1`,
	).bind(discordUserId).first<LinkRow & {guild_id: string | null; channel_id: string | null}>();
}

function gameOnline(gameOnlineAt: number, now: number): boolean {
	return gameOnlineAt > 0 && now - gameOnlineAt <= GAME_ONLINE_SECONDS;
}

async function touchOnline(env: DiscordEnv, installId: string, now: number): Promise<void> {
	await env.DB.prepare("UPDATE discord_links SET game_online_at = ?2 WHERE install_id = ?1").bind(installId, now).run();
}

async function status(request: Request, env: DiscordEnv): Promise<Response> {
	if(!await internalAuthorized(request, env))
		return error(401, "invalid_bot_secret", "Bot authentication failed.");
	const discordUserId = new URL(request.url).searchParams.get("discord_user_id") ?? "";
	if(!SNOWFLAKE_RE.test(discordUserId))
		return error(400, "invalid_request", "Discord user id is invalid.");
	const row = await linkForDiscordUser(env, discordUserId);
	if(!row)
		return json({linked: false, channel_id: "", guild_id: "", game_online: false, message: DISCORD_TEXT.notLinked});
	const channelId = "channel_id" in row && row.channel_id ? row.channel_id : "";
	const guildId = "guild_id" in row && row.guild_id ? row.guild_id : "";
	return json({
		linked: true,
		install_id: row.install_id,
		channel_id: channelId,
		guild_id: guildId,
		game_online: gameOnline(row.game_online_at, nowSeconds()),
		message: channelId ? DISCORD_TEXT.channelExists : "",
	});
}

async function registerChannel(request: Request, env: DiscordEnv): Promise<Response> {
	if(!await internalAuthorized(request, env))
		return error(401, "invalid_bot_secret", "Bot authentication failed.");
	const body = await readJson<{discord_user_id?: string; guild_id?: string; channel_id?: string}>(request);
	if(!body || !SNOWFLAKE_RE.test(body.discord_user_id ?? "") || !SNOWFLAKE_RE.test(body.guild_id ?? "") || !SNOWFLAKE_RE.test(body.channel_id ?? ""))
		return error(400, "invalid_request", "Channel details are invalid.");
	const link = await linkForDiscordUser(env, body.discord_user_id!);
	if(!link)
		return error(403, "not_linked", DISCORD_TEXT.notLinked);
	const existing = await env.DB.prepare(
		"SELECT channel_id FROM discord_message_channels WHERE install_id = ?1",
	).bind(link.install_id).first<{channel_id: string}>();
	if(existing)
		return error(409, "channel_exists", DISCORD_TEXT.channelExists);
	await env.DB.prepare(
		`INSERT INTO discord_message_channels (install_id, discord_user_id, guild_id, channel_id, created_at)
		 VALUES (?1, ?2, ?3, ?4, ?5)`,
	).bind(link.install_id, body.discord_user_id, body.guild_id, body.channel_id, nowSeconds()).run();
	return json({ok: true, channel_id: body.channel_id});
}

async function deleteChannel(request: Request, env: DiscordEnv): Promise<Response> {
	if(!await internalAuthorized(request, env))
		return error(401, "invalid_bot_secret", "Bot authentication failed.");
	const body = await readJson<{discord_user_id?: string}>(request);
	if(!body || !SNOWFLAKE_RE.test(body.discord_user_id ?? ""))
		return error(400, "invalid_request", "Discord user id is invalid.");
	const link = await linkForDiscordUser(env, body.discord_user_id!);
	if(!link)
		return error(403, "not_linked", DISCORD_TEXT.notLinked);
	const existing = await env.DB.prepare(
		"SELECT channel_id FROM discord_message_channels WHERE install_id = ?1",
	).bind(link.install_id).first<{channel_id: string}>();
	if(!existing)
		return error(404, "no_channel", DISCORD_TEXT.noChannel);
	await env.DB.prepare("DELETE FROM discord_message_channels WHERE install_id = ?1").bind(link.install_id).run();
	return json({ok: true, channel_id: existing.channel_id});
}

async function claimLines(env: DiscordEnv, installId: string, lines: string[], now: number): Promise<string[]> {
	await env.DB.prepare("DELETE FROM discord_sent_lines WHERE created_at < ?1").bind(now - 24 * 60 * 60).run();
	const fresh: string[] = [];
	for(const line of lines) {
		const result = await env.DB.prepare(
			`INSERT INTO discord_sent_lines (install_id, body, created_at)
			 VALUES (?1, ?2, ?3)
			 ON CONFLICT(install_id, body) DO UPDATE SET created_at = excluded.created_at
			 WHERE discord_sent_lines.created_at < excluded.created_at - 60`,
		).bind(installId, line, now).run();
		if((result.meta.changes ?? 0) > 0)
			fresh.push(line);
	}
	return fresh;
}

async function postLines(env: DiscordEnv, channelId: string, lines: string[]): Promise<boolean> {
	const token = env.DISCORD_BOT_TOKEN ?? "";
	if(!token)
		return false;
	for(const line of lines) {
		const payload = discordChatPayload(line);
		if(!payload.content)
			continue;
		const result = await discordFetch(`https://discord.com/api/v10/channels/${channelId}/messages`, token, payload);
		if(!result.ok)
			return false;
	}
	return true;
}

async function ingest(request: Request, env: DiscordEnv, authenticate: (request: Request) => Promise<DiscordAuth | Response>, ctx: ExecutionContext): Promise<Response> {
	const authenticated = await authenticate(request);
	if(authenticated instanceof Response)
		return authenticated;
	const link = await linkForInstall(env, authenticated.installId);
	if(!link)
		return json({ok: true, linked: false, delivered: false, accepted: 0});
	const now = nowSeconds();
	await touchOnline(env, authenticated.installId, now);
	const body = await readJson<{line?: string; lines?: string[]}>(request);
	const lines = (Array.isArray(body?.lines) ? body.lines : body?.line ? [body.line] : [])
		.filter((line): line is string => typeof line === "string" && line.trim().length > 0)
		.slice(0, MAX_INGEST_LINES)
		.map(line => line.slice(0, MAX_DISCORD_CONTENT));
	if(lines.length === 0)
		return json({ok: true, linked: true, delivered: false, accepted: 0});
	const channel = await env.DB.prepare(
		"SELECT channel_id FROM discord_message_channels WHERE install_id = ?1",
	).bind(authenticated.installId).first<{channel_id: string}>();
	if(!channel)
		return json({ok: true, linked: true, delivered: false, accepted: 0});
	const fresh = await claimLines(env, authenticated.installId, lines, now);
	if(fresh.length > 0)
		ctx.waitUntil(postLines(env, channel.channel_id, fresh));
	return json({ok: true, linked: true, delivered: true, accepted: fresh.length});
}

async function flushDiscordTopic(env: DiscordEnv, installId: string): Promise<void> {
	const row = await env.DB.prepare(
		"SELECT channel_id, topic, topic_desired, topic_retry_at FROM discord_message_channels WHERE install_id = ?1",
	).bind(installId).first<{channel_id: string; topic: string; topic_desired: string; topic_retry_at: number}>();
	if(!row || row.topic === row.topic_desired)
		return;
	const now = nowSeconds();
	if(row.topic_retry_at > now)
		return;
	const token = env.DISCORD_BOT_TOKEN ?? "";
	if(!token)
		return;
	const result = await discordFetch(`https://discord.com/api/v10/channels/${row.channel_id}`, token, {topic: row.topic_desired}, "PATCH");
	if(result.ok) {
		const retryAt = result.remaining === 0 ? now + result.resetAfterSeconds : 0;
		await env.DB.prepare(
			`UPDATE discord_message_channels
			 SET topic = topic_desired, topic_retry_at = ?2
			 WHERE install_id = ?1 AND topic_desired = ?3`,
		).bind(installId, retryAt, row.topic_desired).run();
		return;
	}
	await env.DB.prepare(
		"UPDATE discord_message_channels SET topic_retry_at = ?2 WHERE install_id = ?1",
	).bind(installId, now + result.retryAfterSeconds).run();
}

async function setTopic(request: Request, env: DiscordEnv, authenticate: (request: Request) => Promise<DiscordAuth | Response>, ctx: ExecutionContext): Promise<Response> {
	const authenticated = await authenticate(request);
	if(authenticated instanceof Response)
		return authenticated;
	const body = await readJson<{topic?: unknown}>(request);
	if(!body || typeof body.topic !== "string")
		return error(400, "invalid_request", "Topic is invalid.");
	const topic = neutralizeMentions(body.topic).slice(0, MAX_TOPIC);
	const link = await linkForInstall(env, authenticated.installId);
	if(!link)
		return json({ok: true, applied: false});
	const saved = await env.DB.prepare(
		"UPDATE discord_message_channels SET topic_desired = ?2 WHERE install_id = ?1",
	).bind(authenticated.installId, topic).run();
	if((saved.meta.changes ?? 0) === 0)
		return json({ok: true, applied: false});
	ctx.waitUntil(flushDiscordTopic(env, authenticated.installId));
	return json({ok: true, applied: true});
}

async function outbound(request: Request, env: DiscordEnv, authenticate: (request: Request) => Promise<DiscordAuth | Response>, ctx: ExecutionContext): Promise<Response> {
	const authenticated = await authenticate(request);
	if(authenticated instanceof Response)
		return authenticated;
	const link = await linkForInstall(env, authenticated.installId);
	if(!link)
		return json({linked: false, messages: []});
	await touchOnline(env, authenticated.installId, nowSeconds());
	ctx.waitUntil(flushDiscordTopic(env, authenticated.installId));
	const taken = await env.DB.prepare(
		`DELETE FROM discord_outbound_messages
		 WHERE id IN (
		   SELECT id FROM discord_outbound_messages
		   WHERE install_id = ?1
		   ORDER BY id ASC
		   LIMIT 10
		 )
		 RETURNING id, body, mode, room_id`,
	).bind(authenticated.installId).all<{id: number; body: string; mode: string; room_id: string}>();
	return json({
		linked: true,
		messages: taken.results.map(row => ({
			id: row.id,
			body: row.body,
			mode: row.mode || "all",
			room_id: row.room_id || "",
		})),
	});
}

async function inbound(request: Request, env: DiscordEnv): Promise<Response> {
	if(!await internalAuthorized(request, env))
		return error(401, "invalid_bot_secret", "Bot authentication failed.");
	const body = await readJson<{channel_id?: string; content?: string}>(request);
	if(!body || !SNOWFLAKE_RE.test(body.channel_id ?? "") || typeof body.content !== "string")
		return error(400, "invalid_request", "Inbound chat is invalid.");
	const text = body.content.trim().slice(0, MAX_GAME_CHAT);
	if(!text)
		return json({ok: true, ignored: true});
	const channel = await env.DB.prepare(
		`SELECT c.install_id, l.game_online_at
		 FROM discord_message_channels c
		 JOIN discord_links l ON l.install_id = c.install_id
		 WHERE c.channel_id = ?1`,
	).bind(body.channel_id).first<{install_id: string; game_online_at: number}>();
	if(!channel)
		return error(404, "no_channel", DISCORD_TEXT.noChannel);
	const now = nowSeconds();
	if(!gameOnline(channel.game_online_at, now))
		return json({ok: false, reason: "offline", message: DISCORD_TEXT.notInGame});
	await env.DB.prepare(
		"INSERT INTO discord_outbound_messages (install_id, body, created_at, mode, room_id) VALUES (?1, ?2, ?3, 'all', '')",
	).bind(channel.install_id, text, now).run();
	return json({ok: true});
}

async function discordRooms(request: Request, env: DiscordEnv): Promise<Response> {
	if(!await internalAuthorized(request, env))
		return error(401, "invalid_bot_secret", "Bot authentication failed.");
	const discordUserId = new URL(request.url).searchParams.get("discord_user_id") ?? "";
	if(!SNOWFLAKE_RE.test(discordUserId))
		return error(400, "invalid_request", "Discord user id is invalid.");
	const link = await linkForDiscordUser(env, discordUserId);
	if(!link)
		return json({rooms: []});
	const rooms = await env.DB.prepare(
		`SELECT r.id, r.name,
		        (SELECT COUNT(*) FROM room_members c WHERE c.room_id = r.id) AS member_count
		 FROM rooms r
		 JOIN room_members m ON m.room_id = r.id
		 WHERE m.install_id = ?1
		 ORDER BY r.name COLLATE NOCASE ASC
		 LIMIT 100`,
	).bind(link.install_id).all<{id: string; name: string; member_count: number}>();
	return json({rooms: rooms.results});
}

async function sendCommand(request: Request, env: DiscordEnv): Promise<Response> {
	if(!await internalAuthorized(request, env))
		return error(401, "invalid_bot_secret", "Bot authentication failed.");
	const body = await readJson<{discord_user_id?: string; content?: string; mode?: string; room_id?: string}>(request);
	const mode = body?.mode ?? "";
	const roomId = body?.room_id?.trim() ?? "";
	if(!body || !SNOWFLAKE_RE.test(body.discord_user_id ?? "") || typeof body.content !== "string")
		return error(400, "invalid_request", "Send request is invalid.");
	if(roomId && (mode === "all" || mode === "team"))
		return error(400, "invalid_request", DISCORD_TEXT.modeRoomConflict);
	if(!roomId && mode !== "" && mode !== "all" && mode !== "team" && mode !== "uclient")
		return error(400, "invalid_request", "Send request is invalid.");
	const link = await linkForDiscordUser(env, body.discord_user_id!);
	if(!link)
		return error(403, "not_linked", DISCORD_TEXT.notLinked);
	const now = nowSeconds();
	if(!gameOnline(link.game_online_at, now))
		return json({ok: false, reason: "offline", message: DISCORD_TEXT.notInGame});
	let storedMode = mode || "all";
	let storedRoom = "";
	let limit = MAX_GAME_CHAT;
	if(roomId) {
		if(!/^[A-Za-z0-9_-]{1,64}$/.test(roomId))
			return error(404, "no_room", DISCORD_TEXT.noRoom);
		const member = await env.DB.prepare(
			`SELECT r.id
			 FROM rooms r
			 JOIN room_members m ON m.room_id = r.id
			 WHERE r.id = ?1 AND m.install_id = ?2`,
		).bind(roomId, link.install_id).first<{id: string}>();
		if(!member)
			return error(404, "no_room", DISCORD_TEXT.noRoom);
		storedMode = "room";
		storedRoom = roomId;
		limit = 512;
	}
	else if(mode === "uclient")
		limit = 512;
	const text = body.content.trim().slice(0, limit);
	if(!text)
		return json({ok: true, ignored: true});
	await env.DB.prepare(
		"INSERT INTO discord_outbound_messages (install_id, body, created_at, mode, room_id) VALUES (?1, ?2, ?3, ?4, ?5)",
	).bind(link.install_id, text, now, storedMode, storedRoom).run();
	return json({ok: true});
}

export async function handleDiscord(
	request: Request,
	env: DiscordEnv,
	segments: string[],
	authenticate: (request: Request) => Promise<DiscordAuth | Response>,
	ctx: ExecutionContext,
): Promise<Response> {
	if(segments[0] === "internal" && segments[1] === "discord") {
		if(segments.length === 3 && segments[2] === "status" && request.method === "GET")
			return status(request, env);
		if(segments.length === 4 && segments[2] === "link" && segments[3] === "start" && request.method === "POST")
			return startLink(request, env);
		if(segments.length === 3 && segments[2] === "channels" && request.method === "POST")
			return registerChannel(request, env);
		if(segments.length === 3 && segments[2] === "channels" && request.method === "DELETE")
			return deleteChannel(request, env);
		if(segments.length === 3 && segments[2] === "inbound" && request.method === "POST")
			return inbound(request, env);
		if(segments.length === 3 && segments[2] === "rooms" && request.method === "GET")
			return discordRooms(request, env);
		if(segments.length === 3 && segments[2] === "send" && request.method === "POST")
			return sendCommand(request, env);
		return error(404, "not_found", "Endpoint not found.");
	}

	if(segments[0] !== "discord")
		return error(404, "not_found", "Endpoint not found.");
	if(segments[1] === "link" && segments[2] && TOKEN_RE.test(segments[2])) {
		if(segments.length === 3 && request.method === "GET") {
			const row = await loadChallenge(env, segments[2]);
			const expired = !row || row.consumed !== 0 || row.expires_at <= nowSeconds();
			return linkPage(segments[2], expired);
		}
		if(segments.length === 4 && segments[3] === "reject" && request.method === "POST")
			return rejectLink(env, segments[2], request);
		if(segments.length === 4 && segments[3] === "confirm" && request.method === "POST")
			return confirmLink(env, segments[2], authenticate, request);
	}
	if(segments[1] === "chat" && segments[2] === "ingest" && segments.length === 3 && request.method === "POST")
		return ingest(request, env, authenticate, ctx);
	if(segments[1] === "chat" && segments[2] === "topic" && segments.length === 3 && request.method === "POST")
		return setTopic(request, env, authenticate, ctx);
	if(segments[1] === "chat" && segments[2] === "outbound" && segments.length === 3 && request.method === "GET")
		return outbound(request, env, authenticate, ctx);
	return error(404, "not_found", "Endpoint not found.");
}
