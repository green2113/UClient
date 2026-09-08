import {
	adminCreateNotice,
	adminDeleteNotice,
	adminListNotices,
	adminUpdateNotice,
	publicNotices,
} from "./notices";

interface Env extends Cloudflare.Env {
	ACCOUNT_PEPPER: string;
	GRACE_PRIVATE_KEY_SEED_HEX: string;
	RELAY_SECRET: string;
	RELAY_INVALIDATE_SECRET?: string;
	ADMIN_TOKEN?: string;
	ADMIN_PASSWORD?: string;
	CFG_BACKUPS: R2Bucket;
}

interface AccountInput {
	install_id: string;
	secret: string;
	player_name?: string;
	version?: string;
}

interface AuthenticatedAccount {
	installId: string;
	ban: BanRow | null;
}

interface EmailAccountInput {
	email: string;
	password: string;
	install_id?: string;
	secret: string;
	version?: string;
}

interface AccountProfileRow {
	install_id: string;
	email_normalized: string | null;
}

interface BackupRow {
	id: string;
	account_install_id: string;
	relative_path: string;
	object_key: string;
	created_at: number;
	size_bytes: number;
	sha256: string;
}

interface BanRow {
	reason: string;
	expires_at: number | null;
}

interface RoomRow {
	id: string;
	name: string;
	owner_install_id: string;
	invite_code: string;
	created_at: number;
	name_color?: number;
	invite_code_public?: number;
}

interface MemberRow {
	member_id: string;
	display_name: string;
	role: string;
	joined_at: number;
	install_id?: string;
}

const JSON_HEADERS = {
	"content-type": "application/json; charset=utf-8",
	"cache-control": "no-store",
} as const;
const UUID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[1-8][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i;
const ROOM_ID_RE = /^[0-9a-f-]{36}$/i;
const MEMBER_ID_RE = /^[A-Za-z0-9_-]{20,64}$/;
const MAX_BODY_BYTES = 16 * 1024;
const REGISTRATION_WINDOW_SECONDS = 24 * 60 * 60;
const REGISTRATION_LIMIT_PER_IP = 5;
const AUTH_WINDOW_SECONDS = 15 * 60;
const AUTH_FAILURE_LIMIT = 10;
const GRACE_SECONDS = 7 * 24 * 60 * 60;
const IP_RETENTION_SECONDS = 90 * 24 * 60 * 60;
const MAX_OWNED_ROOMS = 5;
const PASSWORD_MIN_LENGTH = 10;
const PASSWORD_MAX_LENGTH = 128;
const PBKDF2_ITERATIONS = 100_000;
const BACKUP_OBJECT_LIMIT_BYTES = 10 * 1024 * 1024;
const BACKUP_ACCOUNT_QUOTA_BYTES = 10 * 1024 * 1024;
const BACKUP_EXTENSIONS = new Set([".cfg", ".txt", ".png", ".jpg", ".jpeg", ".log"]);
const SENSITIVE_BACKUP_NAMES = new Set(["steam_uclient_account.json", "uclient_account.json"]);

function json(body: unknown, status = 200): Response {
	return new Response(JSON.stringify(body), {status, headers: JSON_HEADERS});
}

function error(status: number, code: string, message: string): Response {
	return json({error: code, message}, status);
}

async function readJson<T>(request: Request): Promise<T | null> {
	const contentLength = Number(request.headers.get("content-length") ?? "0");
	if(contentLength > MAX_BODY_BYTES)
		return null;
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

function clientIp(request: Request): string {
	return request.headers.get("cf-connecting-ip")?.slice(0, 64) ?? "";
}

function bytesToHex(bytes: Uint8Array): string {
	return Array.from(bytes, value => value.toString(16).padStart(2, "0")).join("");
}

function base64Url(bytes: Uint8Array): string {
	let binary = "";
	for(const value of bytes)
		binary += String.fromCharCode(value);
	return btoa(binary).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/g, "");
}

function decodeHex(value: string): Uint8Array {
	const normalized = value?.trim() ?? "";
	if(normalized.length === 0 || normalized.length % 2 !== 0 || !/^[0-9a-f]+$/i.test(normalized))
		throw new Error(`Invalid hexadecimal value (length ${normalized.length}; expected 64 hexadecimal characters).`);
	const bytes = new Uint8Array(normalized.length / 2);
	for(let index = 0; index < bytes.length; index++)
		bytes[index] = Number.parseInt(normalized.slice(index * 2, index * 2 + 2), 16);
	return bytes;
}

async function secretHash(secret: string, pepper: string): Promise<string> {
	const input = new TextEncoder().encode(`${secret}\0${pepper}`);
	return bytesToHex(new Uint8Array(await crypto.subtle.digest("SHA-256", input)));
}

function decodeBase64Url(value: string): Uint8Array | null {
	try {
		const normalized = value.replace(/-/g, "+").replace(/_/g, "/");
		const binary = atob(normalized.padEnd(Math.ceil(normalized.length / 4) * 4, "="));
		return Uint8Array.from(binary, character => character.charCodeAt(0));
	}
	catch {
		return null;
	}
}

async function derivePassword(password: string, salt: Uint8Array, iterations: number): Promise<Uint8Array> {
	const key = await crypto.subtle.importKey(
		"raw",
		new TextEncoder().encode(password),
		{name: "PBKDF2"},
		false,
		["deriveBits"],
	);
	return new Uint8Array(await crypto.subtle.deriveBits(
		{name: "PBKDF2", hash: "SHA-256", salt, iterations},
		key,
		256,
	));
}

async function passwordHash(password: string): Promise<string> {
	const salt = crypto.getRandomValues(new Uint8Array(16));
	const derived = await derivePassword(password, salt, PBKDF2_ITERATIONS);
	return `pbkdf2-sha256$v=1$i=${PBKDF2_ITERATIONS}$${base64Url(salt)}$${base64Url(derived)}`;
}

async function passwordMatches(password: string, stored: string | null): Promise<boolean> {
	const parts = stored?.split("$") ?? [];
	const iterationsPart = parts[2] ?? "";
	const iterations = Number(iterationsPart.startsWith("i=") ? iterationsPart.slice(2) : "");
	const salt = decodeBase64Url(parts[3] ?? "");
	const expected = decodeBase64Url(parts[4] ?? "");
	const valid = parts[0] === "pbkdf2-sha256"
		&& parts[1] === "v=1"
		&& Number.isInteger(iterations)
		&& iterations >= 100_000
		&& iterations <= PBKDF2_ITERATIONS
		&& salt !== null
		&& salt.byteLength >= 16
		&& expected !== null
		&& expected.byteLength === 32;
	const actual = await derivePassword(
		password,
		valid ? salt : new Uint8Array(16),
		valid ? iterations : PBKDF2_ITERATIONS,
	);
	return valid && timingSafeEqual(bytesToHex(actual), bytesToHex(expected ?? new Uint8Array()));
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

function normalizedEmail(value: unknown): string | null {
	if(typeof value !== "string")
		return null;
	const normalized = value.trim().toLowerCase();
	if(normalized.length < 3 || normalized.length > 254 || !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(normalized))
		return null;
	return normalized;
}

function validPassword(value: unknown): value is string {
	return typeof value === "string" && value.length >= PASSWORD_MIN_LENGTH && value.length <= PASSWORD_MAX_LENGTH;
}

async function authRateKey(kind: string, email: string, request: Request): Promise<string> {
	const value = `${kind}\0${email}\0${clientIp(request)}`;
	return bytesToHex(new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(value))));
}

