interface Env extends Cloudflare.Env {
	ACCOUNT_PEPPER: string;
	GRACE_PRIVATE_KEY_SEED_HEX: string;
	RELAY_SECRET: string;
	RELAY_INVALIDATE_SECRET?: string;
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
const GRACE_SECONDS = 7 * 24 * 60 * 60;
const IP_RETENTION_SECONDS = 90 * 24 * 60 * 60;
const MAX_OWNED_ROOMS = 5;

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
	const existing = await env.DB.prepare("SELECT secret_hash FROM accounts WHERE install_id = ?1")
		.bind(input.install_id).first<{secret_hash: string}>();
	if(existing) {
		if(!timingSafeEqual(existing.secret_hash, hash))
			return error(409, "account_exists", "This install UUID is already registered.");
		const ban = await activeBan(env.DB, input.install_id, now);
		if(ban)
			return json({error: "account_banned", reason: ban.reason, expires_at: ban.expires_at}, 423);
		await env.DB.prepare(
			`UPDATE accounts
			 SET last_seen_at = ?2, last_player_name = ?3, last_client_version = ?4, last_ip = ?5
			 WHERE install_id = ?1`,
		).bind(input.install_id, now, input.player_name?.trim() ?? "", input.version?.trim() ?? "", ip).run();
		const grace = await signGraceToken(env, input.install_id, now);
		return json({install_id: input.install_id, grace_token: grace.token, grace_expires_at: grace.expires_at});
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

	const grace = await signGraceToken(env, input.install_id, now);
	return json({install_id: input.install_id, grace_token: grace.token, grace_expires_at: grace.expires_at}, 201);
}

async function verify(request: Request, env: Env): Promise<Response> {
	const input = await accountInput(request);
	if(!input)
		return error(400, "invalid_request", "Invalid account verification data.");

	const account = await env.DB.prepare("SELECT secret_hash FROM accounts WHERE install_id = ?1")
		.bind(input.install_id).first<{secret_hash: string}>();
	if(!account)
		return error(404, "account_not_found", "This install UUID is not registered.");

	const hash = await secretHash(input.secret, env.ACCOUNT_PEPPER);
	if(!timingSafeEqual(account.secret_hash, hash))
		return error(403, "invalid_credentials", "The account secret is invalid.");

	const now = Math.floor(Date.now() / 1000);
	const ban = await activeBan(env.DB, input.install_id, now);
	if(ban)
		return json({error: "account_banned", reason: ban.reason, expires_at: ban.expires_at}, 423);

	await env.DB.prepare(
		`UPDATE accounts
		 SET last_seen_at = ?2, last_player_name = ?3, last_client_version = ?4, last_ip = ?5
		 WHERE install_id = ?1`,
	).bind(input.install_id, now, input.player_name?.trim() ?? "", input.version?.trim() ?? "", clientIp(request)).run();

	const grace = await signGraceToken(env, input.install_id, now);
	return json({install_id: input.install_id, grace_token: grace.token, grace_expires_at: grace.expires_at});
}

async function authenticate(request: Request, env: Env): Promise<AuthenticatedAccount | Response> {
	const installId = request.headers.get("x-uclient-install-id") ?? "";
	const authorization = request.headers.get("authorization") ?? "";
	const secret = authorization.startsWith("Bearer ") ? authorization.slice(7) : "";
	if(!UUID_RE.test(installId) || !validText(secret, 32, 256))
		return error(401, "authentication_required", "Account credentials are required.");

	const account = await env.DB.prepare("SELECT secret_hash FROM accounts WHERE install_id = ?1")
		.bind(installId).first<{secret_hash: string}>();
	if(!account)
		return error(401, "invalid_credentials", "Account credentials are invalid.");
	const hash = await secretHash(secret, env.ACCOUNT_PEPPER);
	if(!timingSafeEqual(account.secret_hash, hash))
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
	if((hasColor || hasInvitePublic) && role !== "owner")
		return error(403, "owner_required", "Only the room owner can change that setting.");

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
	const rooms = await env.DB.prepare("SELECT id, name FROM rooms ORDER BY id").all<{id: string; name: string}>();
	const filtered = roomIds === null ? rooms.results : rooms.results.filter(room => roomIds.includes(room.id));
	const memberships = await Promise.all(filtered.map(async room => {
		const members = await env.DB.prepare("SELECT install_id FROM room_members WHERE room_id = ?1")
			.bind(room.id).all<{install_id: string}>();
		return {room_id: room.id, room_name: room.name, install_ids: members.results.map(member => member.install_id)};
	}));
	if(roomIds !== null) {
		for(const deletedId of roomIds.filter(id => !filtered.some(room => room.id === id)))
			memberships.push({room_id: deletedId, room_name: "", install_ids: []});
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
			if(url.pathname === "/internal/memberships" && request.method === "GET")
				return internalMemberships(request, env);
			if(url.pathname === "/internal/bans" && request.method === "GET")
				return internalBans(request, env);
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
