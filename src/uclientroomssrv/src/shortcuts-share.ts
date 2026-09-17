interface ShareEnv {
	CFG_BACKUPS: R2Bucket;
	DB: D1Database;
	ASSETS?: Fetcher;
}

const SHARE_JSON_HEADERS = {
	"content-type": "application/json; charset=utf-8",
} as const;

const PUBLIC_CACHE_HEADERS = {
	"content-type": "application/json; charset=utf-8",
	"cache-control": "public, max-age=300",
} as const;

const UUID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[1-8][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i;
const MAX_SHARE_BYTES = 128 * 1024;
const MAX_ACTIONS = 200;

const ALLOWED_ACTION_TYPES = new Set([
	"send_chat", "text", "wait", "switch_weapon_use", "switch_weapon", "emote", "kill", "vote",
	"set_skin", "set_custom_color", "set_body_color", "set_feet_color", "set_name",
	"get", "get_clipboard", "get_player_info", "ask_for_text",
	"repeat", "end_repeat", "if", "otherwise", "end_if", "stop",
	"connect_server", "leave_server", "run_shortcut",
]);

function shareJson(body: unknown, status = 200, headers: Record<string, string> = SHARE_JSON_HEADERS): Response {
	return new Response(JSON.stringify(body), {status, headers});
}

function shareError(status: number, code: string, message: string): Response {
	return shareJson({error: code, message}, status);
}

function isRecord(value: unknown): value is Record<string, unknown> {
	return typeof value === "object" && value !== null && !Array.isArray(value);
}

function validateActions(actions: unknown): string | null {
	if(!Array.isArray(actions))
		return "actions must be an array";
	if(actions.length === 0)
		return "actions must not be empty";
	if(actions.length > MAX_ACTIONS)
		return `actions exceed limit of ${MAX_ACTIONS}`;
	for(const item of actions) {
		if(!isRecord(item))
			return "invalid action entry";
		const type = item.type;
		if(typeof type !== "string" || !ALLOWED_ACTION_TYPES.has(type))
			return `unsupported action type: ${String(type)}`;
	}
	return null;
}

function sanitizeEntry(raw: unknown): {name: string; kind: string; enabled: boolean; trigger: unknown; actions: unknown[]} | null {
	if(!isRecord(raw))
		return null;
	const name = typeof raw.name === "string" ? raw.name.trim() : "";
	if(name.length < 1 || name.length > 128)
		return null;
	const kind = raw.kind === "manual" || raw.kind === "automation" ? raw.kind : null;
	if(!kind)
		return null;
	if(kind === "automation" && !isRecord(raw.trigger))
		return null;
	if(kind === "manual" && raw.trigger != null && isRecord(raw.trigger))
		return null;
	const actionErr = validateActions(raw.actions);
	if(actionErr)
		return null;
	const enabled = raw.enabled === undefined ? true : !!raw.enabled;
	return {
		name,
		kind,
		enabled,
		trigger: kind === "automation" ? raw.trigger : null,
		actions: raw.actions as unknown[],
	};
}

async function requireEmailLinked(env: ShareEnv, installId: string): Promise<Response | null> {
	const account = await env.DB.prepare(
		"SELECT email_normalized FROM accounts WHERE install_id = ?1",
	).bind(installId).first<{email_normalized: string | null}>();
	if(!account?.email_normalized)
		return shareError(403, "email_required", "Connect an email to your account before sharing shortcuts.");
	return null;
}

async function readShareBody(request: Request): Promise<{entry: unknown} | Response> {
	const length = Number(request.headers.get("content-length") ?? "0");
	if(length > MAX_SHARE_BYTES)
		return shareError(413, "payload_too_large", "Share payload is too large.");
	const text = await request.text();
	if(text.length > MAX_SHARE_BYTES)
		return shareError(413, "payload_too_large", "Share payload is too large.");
	let parsed: unknown;
	try {
		parsed = JSON.parse(text);
	}
	catch {
		return shareError(400, "invalid_json", "Request body must be JSON.");
	}
	if(!isRecord(parsed))
		return shareError(400, "invalid_body", "Request body must be an object.");
	const entry = parsed.entry !== undefined ? parsed.entry : parsed;
	return {entry};
}

export async function postShortcutShare(request: Request, env: ShareEnv, installId: string): Promise<Response> {
	const emailGate = await requireEmailLinked(env, installId);
	if(emailGate)
		return emailGate;

	const body = await readShareBody(request);
	if(body instanceof Response)
		return body;

	const sanitized = sanitizeEntry(body.entry);
	if(!sanitized)
		return shareError(400, "invalid_entry", "Shortcut entry is invalid or uses unsupported actions.");

	const shareId = crypto.randomUUID();
	const now = Math.floor(Date.now() / 1000);
	const document = {
		version: 1,
		sharedAt: now,
		sharedByInstallId: installId,
		entry: sanitized,
	};
	const key = `shortcuts/share/${shareId}.json`;
	await env.CFG_BACKUPS.put(key, JSON.stringify(document), {
		httpMetadata: {
			contentType: "application/json; charset=utf-8",
			cacheControl: "public, max-age=300",
		},
		customMetadata: {
			source: "uclient-shortcut-share",
			kind: sanitized.kind,
		},
	});

	const origin = new URL(request.url).origin;
	const webUrl = `${origin}/shortcuts/share/${shareId}`;
	const deepLink = `uclient://share/${shareId}`;
	return shareJson({
		ok: true,
		shareId,
		webUrl,
		deepLink,
		kind: sanitized.kind,
		name: sanitized.name,
	}, 201);
}

export async function getShortcutShare(env: ShareEnv, shareId: string): Promise<Response> {
	if(!UUID_RE.test(shareId))
		return shareError(400, "invalid_share_id", "Share id must be a UUID.");
	const key = `shortcuts/share/${shareId}.json`;
	const object = await env.CFG_BACKUPS.get(key);
	if(!object)
		return shareError(404, "not_found", "This share link was not found.");
	return new Response(object.body, {status: 200, headers: PUBLIC_CACHE_HEADERS});
}

function wantsShareHtmlPage(request: Request): boolean {
	const dest = request.headers.get("Sec-Fetch-Dest");
	if(dest === "document")
		return true;
	const accept = request.headers.get("Accept") ?? "";
	return accept.includes("text/html") && !accept.includes("application/json");
}

export async function handleShortcutsShare(request: Request, env: ShareEnv, segments: string[]): Promise<Response> {
	if(segments.length === 2 && request.method === "POST")
		return shareError(405, "method_not_allowed", "Use POST /shortcuts/share with authentication.");
	if(segments.length === 3 && segments[2] && request.method === "GET") {
		if(wantsShareHtmlPage(request))
			return serveSharePage(env, request, segments[2]);
		return getShortcutShare(env, segments[2]);
	}
	return shareError(404, "not_found", "Endpoint not found.");
}

export async function serveSharePage(env: ShareEnv, request: Request, shareId: string): Promise<Response> {
	if(!UUID_RE.test(shareId))
		return shareError(400, "invalid_share_id", "Share id must be a UUID.");
	if(env.ASSETS) {
		const assetUrl = new URL("/share.html", request.url);
		const assetResponse = await env.ASSETS.fetch(assetUrl);
		if(assetResponse.ok)
			return assetResponse;
	}
	return shareError(404, "not_found", "Share page is not available.");
}