async function authRateLimited(db: D1Database, kind: string, rateKey: string, now: number): Promise<boolean> {
	const row = await db.prepare(
		`SELECT COUNT(*) AS count FROM auth_attempts
		 WHERE kind = ?1 AND rate_key = ?2 AND succeeded = 0 AND created_at > ?3`,
	).bind(kind, rateKey, now - AUTH_WINDOW_SECONDS).first<{count: number}>();
	return (row?.count ?? 0) >= AUTH_FAILURE_LIMIT;
}

async function recordAuthAttempt(
	db: D1Database,
	kind: string,
	rateKey: string,
	succeeded: boolean,
	now: number,
): Promise<void> {
	if(succeeded) {
		await db.batch([
			db.prepare("DELETE FROM auth_attempts WHERE kind = ?1 AND rate_key = ?2").bind(kind, rateKey),
			db.prepare("DELETE FROM auth_attempts WHERE created_at <= ?1").bind(now - AUTH_WINDOW_SECONDS),
		]);
		return;
	}
	await db.prepare(
		"INSERT INTO auth_attempts(kind, rate_key, succeeded, created_at) VALUES (?1, ?2, 0, ?3)",
	).bind(kind, rateKey, now).run();
}

async function credentialAccount(
	db: D1Database,
	installId: string,
	hash: string,
): Promise<{install_id: string} | null> {
	return await db.prepare(
		`SELECT a.install_id
		 FROM accounts a
		 WHERE a.install_id = ?1
		   AND (
		   	a.secret_hash = ?2
		   	OR EXISTS (
		   		SELECT 1 FROM account_device_credentials c
		   		WHERE c.account_install_id = a.install_id AND c.secret_hash = ?2
		   	)
		   )
		 LIMIT 1`,
	).bind(installId, hash).first<{install_id: string}>();
}

async function accountSuccess(env: Env, installId: string, now: number, status = 200): Promise<Response> {
	const account = await env.DB.prepare(
		"SELECT install_id, email_normalized FROM accounts WHERE install_id = ?1",
	).bind(installId).first<AccountProfileRow>();
	if(!account)
		return error(404, "account_not_found", "This account no longer exists.");
	const grace = await signGraceToken(env, installId, now);
	return json({
		install_id: installId,
		grace_token: grace.token,
		grace_expires_at: grace.expires_at,
		has_email: account.email_normalized !== null,
		email: account.email_normalized,
	}, status);
}

async function activeBan(db: D1Database, installId: string, now: number): Promise<BanRow | null> {
	return await db.prepare(
		`SELECT reason, expires_at
		 FROM user_bans
		 WHERE install_id = ?1 AND (expires_at IS NULL OR expires_at > ?2)
		 ORDER BY banned_at DESC LIMIT 1`,
	).bind(installId, now).first<BanRow>();
}

async function signGraceToken(env: Env, installId: string, now: number): Promise<{token: string; expires_at: number}> {
	const expiresAt = now + GRACE_SECONDS;
	const payload = new TextEncoder().encode(JSON.stringify({install_id: installId, exp: expiresAt}));
	// RFC 8410 PKCS#8 prefix for an Ed25519 32-byte private seed.
	const pkcs8Prefix = decodeHex("302e020100300506032b657004220420");
	const seed = decodeHex(env.GRACE_PRIVATE_KEY_SEED_HEX);
	if(seed.byteLength !== 32)
		throw new Error("GRACE_PRIVATE_KEY_SEED_HEX must contain exactly 32 bytes.");
	const keyData = new Uint8Array(48);
	keyData.set(pkcs8Prefix.slice(0, 16));
	keyData.set(seed, 16);
	const key = await crypto.subtle.importKey(
		"pkcs8",
		keyData.buffer,
		{name: "Ed25519"},
		false,
		["sign"],
	);
	const signature = new Uint8Array(await crypto.subtle.sign("Ed25519", key, payload));
	return {token: `${base64Url(payload)}.${base64Url(signature)}`, expires_at: expiresAt};
}

async function accountInput(request: Request): Promise<AccountInput | null> {
	const input = await readJson<AccountInput>(request);
	if(!input || !UUID_RE.test(input.install_id) || !validText(input.secret, 32, 256))
		return null;
	if(input.player_name !== undefined && !validText(input.player_name, 0, 64))
		return null;
	if(input.version !== undefined && !validText(input.version, 0, 64))
		return null;
	return input;
}

async function register(request: Request, env: Env): Promise<Response> {
	const input = await accountInput(request);
	if(!input)
		return error(400, "invalid_request", "Invalid account registration data.");

	const now = Math.floor(Date.now() / 1000);
	const ip = clientIp(request);
	const hash = await secretHash(input.secret, env.ACCOUNT_PEPPER);
	const existing = await env.DB.prepare("SELECT install_id FROM accounts WHERE install_id = ?1")
		.bind(input.install_id).first<{install_id: string}>();
	if(existing) {
		if(!await credentialAccount(env.DB, input.install_id, hash))
			return error(409, "account_exists", "This install UUID is already registered.");
		const ban = await activeBan(env.DB, input.install_id, now);
		if(ban)
			return json({error: "account_banned", reason: ban.reason, expires_at: ban.expires_at}, 423);
		await env.DB.prepare(
			`UPDATE accounts
			 SET last_seen_at = ?2,
			     last_player_name = CASE WHEN ?3 = '' THEN last_player_name ELSE ?3 END,
			     last_client_version = CASE WHEN ?4 = '' THEN last_client_version ELSE ?4 END,
			     last_ip = ?5
			 WHERE install_id = ?1`,
		).bind(input.install_id, now, input.player_name?.trim() ?? "", input.version?.trim() ?? "", ip).run();
		await env.DB.prepare(
			`INSERT OR IGNORE INTO account_device_credentials(account_install_id, secret_hash, created_at, last_used_at)
			 VALUES (?1, ?2, ?3, ?3)`,
		).bind(input.install_id, hash, now).run();
		return accountSuccess(env, input.install_id, now);
	}

	if(ip) {
		const attempts = await env.DB.prepare(
			"SELECT COUNT(*) AS count FROM registration_attempts WHERE ip = ?1 AND created_at > ?2",
		).bind(ip, now - REGISTRATION_WINDOW_SECONDS).first<{count: number}>();
		if((attempts?.count ?? 0) >= REGISTRATION_LIMIT_PER_IP)
			return error(429, "registration_rate_limited", "Too many accounts were registered from this network.");
	}

	const statements = [
		env.DB.prepare(
			`INSERT INTO accounts
			 (install_id, secret_hash, created_at, last_seen_at, last_player_name, last_client_version, created_ip, last_ip)
			 VALUES (?1, ?2, ?3, ?3, ?4, ?5, ?6, ?6)`,
		).bind(input.install_id, hash, now, input.player_name?.trim() ?? "", input.version?.trim() ?? "", ip),
		env.DB.prepare(
			`INSERT INTO account_device_credentials(account_install_id, secret_hash, created_at, last_used_at)
			 VALUES (?1, ?2, ?3, ?3)`,
		).bind(input.install_id, hash, now),
		env.DB.prepare("DELETE FROM registration_attempts WHERE created_at <= ?1").bind(now - REGISTRATION_WINDOW_SECONDS),
		env.DB.prepare(
			`UPDATE accounts SET
			 created_ip = CASE WHEN created_at <= ?1 THEN '' ELSE created_ip END,
			 last_ip = CASE WHEN last_seen_at <= ?1 THEN '' ELSE last_ip END
			 WHERE created_ip != '' OR last_ip != ''`,
		).bind(now - IP_RETENTION_SECONDS),
	];
	if(ip)
		statements.push(env.DB.prepare("INSERT INTO registration_attempts(ip, created_at) VALUES (?1, ?2)").bind(ip, now));
	await env.DB.batch(statements);

	return accountSuccess(env, input.install_id, now, 201);
}

