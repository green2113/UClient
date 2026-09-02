interface NoticeRow {
	id: string;
	title: string;
	body: string;
	severity: string;
	blocks_play: number;
	starts_at: number | null;
	ends_at: number | null;
	sort_order: number;
	enabled: number;
	created_at: number;
	updated_at: number;
}

interface NoticeInput {
	title?: string;
	body?: string;
	severity?: string;
	blocks_play?: boolean;
	starts_at?: number | null;
	ends_at?: number | null;
	sort_order?: number;
	enabled?: boolean;
}

type NoticeEnv = {
	DB: D1Database;
	ADMIN_TOKEN?: string;
	ADMIN_PASSWORD?: string;
};

const SEVERITIES = new Set(["info", "warning", "critical"]);

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

function adminAuthorized(request: Request, env: NoticeEnv): boolean {
	const token = env.ADMIN_TOKEN ?? env.ADMIN_PASSWORD ?? "";
	if(!token)
		return false;
	const authorization = request.headers.get("authorization") ?? "";
	const provided = authorization.startsWith("Bearer ") ? authorization.slice(7) : "";
	return provided.length > 0 && timingSafeEqual(provided, token);
}

function serializePublicNotice(row: NoticeRow) {
	return {
		id: row.id,
		title: row.title,
		body: row.body,
		severity: row.severity,
		blocks_play: row.blocks_play !== 0,
	};
}

function serializeAdminNotice(row: NoticeRow) {
	return {
		...serializePublicNotice(row),
		starts_at: row.starts_at,
		ends_at: row.ends_at,
		sort_order: row.sort_order,
		enabled: row.enabled !== 0,
		created_at: row.created_at,
		updated_at: row.updated_at,
	};
}

async function readJson<T>(request: Request): Promise<T | null> {
	try {
		return await request.json<T>();
	}
	catch {
		return null;
	}
}

function validText(value: unknown, min: number, max: number): value is string {
	return typeof value === "string" && value.trim().length >= min && value.trim().length <= max;
}

function parseOptionalTimestamp(value: unknown): number | null | undefined {
	if(value === undefined)
		return undefined;
	if(value === null)
		return null;
	if(typeof value === "number" && Number.isFinite(value))
		return Math.floor(value);
	return undefined;
}

function validateNoticeInput(input: NoticeInput | null, requireAll: boolean): NoticeInput | null {
	if(!input)
		return null;
	const result: NoticeInput = {};
	if(requireAll || input.title !== undefined) {
		if(!validText(input.title, 1, 120))
			return null;
		result.title = input.title.trim();
	}
	if(requireAll || input.body !== undefined) {
		if(!validText(input.body, 1, 4000))
			return null;
		result.body = input.body.trim();
	}
	if(requireAll || input.severity !== undefined) {
		const severity = (input.severity ?? "warning").trim();
		if(!SEVERITIES.has(severity))
			return null;
		result.severity = severity;
	}
	if(requireAll || input.blocks_play !== undefined)
		result.blocks_play = !!input.blocks_play;
	if(input.starts_at !== undefined) {
		const startsAt = parseOptionalTimestamp(input.starts_at);
		if(startsAt === undefined)
			return null;
		result.starts_at = startsAt;
	}
	else if(requireAll) {
		result.starts_at = null;
	}
	if(input.ends_at !== undefined) {
		const endsAt = parseOptionalTimestamp(input.ends_at);
		if(endsAt === undefined)
			return null;
		result.ends_at = endsAt;
	}
	else if(requireAll) {
		result.ends_at = null;
	}
	if(requireAll || input.sort_order !== undefined) {
		if(!Number.isInteger(input.sort_order))
			return null;
		result.sort_order = input.sort_order as number;
	}
	if(requireAll || input.enabled !== undefined)
		result.enabled = !!input.enabled;
	return result;
}

async function activeNotices(db: D1Database, now: number): Promise<NoticeRow[]> {
	const result = await db.prepare(
		`SELECT *
		 FROM launcher_notices
		 WHERE enabled = 1
		   AND (starts_at IS NULL OR starts_at <= ?1)
		   AND (ends_at IS NULL OR ends_at > ?1)
		 ORDER BY blocks_play DESC,
		          CASE severity WHEN 'critical' THEN 0 WHEN 'warning' THEN 1 ELSE 2 END,
		          sort_order ASC,
		          created_at DESC`,
	).bind(now).all<NoticeRow>();
	return result.results;
}

