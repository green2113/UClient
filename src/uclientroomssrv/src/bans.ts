import {adminAuthorized} from "./notices";

const UUID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[1-8][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i;
const MAX_BAN_SECONDS = 10 * 365 * 24 * 60 * 60;

interface BanEnv {
	DB: D1Database;
	ADMIN_TOKEN?: string;
	ADMIN_PASSWORD?: string;
}

interface AccountBanRow {
	install_id: string;
	email_normalized: string | null;
	last_player_name: string;
	last_seen_at: number;
	last_client_version: string;
	last_launcher_version: string;
	last_ip: string;
	ban_id: number | null;
	ban_reason: string | null;
	banned_at: number | null;
	expires_at: number | null;
	banned_by: string | null;
}

interface BanListRow {
	id: number;
	install_id: string;
	reason: string;
	banned_at: number;
	expires_at: number | null;
	banned_by: string;
	email_normalized: string | null;
	last_player_name: string | null;
}

interface BanInput {
	install_id?: string;
	reason?: string;
	expires_at?: number | null;
}

function json(body: unknown, status = 200): Response {
	return new Response(JSON.stringify(body), {
		status,
		headers: {
			"content-type": "application/json; charset=utf-8",
			"cache-control": "no-store",
		},
	});
}

function error(status: number, code: string, message: string): Response {
	return json({error: code, message}, status);
}

function serializeAccount(row: AccountBanRow) {
	const banned = row.ban_id !== null;
	return {
		install_id: row.install_id,
		email: row.email_normalized,
		player_name: row.last_player_name,
		last_seen_at: row.last_seen_at,
		client_version: row.last_client_version,
		launcher_version: row.last_launcher_version,
		last_ip: row.last_ip,
		ban: banned ? {
			id: row.ban_id,
			reason: row.ban_reason,
			banned_at: row.banned_at,
			expires_at: row.expires_at,
			permanent: row.expires_at === null,
			banned_by: row.banned_by,
		} : null,
	};
}

function serializeBan(row: BanListRow) {
	return {
		id: row.id,
		install_id: row.install_id,
		reason: row.reason,
		banned_at: row.banned_at,
		expires_at: row.expires_at,
		permanent: row.expires_at === null,
		banned_by: row.banned_by,
		email: row.email_normalized,
		player_name: row.last_player_name ?? "",
	};
}

function likePattern(value: string): string {
	return `%${value.replace(/[\\%_]/g, ch => `\\${ch}`)}%`;
}

const ACCOUNT_SELECT = `
	SELECT a.install_id, a.email_normalized, a.last_player_name, a.last_seen_at,
	       a.last_client_version, a.last_launcher_version, a.last_ip,
	       b.id AS ban_id, b.reason AS ban_reason, b.banned_at, b.expires_at, b.banned_by
	FROM accounts a
	LEFT JOIN user_bans b ON b.id = (
		SELECT id FROM user_bans
		WHERE install_id = a.install_id AND (expires_at IS NULL OR expires_at > ?1)
		ORDER BY banned_at DESC LIMIT 1
	)`;

export async function adminSearchAccounts(request: Request, env: BanEnv): Promise<Response> {
	if(!adminAuthorized(request, env))
		return error(401, "authentication_required", "Admin credentials are required.");
	const query = new URL(request.url).searchParams.get("q")?.trim() ?? "";
	if(query.length < 2 || query.length > 120)
		return error(400, "invalid_request", "Search needs 2 to 120 characters.");
	const now = Math.floor(Date.now() / 1000);
	const email = query.toLowerCase();
	const rows = await env.DB.prepare(
		`${ACCOUNT_SELECT}
		 WHERE a.install_id = ?2
		    OR lower(IFNULL(a.email_normalized, '')) = ?3
		    OR a.last_player_name LIKE ?4 ESCAPE '\\'
		    OR a.last_ip = ?2
		 ORDER BY a.last_seen_at DESC
		 LIMIT 25`,
	).bind(now, query, email, likePattern(query)).all<AccountBanRow>();
	return json({accounts: rows.results.map(serializeAccount)});
}

export async function adminListBans(request: Request, env: BanEnv): Promise<Response> {
	if(!adminAuthorized(request, env))
		return error(401, "authentication_required", "Admin credentials are required.");
	const now = Math.floor(Date.now() / 1000);
	const rows = await env.DB.prepare(
		`SELECT b.id, b.install_id, b.reason, b.banned_at, b.expires_at, b.banned_by,
		        a.email_normalized, a.last_player_name
		 FROM user_bans b
		 LEFT JOIN accounts a ON a.install_id = b.install_id
		 WHERE b.expires_at IS NULL OR b.expires_at > ?1
		 ORDER BY b.banned_at DESC
		 LIMIT 100`,
	).bind(now).all<BanListRow>();
	return json({bans: rows.results.map(serializeBan)});
}

export async function adminCreateBan(request: Request, env: BanEnv): Promise<Response> {
	if(!adminAuthorized(request, env))
		return error(401, "authentication_required", "Admin credentials are required.");
	let input: BanInput | null = null;
	try {
		input = await request.json<BanInput>();
	}
	catch {
		input = null;
	}
	const installId = input?.install_id?.trim() ?? "";
	const reason = input?.reason?.trim() ?? "";
	if(!UUID_RE.test(installId) || reason.length < 1 || reason.length > 500)
		return error(400, "invalid_request", "A valid install id and reason are required.");
	const now = Math.floor(Date.now() / 1000);
	let expiresAt: number | null = null;
	if(input?.expires_at !== undefined && input.expires_at !== null) {
		if(typeof input.expires_at !== "number" || !Number.isFinite(input.expires_at))
			return error(400, "invalid_request", "expires_at must be a unix timestamp or null.");
		expiresAt = Math.floor(input.expires_at);
		if(expiresAt <= now || expiresAt > now + MAX_BAN_SECONDS)
			return error(400, "invalid_request", "Ban expiry must be in the future and within 10 years.");
	}
	const account = await env.DB.prepare("SELECT install_id FROM accounts WHERE install_id = ?1")
		.bind(installId).first<{install_id: string}>();
	if(!account)
		return error(404, "account_not_found", "No account uses that install id.");
	await env.DB.batch([
		env.DB.prepare(
			"DELETE FROM user_bans WHERE install_id = ?1 AND (expires_at IS NULL OR expires_at > ?2)",
		).bind(installId, now),
		env.DB.prepare(
			"INSERT INTO user_bans(install_id, reason, banned_at, expires_at, banned_by) VALUES (?1, ?2, ?3, ?4, 'admin')",
		).bind(installId, reason, now, expiresAt),
	]);
	const row = await env.DB.prepare(
		`${ACCOUNT_SELECT} WHERE a.install_id = ?2`,
	).bind(now, installId).first<AccountBanRow>();
	return json({account: row ? serializeAccount(row) : null}, 201);
}

export async function adminClearBans(request: Request, env: BanEnv, installId: string): Promise<Response> {
	if(!adminAuthorized(request, env))
		return error(401, "authentication_required", "Admin credentials are required.");
	if(!UUID_RE.test(installId))
		return error(400, "invalid_request", "A valid install id is required.");
	const now = Math.floor(Date.now() / 1000);
	const result = await env.DB.prepare(
		"DELETE FROM user_bans WHERE install_id = ?1 AND (expires_at IS NULL OR expires_at > ?2)",
	).bind(installId, now).run();
	return json({ok: true, removed: result.meta.changes ?? 0});
}