async function verify(request: Request, env: Env): Promise<Response> {
	const input = await accountInput(request);
	if(!input)
		return error(400, "invalid_request", "Invalid account verification data.");

	const account = await env.DB.prepare("SELECT install_id FROM accounts WHERE install_id = ?1")
		.bind(input.install_id).first<{install_id: string}>();
	if(!account)
		return error(404, "account_not_found", "This install UUID is not registered.");

	const hash = await secretHash(input.secret, env.ACCOUNT_PEPPER);
	if(!await credentialAccount(env.DB, input.install_id, hash))
		return error(403, "invalid_credentials", "The account secret is invalid.");

	const now = Math.floor(Date.now() / 1000);
	const ban = await activeBan(env.DB, input.install_id, now);
	if(ban)
		return json({error: "account_banned", reason: ban.reason, expires_at: ban.expires_at}, 423);

	await env.DB.batch([
		env.DB.prepare(
			`UPDATE accounts
			 SET last_seen_at = ?2,
			     last_player_name = CASE WHEN ?3 = '' THEN last_player_name ELSE ?3 END,
			     last_client_version = CASE WHEN ?4 = '' THEN last_client_version ELSE ?4 END,
			     last_ip = ?5
			 WHERE install_id = ?1`,
		).bind(input.install_id, now, input.player_name?.trim() ?? "", input.version?.trim() ?? "", clientIp(request)),
		env.DB.prepare(
			`INSERT OR IGNORE INTO account_device_credentials(account_install_id, secret_hash, created_at, last_used_at)
			 VALUES (?1, ?2, ?3, ?3)`,
		).bind(input.install_id, hash, now),
		env.DB.prepare(
			"UPDATE account_device_credentials SET last_used_at = ?3 WHERE account_install_id = ?1 AND secret_hash = ?2",
		).bind(input.install_id, hash, now),
	]);

	return accountSuccess(env, input.install_id, now);
}

async function registerEmail(request: Request, env: Env): Promise<Response> {
	const input = await readJson<EmailAccountInput>(request);
	const email = normalizedEmail(input?.email);
	if(!input
		|| !email
		|| !validPassword(input.password)
		|| !UUID_RE.test(input.install_id ?? "")
		|| !validText(input.secret, 32, 256)
		|| (input.version !== undefined && !validText(input.version, 0, 64)))
		return error(400, "invalid_request", "A valid email, password, install UUID, and device secret are required.");

	const now = Math.floor(Date.now() / 1000);
	const ip = clientIp(request);
	if(ip) {
		const attempts = await env.DB.prepare(
			"SELECT COUNT(*) AS count FROM registration_attempts WHERE ip = ?1 AND created_at > ?2",
		).bind(ip, now - REGISTRATION_WINDOW_SECONDS).first<{count: number}>();
		if((attempts?.count ?? 0) >= REGISTRATION_LIMIT_PER_IP)
			return error(429, "registration_rate_limited", "Too many accounts were registered from this network.");
	}
	const rateKey = await authRateKey("register-email", email, request);
	if(await authRateLimited(env.DB, "register-email", rateKey, now))
		return error(429, "auth_rate_limited", "Too many authentication attempts. Try again later.");
	const duplicate = await env.DB.prepare(
		"SELECT install_id, email_normalized FROM accounts WHERE install_id = ?1 OR email_normalized = ?2 LIMIT 1",
	).bind(input.install_id, email).first<AccountProfileRow>();
	if(duplicate) {
		await recordAuthAttempt(env.DB, "register-email", rateKey, false, now);
		return error(409, duplicate.email_normalized === email ? "email_exists" : "account_exists",
			duplicate.email_normalized === email ? "This email is already registered." : "This install UUID is already registered.");
	}

	const [deviceHash, storedPassword] = await Promise.all([
		secretHash(input.secret, env.ACCOUNT_PEPPER),
		passwordHash(input.password),
	]);
	const statements = [
		env.DB.prepare(
			`INSERT INTO accounts
			 (install_id, secret_hash, email_normalized, password_hash, created_at, last_seen_at,
			  last_player_name, last_client_version, created_ip, last_ip)
			 VALUES (?1, ?2, ?3, ?4, ?5, ?5, '', ?6, ?7, ?7)`,
		).bind(input.install_id, deviceHash, email, storedPassword, now, input.version?.trim() ?? "", ip),
		env.DB.prepare(
			`INSERT INTO account_device_credentials(account_install_id, secret_hash, created_at, last_used_at)
			 VALUES (?1, ?2, ?3, ?3)`,
		).bind(input.install_id, deviceHash, now),
		env.DB.prepare("DELETE FROM registration_attempts WHERE created_at <= ?1").bind(now - REGISTRATION_WINDOW_SECONDS),
	];
	if(ip)
		statements.push(env.DB.prepare("INSERT INTO registration_attempts(ip, created_at) VALUES (?1, ?2)").bind(ip, now));
	await env.DB.batch(statements);
	await recordAuthAttempt(env.DB, "register-email", rateKey, true, now);
	return accountSuccess(env, input.install_id!, now, 201);
}

async function loginEmail(request: Request, env: Env): Promise<Response> {
	const input = await readJson<EmailAccountInput>(request);
	const email = normalizedEmail(input?.email);
	if(!input
		|| !email
		|| !validPassword(input.password)
		|| !validText(input.secret, 32, 256)
		|| (input.version !== undefined && !validText(input.version, 0, 64)))
		return error(400, "invalid_request", "A valid email, password, and device secret are required.");

	const now = Math.floor(Date.now() / 1000);
	const rateKey = await authRateKey("login-email", email, request);
	if(await authRateLimited(env.DB, "login-email", rateKey, now))
		return error(429, "auth_rate_limited", "Too many authentication attempts. Try again later.");
	const account = await env.DB.prepare(
		"SELECT install_id, password_hash FROM accounts WHERE email_normalized = ?1",
	).bind(email).first<{install_id: string; password_hash: string | null}>();
	if(!await passwordMatches(input.password, account?.password_hash ?? null)) {
		await recordAuthAttempt(env.DB, "login-email", rateKey, false, now);
		return error(403, "invalid_credentials", "The email or password is invalid.");
	}

	const ban = await activeBan(env.DB, account!.install_id, now);
	if(ban)
		return json({error: "account_banned", reason: ban.reason, expires_at: ban.expires_at}, 423);
	const deviceHash = await secretHash(input.secret, env.ACCOUNT_PEPPER);
	await env.DB.batch([
		env.DB.prepare(
			`INSERT OR IGNORE INTO account_device_credentials(account_install_id, secret_hash, created_at, last_used_at)
			 VALUES (?1, ?2, ?3, ?3)`,
		).bind(account!.install_id, deviceHash, now),
		env.DB.prepare(
			`UPDATE account_device_credentials SET last_used_at = ?3
			 WHERE account_install_id = ?1 AND secret_hash = ?2`,
		).bind(account!.install_id, deviceHash, now),
		env.DB.prepare(
			`UPDATE accounts
			 SET last_seen_at = ?2,
			     last_client_version = CASE WHEN ?3 = '' THEN last_client_version ELSE ?3 END,
			     last_ip = ?4
			 WHERE install_id = ?1`,
		).bind(account!.install_id, now, input.version?.trim() ?? "", clientIp(request)),
	]);
	await recordAuthAttempt(env.DB, "login-email", rateKey, true, now);
	return accountSuccess(env, account!.install_id, now);
}