export async function publicNotices(env: NoticeEnv): Promise<Response> {
	const now = Math.floor(Date.now() / 1000);
	const rows = await activeNotices(env.DB, now);
	return json({notices: rows.map(serializePublicNotice)});
}

export async function adminListNotices(request: Request, env: NoticeEnv): Promise<Response> {
	if(!adminAuthorized(request, env))
		return error(401, "authentication_required", "Admin credentials are required.");
	const rows = await env.DB.prepare(
		"SELECT * FROM launcher_notices ORDER BY sort_order ASC, created_at DESC",
	).all<NoticeRow>();
	return json({notices: rows.results.map(serializeAdminNotice)});
}

export async function adminCreateNotice(request: Request, env: NoticeEnv): Promise<Response> {
	if(!adminAuthorized(request, env))
		return error(401, "authentication_required", "Admin credentials are required.");
	const input = validateNoticeInput(await readJson<NoticeInput>(request), true);
	if(!input)
		return error(400, "invalid_request", "A valid notice payload is required.");
	const now = Math.floor(Date.now() / 1000);
	const id = crypto.randomUUID();
	await env.DB.prepare(
		`INSERT INTO launcher_notices
		 (id, title, body, severity, blocks_play, starts_at, ends_at, sort_order, enabled, created_at, updated_at)
		 VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?10)`,
	).bind(
		id,
		input.title,
		input.body,
		input.severity ?? "warning",
		input.blocks_play ? 1 : 0,
		input.starts_at ?? null,
		input.ends_at ?? null,
		input.sort_order ?? 0,
		input.enabled === false ? 0 : 1,
		now,
	).run();
	const row = await env.DB.prepare("SELECT * FROM launcher_notices WHERE id = ?1").bind(id).first<NoticeRow>();
	return json({notice: row ? serializeAdminNotice(row) : null}, 201);
}

export async function adminUpdateNotice(request: Request, env: NoticeEnv, noticeId: string): Promise<Response> {
	if(!adminAuthorized(request, env))
		return error(401, "authentication_required", "Admin credentials are required.");
	const existing = await env.DB.prepare("SELECT * FROM launcher_notices WHERE id = ?1").bind(noticeId).first<NoticeRow>();
	if(!existing)
		return error(404, "not_found", "Notice not found.");
	const input = validateNoticeInput(await readJson<NoticeInput>(request), false);
	if(!input)
		return error(400, "invalid_request", "A valid notice update is required.");
	const now = Math.floor(Date.now() / 1000);
	await env.DB.prepare(
		`UPDATE launcher_notices
		 SET title = ?2,
		     body = ?3,
		     severity = ?4,
		     blocks_play = ?5,
		     starts_at = ?6,
		     ends_at = ?7,
		     sort_order = ?8,
		     enabled = ?9,
		     updated_at = ?10
		 WHERE id = ?1`,
	).bind(
		noticeId,
		input.title ?? existing.title,
		input.body ?? existing.body,
		input.severity ?? existing.severity,
		(input.blocks_play ?? existing.blocks_play !== 0) ? 1 : 0,
		input.starts_at !== undefined ? input.starts_at : existing.starts_at,
		input.ends_at !== undefined ? input.ends_at : existing.ends_at,
		input.sort_order ?? existing.sort_order,
		(input.enabled ?? existing.enabled !== 0) ? 1 : 0,
		now,
	).run();
	const row = await env.DB.prepare("SELECT * FROM launcher_notices WHERE id = ?1").bind(noticeId).first<NoticeRow>();
	return json({notice: row ? serializeAdminNotice(row) : null});
}

export async function adminDeleteNotice(request: Request, env: NoticeEnv, noticeId: string): Promise<Response> {
	if(!adminAuthorized(request, env))
		return error(401, "authentication_required", "Admin credentials are required.");
	const result = await env.DB.prepare("DELETE FROM launcher_notices WHERE id = ?1").bind(noticeId).run();
	if(!result.meta.changes)
		return error(404, "not_found", "Notice not found.");
	return json({ok: true});
}