async function linkEmail(request: Request, env: Env, installId: string): Promise<Response> {
	const input = await readJson<{email?: string; password?: string}>(request);
	const email = normalizedEmail(input?.email);
	if(!input || !email || !validPassword(input.password))
		return error(400, "invalid_request", "A valid email and password are required.");

	const existing = await env.DB.prepare(
		"SELECT install_id, email_normalized FROM accounts WHERE email_normalized = ?1 OR install_id = ?2 ORDER BY install_id = ?2 DESC",
	).bind(email, installId).all<AccountProfileRow>();
	const account = existing.results.find(row => row.install_id === installId);
	if(account?.email_normalized)
		return error(409, "email_already_linked", "This account already has an email.");
	if(existing.results.some(row => row.install_id !== installId))
		return error(409, "email_exists", "This email is already registered.");

	const storedPassword = await passwordHash(input.password);
	await env.DB.prepare(
		`UPDATE accounts SET email_normalized = ?2, password_hash = ?3
		 WHERE install_id = ?1 AND email_normalized IS NULL`,
	).bind(installId, email, storedPassword).run();
	return accountSuccess(env, installId, Math.floor(Date.now() / 1000));
}

async function authenticate(request: Request, env: Env): Promise<AuthenticatedAccount | Response> {
	const installId = request.headers.get("x-uclient-install-id") ?? "";
	const authorization = request.headers.get("authorization") ?? "";
	const secret = authorization.startsWith("Bearer ") ? authorization.slice(7) : "";
	if(!UUID_RE.test(installId) || !validText(secret, 32, 256))
		return error(401, "authentication_required", "Account credentials are required.");

	const hash = await secretHash(secret, env.ACCOUNT_PEPPER);
	if(!await credentialAccount(env.DB, installId, hash))
		return error(401, "invalid_credentials", "Account credentials are invalid.");

	const ban = await activeBan(env.DB, installId, Math.floor(Date.now() / 1000));
	if(ban)
		return json({error: "account_banned", reason: ban.reason, expires_at: ban.expires_at}, 423);
	return {installId, ban: null};
}

function randomCode(length: number): string {
	const alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
	const bytes = crypto.getRandomValues(new Uint8Array(length));
	return Array.from(bytes, value => alphabet[value % alphabet.length]!).join("");
}

function opaqueId(): string {
	return base64Url(crypto.getRandomValues(new Uint8Array(18)));
}

async function membershipRole(env: Env, roomId: string, installId: string): Promise<string | null> {
	const row = await env.DB.prepare(
		"SELECT role FROM room_members WHERE room_id = ?1 AND install_id = ?2",
	).bind(roomId, installId).first<{role: string}>();
	return row?.role ?? null;
}

async function roomList(env: Env, installId: string): Promise<Response> {
	const rooms = await env.DB.prepare(
		`SELECT r.id, r.name, r.owner_install_id, r.invite_code, r.created_at, r.name_color, r.invite_code_public, m.role AS viewer_role
		 FROM rooms r JOIN room_members m ON m.room_id = r.id
		 WHERE m.install_id = ?1 ORDER BY r.created_at ASC`,
	).bind(installId).all<RoomRow & {viewer_role: string}>();

	const result = await Promise.all(rooms.results.map(async room => {
		const members = await env.DB.prepare(
			`SELECT member_id, display_name, role, joined_at, install_id
			 FROM room_members WHERE room_id = ?1
			 ORDER BY CASE role WHEN 'owner' THEN 0 WHEN 'admin' THEN 1 ELSE 2 END, joined_at ASC`,
		).bind(room.id).all<MemberRow>();
		const isOwner = room.viewer_role === "owner";
		const isAdmin = room.viewer_role === "admin";
		const inviteCodePublic = (room.invite_code_public ?? 0) !== 0;
		const canSeeInvite = isOwner || isAdmin || inviteCodePublic;
		return {
			id: room.id,
			name: room.name,
			invite_code: canSeeInvite ? room.invite_code : "",
			invite_code_public: inviteCodePublic,
			name_color: room.name_color ?? 0,
			is_owner: isOwner,
			is_admin: isAdmin,
			members: members.results.map(member => ({
				member_id: member.member_id,
				display_name: member.display_name,
				role: member.role,
				joined_at: member.joined_at,
				is_self: member.install_id === installId,
			})),
		};
	}));
	return json({rooms: result});
}

async function markRoomChanged(env: Env, roomId: string, now: number): Promise<void> {
	await env.DB.prepare("INSERT INTO room_changes(room_id, changed_at) VALUES (?1, ?2)").bind(roomId, now).run();
}

async function invalidateRelay(env: Env, roomId: string): Promise<void> {
	if(!env.RELAY_INVALIDATE_URL)
		return;
	const response = await fetch(env.RELAY_INVALIDATE_URL, {
		method: "POST",
		headers: {
			"content-type": "application/json",
			"authorization": `Bearer ${env.RELAY_INVALIDATE_SECRET ?? env.RELAY_SECRET}`,
		},
		body: JSON.stringify({room_id: roomId}),
	});
	if(!response.ok)
		console.error(JSON.stringify({event: "relay_invalidate_failed", room_id: roomId, status: response.status}));
}

async function createRoom(request: Request, env: Env, installId: string, ctx: ExecutionContext): Promise<Response> {
	const input = await readJson<{name?: string; display_name?: string}>(request);
	if(!input || !validText(input.name, 1, 48) || !validText(input.display_name, 1, 64))
		return error(400, "invalid_request", "Room name and display name are required.");
	const ownedRooms = await env.DB.prepare("SELECT COUNT(*) AS count FROM rooms WHERE owner_install_id = ?1")
		.bind(installId).first<{count: number}>();
	if((ownedRooms?.count ?? 0) >= MAX_OWNED_ROOMS)
		return error(409, "room_limit_reached", "You can create up to 5 chat rooms.");
	const now = Math.floor(Date.now() / 1000);
	const roomId = crypto.randomUUID();
	const memberId = opaqueId();

	for(let attempt = 0; attempt < 5; attempt++) {
		const inviteCode = randomCode(10);
		try {
			await env.DB.batch([
				env.DB.prepare(
					"INSERT INTO rooms(id, name, owner_install_id, invite_code, created_at, name_color, invite_code_public) VALUES (?1, ?2, ?3, ?4, ?5, 0, 0)",
				).bind(roomId, input.name.trim(), installId, inviteCode, now),
				env.DB.prepare(
					`INSERT INTO room_members(room_id, install_id, member_id, display_name, role, joined_at)
					 VALUES (?1, ?2, ?3, ?4, 'owner', ?5)`,
				).bind(roomId, installId, memberId, input.display_name.trim(), now),
			]);
			await markRoomChanged(env, roomId, now);
			ctx.waitUntil(invalidateRelay(env, roomId));
			return json({id: roomId, name: input.name.trim(), invite_code: inviteCode}, 201);
		}
		catch(errorValue) {
			if(attempt === 4)
				throw errorValue;
		}
	}
	return error(500, "room_create_failed", "The room could not be created.");
}

async function roomForOwner(env: Env, roomId: string, installId: string): Promise<RoomRow | null> {
	return await env.DB.prepare("SELECT * FROM rooms WHERE id = ?1 AND owner_install_id = ?2")
		.bind(roomId, installId).first<RoomRow>();
}

async function renameRoom(request: Request, env: Env, roomId: string, installId: string, ctx: ExecutionContext): Promise<Response> {
	if(!ROOM_ID_RE.test(roomId))
		return error(404, "room_not_found", "Room not found.");
	const input = await readJson<{name?: string; name_color?: number; invite_code_public?: boolean}>(request);
	if(!input)
		return error(400, "invalid_request", "A valid room update is required.");
	const hasName = Object.prototype.hasOwnProperty.call(input, "name");
	const hasColor = Object.prototype.hasOwnProperty.call(input, "name_color");
	const hasInvitePublic = Object.prototype.hasOwnProperty.call(input, "invite_code_public");
	if(!hasName && !hasColor && !hasInvitePublic)
		return error(400, "invalid_request", "A valid room update is required.");
	if(hasName && !validText(input.name, 1, 48))
		return error(400, "invalid_request", "A valid room name is required.");
	if(hasColor && (!Number.isInteger(input.name_color) || (input.name_color as number) < 0 || (input.name_color as number) > 0xffffffff))
		return error(400, "invalid_request", "A valid room name color is required.");
	if(hasInvitePublic && typeof input.invite_code_public !== "boolean")
		return error(400, "invalid_request", "invite_code_public must be a boolean.");

	const role = await membershipRole(env, roomId, installId);
	if(role !== "owner" && role !== "admin")
		return error(403, "permission_denied", "Only the room owner or an admin can update this room.");
	if(hasInvitePublic && role !== "owner")
		return error(403, "owner_required", "Only the room owner can change invite visibility.");

	const now = Math.floor(Date.now() / 1000);
	if(hasName)
		await env.DB.prepare("UPDATE rooms SET name = ?2 WHERE id = ?1").bind(roomId, input.name!.trim()).run();
	if(hasColor)
		await env.DB.prepare("UPDATE rooms SET name_color = ?2 WHERE id = ?1").bind(roomId, input.name_color).run();
	if(hasInvitePublic)
		await env.DB.prepare("UPDATE rooms SET invite_code_public = ?2 WHERE id = ?1").bind(roomId, input.invite_code_public ? 1 : 0).run();
	await markRoomChanged(env, roomId, now);
	ctx.waitUntil(invalidateRelay(env, roomId));
	return json({ok: true});
}

async function regenerateInvite(env: Env, roomId: string, installId: string): Promise<Response> {
	if(!await roomForOwner(env, roomId, installId))
		return error(403, "owner_required", "Only the room owner can regenerate the invite code.");
	for(let attempt = 0; attempt < 5; attempt++) {
		const inviteCode = randomCode(10);
		try {
			await env.DB.prepare("UPDATE rooms SET invite_code = ?2 WHERE id = ?1").bind(roomId, inviteCode).run();
			return json({invite_code: inviteCode});
		}
		catch(errorValue) {
			if(attempt === 4)
				throw errorValue;
		}
	}
	return error(500, "invite_create_failed", "The invite code could not be regenerated.");
}

async function transferOwnership(request: Request, env: Env, roomId: string, installId: string, ctx: ExecutionContext): Promise<Response> {
	if(!ROOM_ID_RE.test(roomId))
		return error(404, "room_not_found", "Room not found.");
	const input = await readJson<{member_id?: string}>(request);
	if(!input || !validText(input.member_id, 20, 64) || !MEMBER_ID_RE.test(input.member_id))
		return error(400, "invalid_request", "A valid member id is required.");
	if(!await roomForOwner(env, roomId, installId))
		return error(403, "owner_required", "Only the room owner can transfer ownership.");

	const target = await env.DB.prepare(
		"SELECT install_id, role FROM room_members WHERE room_id = ?1 AND member_id = ?2",
	).bind(roomId, input.member_id).first<{install_id: string; role: string}>();
	if(!target)
		return error(404, "member_not_found", "Room member not found.");
	if(target.role === "owner")
		return error(400, "already_owner", "That member is already the room owner.");

	const ownedRooms = await env.DB.prepare("SELECT COUNT(*) AS count FROM rooms WHERE owner_install_id = ?1")
		.bind(target.install_id).first<{count: number}>();
	if((ownedRooms?.count ?? 0) >= MAX_OWNED_ROOMS)
		return error(409, "room_limit_reached", "That member already owns the maximum number of chat rooms.");

	const now = Math.floor(Date.now() / 1000);
	await env.DB.batch([
		env.DB.prepare("UPDATE rooms SET owner_install_id = ?2 WHERE id = ?1").bind(roomId, target.install_id),
		env.DB.prepare("UPDATE room_members SET role = 'member' WHERE room_id = ?1 AND install_id = ?2").bind(roomId, installId),
		env.DB.prepare("UPDATE room_members SET role = 'owner' WHERE room_id = ?1 AND member_id = ?2").bind(roomId, input.member_id),
	]);
	await markRoomChanged(env, roomId, now);
	ctx.waitUntil(invalidateRelay(env, roomId));
	return json({ok: true});
}

async function setMemberRole(request: Request, env: Env, roomId: string, memberId: string, installId: string, ctx: ExecutionContext): Promise<Response> {
	if(!ROOM_ID_RE.test(roomId) || !MEMBER_ID_RE.test(memberId))
		return error(404, "not_found", "Room or member not found.");
	const input = await readJson<{role?: string}>(request);
	if(!input || (input.role !== "admin" && input.role !== "member"))
		return error(400, "invalid_request", "Role must be admin or member.");
	if(!await roomForOwner(env, roomId, installId))
		return error(403, "owner_required", "Only the room owner can change admin status.");

	const target = await env.DB.prepare(
		"SELECT role FROM room_members WHERE room_id = ?1 AND member_id = ?2",
	).bind(roomId, memberId).first<{role: string}>();
	if(!target)
		return error(404, "member_not_found", "Room member not found.");
	if(target.role === "owner")
		return error(400, "cannot_change_owner_role", "The room owner role cannot be changed this way.");

	const now = Math.floor(Date.now() / 1000);
	await env.DB.prepare("UPDATE room_members SET role = ?3 WHERE room_id = ?1 AND member_id = ?2")
		.bind(roomId, memberId, input.role).run();
	await markRoomChanged(env, roomId, now);
	ctx.waitUntil(invalidateRelay(env, roomId));
	return json({ok: true, role: input.role});
}

async function joinRoom(request: Request, env: Env, installId: string, ctx: ExecutionContext): Promise<Response> {
	const input = await readJson<{code?: string; display_name?: string}>(request);
	if(!input || !validText(input.code, 6, 32) || !validText(input.display_name, 1, 64))
		return error(400, "invalid_request", "Invite code and display name are required.");
	const room = await env.DB.prepare("SELECT * FROM rooms WHERE invite_code = ?1")
		.bind(input.code.trim().toUpperCase()).first<RoomRow>();
	if(!room)
		return error(404, "invalid_invite", "The invite code is invalid.");
	const existing = await env.DB.prepare("SELECT 1 FROM room_members WHERE room_id = ?1 AND install_id = ?2")
		.bind(room.id, installId).first();
	if(existing)
		return error(409, "already_member", "You are already a member of this room.");
	const now = Math.floor(Date.now() / 1000);
	await env.DB.prepare(
		`INSERT INTO room_members(room_id, install_id, member_id, display_name, role, joined_at)
		 VALUES (?1, ?2, ?3, ?4, 'member', ?5)`,
	).bind(room.id, installId, opaqueId(), input.display_name.trim(), now).run();
	await markRoomChanged(env, room.id, now);
	ctx.waitUntil(invalidateRelay(env, room.id));
	return json({id: room.id, name: room.name}, 201);
}

async function kickMember(env: Env, roomId: string, memberId: string, installId: string, ctx: ExecutionContext): Promise<Response> {
	if(!MEMBER_ID_RE.test(memberId))
		return error(403, "permission_denied", "You do not have permission to remove members.");
	const actorRole = await membershipRole(env, roomId, installId);
	if(actorRole !== "owner" && actorRole !== "admin")
		return error(403, "permission_denied", "Only the room owner or an admin can remove members.");
	const member = await env.DB.prepare("SELECT role FROM room_members WHERE room_id = ?1 AND member_id = ?2")
		.bind(roomId, memberId).first<{role: string}>();
	if(!member)
		return error(404, "member_not_found", "Room member not found.");
	if(member.role === "owner")
		return error(400, "cannot_remove_owner", "The room owner cannot be removed.");
	if(actorRole === "admin" && member.role === "admin")
		return error(403, "permission_denied", "Admins cannot remove other admins.");
	await env.DB.prepare("DELETE FROM room_members WHERE room_id = ?1 AND member_id = ?2").bind(roomId, memberId).run();
	await markRoomChanged(env, roomId, Math.floor(Date.now() / 1000));
	ctx.waitUntil(invalidateRelay(env, roomId));
	return json({ok: true});
}

async function leaveRoom(env: Env, roomId: string, installId: string, ctx: ExecutionContext): Promise<Response> {
	const room = await env.DB.prepare("SELECT owner_install_id FROM rooms WHERE id = ?1").bind(roomId).first<{owner_install_id: string}>();
	if(!room)
		return error(404, "room_not_found", "Room not found.");
	if(room.owner_install_id === installId) {
		await env.DB.prepare("DELETE FROM rooms WHERE id = ?1").bind(roomId).run();
	}
	else {
		const result = await env.DB.prepare("DELETE FROM room_members WHERE room_id = ?1 AND install_id = ?2")
			.bind(roomId, installId).run();
		if(!result.meta.changes)
			return error(404, "membership_not_found", "You are not a member of this room.");
	}
	await markRoomChanged(env, roomId, Math.floor(Date.now() / 1000));
	ctx.waitUntil(invalidateRelay(env, roomId));
	return json({ok: true, room_deleted: room.owner_install_id === installId});
}

async function relayAuthorized(request: Request, env: Env): Promise<boolean> {
	const authorization = request.headers.get("authorization") ?? "";
	const provided = authorization.startsWith("Bearer ") ? authorization.slice(7) : "";
	if(!provided || !env.RELAY_SECRET)
		return false;
	const [providedHash, expectedHash] = await Promise.all([
		crypto.subtle.digest("SHA-256", new TextEncoder().encode(provided)),
		crypto.subtle.digest("SHA-256", new TextEncoder().encode(env.RELAY_SECRET)),
	]);
	return timingSafeEqual(bytesToHex(new Uint8Array(providedHash)), bytesToHex(new Uint8Array(expectedHash)));
}

async function internalMemberships(request: Request, env: Env): Promise<Response> {
	if(!await relayAuthorized(request, env))
		return error(401, "invalid_relay_secret", "Relay authentication failed.");
	const url = new URL(request.url);
	const since = Math.max(0, Number(url.searchParams.get("since") ?? "0") || 0);
	const sequenceRow = await env.DB.prepare("SELECT COALESCE(MAX(sequence), 0) AS sequence FROM room_changes")
		.first<{sequence: number}>();
	let roomIds: string[] | null = null;
	if(since > 0) {
		const changed = await env.DB.prepare("SELECT DISTINCT room_id FROM room_changes WHERE sequence > ?1")
			.bind(since).all<{room_id: string}>();
		roomIds = changed.results.map(row => row.room_id);
	}
	const rooms = await env.DB.prepare("SELECT id, name, name_color FROM rooms ORDER BY id").all<{id: string; name: string; name_color: number | null}>();
	const filtered = roomIds === null ? rooms.results : rooms.results.filter(room => roomIds.includes(room.id));
	const memberships = await Promise.all(filtered.map(async room => {
		const members = await env.DB.prepare("SELECT install_id FROM room_members WHERE room_id = ?1")
			.bind(room.id).all<{install_id: string}>();
		return {
			room_id: room.id,
			room_name: room.name,
			name_color: room.name_color ?? 0,
			install_ids: members.results.map(member => member.install_id),
		};
	}));
	if(roomIds !== null) {
		for(const deletedId of roomIds.filter(id => !filtered.some(room => room.id === id)))
			memberships.push({room_id: deletedId, room_name: "", name_color: 0, install_ids: []});
	}
	return json({sequence: sequenceRow?.sequence ?? 0, rooms: memberships});
}

async function internalBans(request: Request, env: Env): Promise<Response> {
	if(!await relayAuthorized(request, env))
		return error(401, "invalid_relay_secret", "Relay authentication failed.");
	const now = Math.floor(Date.now() / 1000);
	const bans = await env.DB.prepare(
		"SELECT DISTINCT install_id FROM user_bans WHERE expires_at IS NULL OR expires_at > ?1",
	).bind(now).all<{install_id: string}>();
	return json({install_ids: bans.results.map(row => row.install_id)});
}

function backupExtension(path: string): string {
	const slash = path.lastIndexOf("/");
	const dot = path.lastIndexOf(".");
	return dot > slash ? path.slice(dot).toLowerCase() : "";
}

function backupPath(request: Request): string | null {
	const encoded = request.headers.get("x-uclient-path");
	if(!encoded)
		return null;
	try {
		const decoded = decodeURIComponent(encoded).replace(/\\/g, "/");
		if(decoded.includes("\0") || decoded.startsWith("/") || /^[A-Za-z]:/.test(decoded))
			return null;
		const parts = decoded.split("/");
		if(parts.some(part =>
			part === ".."
			|| /[\x00-\x1f<>:"|?*]/.test(part)
			|| part.endsWith(".")
			|| part.endsWith(" ")))
			return null;
		const normalized = parts.filter(part => part !== "" && part !== ".").join("/");
		if(!normalized || normalized.length > 1024 || !BACKUP_EXTENSIONS.has(backupExtension(normalized)))
			return null;
		if(parts.some(part => {
			const lower = part.toLowerCase();
			return lower === "dumps" || lower === "downloadedskins"
				|| lower === "communityicons" || lower === "communityicsons";
		}))
			return null;
		if(normalized.split("/").some(part => SENSITIVE_BACKUP_NAMES.has(part.toLowerCase())))
			return null;
		return normalized;
	}
	catch {
		return null;
	}
}

function hasPngSignature(bytes: Uint8Array): boolean {
	const signature = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];
	return bytes.length >= signature.length && signature.every((value, index) => bytes[index] === value);
}

function hasJpegSignature(bytes: Uint8Array): boolean {
	return bytes.length >= 4
		&& bytes[0] === 0xff && bytes[1] === 0xd8 && bytes[2] === 0xff
		&& bytes[bytes.length - 2] === 0xff && bytes[bytes.length - 1] === 0xd9;
}

function validTextBackup(bytes: Uint8Array): boolean {
	for(const byte of bytes) {
		if(byte < 0x20 && byte !== 0x09 && byte !== 0x0a && byte !== 0x0d)
			return false;
	}
	let text: string;
	try {
		text = new TextDecoder("utf-8", {fatal: true, ignoreBOM: false}).decode(bytes);
	}
	catch {
		return false;
	}
	const trimmed = text.trim();
	if(!trimmed.startsWith("{") || !trimmed.endsWith("}"))
		return true;
	try {
		const parsed = JSON.parse(trimmed);
		if(!parsed || typeof parsed !== "object" || Array.isArray(parsed))
			return true;
		const keys = new Set(Object.keys(parsed as Record<string, unknown>).map(key => key.toLowerCase()));
		return !(keys.has("install_id") && (keys.has("secret") || keys.has("grace_token")));
	}
	catch {
		return true;
	}
}

function validBackupBody(path: string, body: ArrayBuffer): boolean {
	const bytes = new Uint8Array(body);
	switch(backupExtension(path)) {
	case ".png":
		return hasPngSignature(bytes);
	case ".jpg":
	case ".jpeg":
		return hasJpegSignature(bytes);
	case ".cfg":
	case ".txt":
	case ".log":
		return validTextBackup(bytes);
	default:
		return false;
	}
}

async function readBackupBody(request: Request): Promise<ArrayBuffer | null> {
	if(!request.body)
		return new ArrayBuffer(0);
	const reader = request.body.getReader();
	const chunks: Uint8Array[] = [];
	let total = 0;
	try {
		while(true) {
			const {done, value} = await reader.read();
			if(done)
				break;
			total += value.byteLength;
			if(total > BACKUP_OBJECT_LIMIT_BYTES) {
				await reader.cancel();
				return null;
			}
			chunks.push(value);
		}
	}
	finally {
		reader.releaseLock();
	}
	const body = new Uint8Array(total);
	let offset = 0;
	for(const chunk of chunks) {
		body.set(chunk, offset);
		offset += chunk.byteLength;
	}
	return body.buffer;
}

async function backupUsage(db: D1Database, installId: string): Promise<number> {
	const row = await db.prepare(
		"SELECT COALESCE(SUM(size_bytes), 0) AS used_bytes FROM cfg_backup_versions WHERE account_install_id = ?1",
	).bind(installId).first<{used_bytes: number}>();
	return row?.used_bytes ?? 0;
}

async function uploadBackup(request: Request, env: Env, installId: string): Promise<Response> {
	const path = backupPath(request);
	if(!path)
		return error(400, "invalid_backup_path", "x-uclient-path must contain an allowed relative backup path.");
	const contentLength = Number(request.headers.get("content-length") ?? "0");
	if(Number.isFinite(contentLength) && contentLength > BACKUP_OBJECT_LIMIT_BYTES)
		return error(413, "backup_too_large", "A backup object cannot exceed 10 MB.");

	const body = await readBackupBody(request);
	if(!body)
		return error(413, "backup_too_large", "A backup object cannot exceed 10 MB.");
	if(!validBackupBody(path, body))
		return error(400, "invalid_backup_content", "The file contents do not match an allowed backup type or contain account credentials.");
	const usedBytes = await backupUsage(env.DB, installId);
	if(usedBytes + body.byteLength > BACKUP_ACCOUNT_QUOTA_BYTES)
		return error(413, "backup_quota_exceeded", "This account's 10 MB backup quota would be exceeded.");

	const now = Math.floor(Date.now() / 1000);
	const id = crypto.randomUUID();
	const objectKey = `accounts/${installId}/${now}-${id}`;
	const sha256 = bytesToHex(new Uint8Array(await crypto.subtle.digest("SHA-256", body)));
	await env.CFG_BACKUPS.put(objectKey, body, {
		httpMetadata: {contentType: "application/octet-stream"},
		customMetadata: {
			backupId: id,
			accountInstallId: installId,
			relativePath: path,
			sha256,
		},
	});
	try {
		const inserted = await env.DB.prepare(
			`INSERT INTO cfg_backup_versions
			 (id, account_install_id, relative_path, object_key, created_at, size_bytes, sha256)
			 SELECT ?1, ?2, ?3, ?4, ?5, ?6, ?7
			 WHERE ?6 + (
			 	SELECT COALESCE(SUM(size_bytes), 0)
			 	FROM cfg_backup_versions WHERE account_install_id = ?2
			 ) <= ?8`,
		).bind(id, installId, path, objectKey, now, body.byteLength, sha256, BACKUP_ACCOUNT_QUOTA_BYTES).run();
		if(!inserted.meta.changes) {
			await env.CFG_BACKUPS.delete(objectKey);
			return error(413, "backup_quota_exceeded", "This account's 10 MB backup quota would be exceeded.");
		}
	}
	catch(errorValue) {
		await env.CFG_BACKUPS.delete(objectKey);
		throw errorValue;
	}
	const currentUsedBytes = await backupUsage(env.DB, installId);
	return json({
		version: {id, relative_path: path, created_at: now, size_bytes: body.byteLength, sha256},
		used_bytes: currentUsedBytes,
		quota_bytes: BACKUP_ACCOUNT_QUOTA_BYTES,
	}, 201);
}

async function listBackups(env: Env, installId: string): Promise<Response> {
	const versions = await env.DB.prepare(
		`SELECT id, relative_path, created_at, size_bytes, sha256
		 FROM cfg_backup_versions WHERE account_install_id = ?1
		 ORDER BY created_at DESC, id DESC`,
	).bind(installId).all<Pick<BackupRow, "id" | "relative_path" | "created_at" | "size_bytes" | "sha256">>();
	const usedBytes = versions.results.reduce((sum, version) => sum + version.size_bytes, 0);
	return json({versions: versions.results, used_bytes: usedBytes, quota_bytes: BACKUP_ACCOUNT_QUOTA_BYTES});
}

async function backupForAccount(env: Env, installId: string, id: string): Promise<BackupRow | null> {
	if(!UUID_RE.test(id))
		return null;
	return await env.DB.prepare(
		`SELECT id, account_install_id, relative_path, object_key, created_at, size_bytes, sha256
		 FROM cfg_backup_versions WHERE id = ?1 AND account_install_id = ?2`,
	).bind(id, installId).first<BackupRow>();
}

async function downloadBackup(env: Env, installId: string, id: string): Promise<Response> {
	const version = await backupForAccount(env, installId, id);
	if(!version)
		return error(404, "backup_not_found", "Backup version not found.");
	const object = await env.CFG_BACKUPS.get(version.object_key);
	if(!object) {
		await env.DB.prepare(
			"DELETE FROM cfg_backup_versions WHERE id = ?1 AND account_install_id = ?2",
		).bind(id, installId).run();
		return error(410, "backup_object_missing", "The backup object is unavailable and its stale metadata was removed.");
	}
	return new Response(object.body, {
		headers: {
			"content-type": object.httpMetadata?.contentType ?? "application/octet-stream",
			"content-length": String(version.size_bytes),
			"cache-control": "private, no-store",
			"x-uclient-backup-id": version.id,
			"x-uclient-path": encodeURIComponent(version.relative_path),
			"x-uclient-created-at": String(version.created_at),
			"x-uclient-sha256": version.sha256,
		},
	});
}

async function deleteBackup(env: Env, installId: string, id: string): Promise<Response> {
	const version = await backupForAccount(env, installId, id);
	if(!version)
		return error(404, "backup_not_found", "Backup version not found.");
	const object = await env.CFG_BACKUPS.get(version.object_key);
	if(!object) {
		await env.DB.prepare(
			"DELETE FROM cfg_backup_versions WHERE id = ?1 AND account_install_id = ?2",
		).bind(id, installId).run();
		return json({ok: true, object_missing: true});
	}
	const body = await object.arrayBuffer();
	await env.CFG_BACKUPS.delete(version.object_key);
	try {
		await env.DB.prepare(
			"DELETE FROM cfg_backup_versions WHERE id = ?1 AND account_install_id = ?2",
		).bind(id, installId).run();
	}
	catch(errorValue) {
		await env.CFG_BACKUPS.put(version.object_key, body, {
			httpMetadata: object.httpMetadata,
			customMetadata: object.customMetadata,
		});
		throw errorValue;
	}
	return json({ok: true});
}

async function handleBackups(request: Request, env: Env, segments: string[]): Promise<Response> {
	const authenticated = await authenticate(request, env);
	if(authenticated instanceof Response)
		return authenticated;
	if(segments.length === 2 && request.method === "PUT")
		return uploadBackup(request, env, authenticated.installId);
	if(segments.length === 2 && request.method === "GET")
		return listBackups(env, authenticated.installId);
	if(segments.length === 3 && request.method === "GET")
		return downloadBackup(env, authenticated.installId, segments[2]!);
	if(segments.length === 3 && request.method === "DELETE")
		return deleteBackup(env, authenticated.installId, segments[2]!);
	return error(404, "not_found", "Endpoint not found.");
}

async function handleRooms(request: Request, env: Env, ctx: ExecutionContext, segments: string[]): Promise<Response> {
	const authenticated = await authenticate(request, env);
	if(authenticated instanceof Response)
		return authenticated;
	const method = request.method;

	if(segments.length === 1 && method === "GET")
		return roomList(env, authenticated.installId);
	if(segments.length === 1 && method === "POST")
		return createRoom(request, env, authenticated.installId, ctx);
	if(segments.length === 2 && segments[1] === "join" && method === "POST")
		return joinRoom(request, env, authenticated.installId, ctx);
	if(segments.length === 2 && (method === "PATCH" || method === "POST"))
		return renameRoom(request, env, segments[1]!, authenticated.installId, ctx);
	if(segments.length === 3 && segments[2] === "invite-code" && method === "POST")
		return regenerateInvite(env, segments[1]!, authenticated.installId);
	if(segments.length === 3 && segments[2] === "transfer" && method === "POST")
		return transferOwnership(request, env, segments[1]!, authenticated.installId, ctx);
	if(segments.length === 5 && segments[2] === "members" && segments[4] === "role" && method === "POST")
		return setMemberRole(request, env, segments[1]!, segments[3]!, authenticated.installId, ctx);
	if(segments.length === 4 && segments[2] === "members" && segments[3] === "me" && method === "DELETE")
		return leaveRoom(env, segments[1]!, authenticated.installId, ctx);
	if(segments.length === 4 && segments[2] === "members" && method === "DELETE")
		return kickMember(env, segments[1]!, segments[3]!, authenticated.installId, ctx);
	return error(404, "not_found", "Endpoint not found.");
}

export default {
	async fetch(request: Request, env: Env, ctx: ExecutionContext): Promise<Response> {
		try {
			const url = new URL(request.url);
			const segments = url.pathname.split("/").filter(Boolean);

			if(request.method === "GET" && url.pathname === "/healthz")
				return json({ok: true});
			if(request.method === "POST" && url.pathname === "/account/register")
				return register(request, env);
			if(request.method === "POST" && url.pathname === "/account/verify")
				return verify(request, env);
			if(request.method === "POST" && url.pathname === "/account/register-email")
				return registerEmail(request, env);
			if(request.method === "POST" && url.pathname === "/account/login-email")
				return loginEmail(request, env);
			if(request.method === "POST" && url.pathname === "/account/link-email") {
				const authenticated = await authenticate(request, env);
				if(authenticated instanceof Response)
					return authenticated;
				return linkEmail(request, env, authenticated.installId);
			}
			if(request.method === "GET" && url.pathname === "/account/profile") {
				const authenticated = await authenticate(request, env);
				if(authenticated instanceof Response)
					return authenticated;
				return accountSuccess(env, authenticated.installId, Math.floor(Date.now() / 1000));
			}
			if(url.pathname === "/internal/memberships" && request.method === "GET")
				return internalMemberships(request, env);
			if(url.pathname === "/internal/bans" && request.method === "GET")
				return internalBans(request, env);
			if(request.method === "GET" && url.pathname === "/launcher/notices")
				return publicNotices(env);
			if(request.method === "GET" && url.pathname === "/admin")
			{
				if(env.ASSETS)
					return env.ASSETS.fetch(new URL("/admin.html", request.url));
				return error(404, "not_found", "Admin page is not available.");
			}
			if(segments[0] === "admin" && segments[1] === "notices")
			{
				if(segments.length === 2 && request.method === "GET")
					return adminListNotices(request, env);
				if(segments.length === 2 && request.method === "POST")
					return adminCreateNotice(request, env);
				if(segments.length === 3 && request.method === "PATCH")
					return adminUpdateNotice(request, env, segments[2]!);
				if(segments.length === 3 && request.method === "DELETE")
					return adminDeleteNotice(request, env, segments[2]!);
			}
			if(segments[0] === "backups" && segments[1] === "cfg")
				return handleBackups(request, env, segments);
			if(segments[0] === "rooms")
				return handleRooms(request, env, ctx, segments);
			return error(404, "not_found", "Endpoint not found.");
		}
		catch(errorValue) {
			console.error(JSON.stringify({
				event: "request_failed",
				message: errorValue instanceof Error ? errorValue.message : String(errorValue),
			}));
			return error(500, "internal_error", "The request could not be completed.");
		}
	},
} satisfies ExportedHandler<Env>;
