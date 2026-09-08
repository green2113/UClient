import {applyD1Migrations, env, SELF} from "cloudflare:test";
import {beforeAll, describe, expect, it} from "vitest";

interface TestEnv extends Cloudflare.Env {
	ACCOUNT_PEPPER: string;
	GRACE_PRIVATE_KEY_SEED_HEX: string;
	RELAY_SECRET: string;
	ADMIN_TOKEN: string;
	TEST_MIGRATIONS: D1Migration[];
}

interface D1Migration {
	name: string;
	queries: string[];
}

const testEnv = env as TestEnv;
const owner = {
	install_id: "11111111-1111-4111-8111-111111111111",
	secret: "owner-secret-with-at-least-32-characters",
};
const member = {
	install_id: "22222222-2222-4222-8222-222222222222",
	secret: "member-secret-with-at-least-32-characters",
};

function jsonRequest(path: string, body: unknown, account?: typeof owner): Request {
	const headers: Record<string, string> = {"content-type": "application/json"};
	if(account) {
		headers.authorization = `Bearer ${account.secret}`;
		headers["x-uclient-install-id"] = account.install_id;
	}
	return new Request(`https://worker.test${path}`, {
		method: "POST",
		headers,
		body: JSON.stringify(body),
	});
}

function authenticatedRequest(path: string, method: string, account: typeof owner): Request {
	return new Request(`https://worker.test${path}`, {
		method,
		headers: {
			authorization: `Bearer ${account.secret}`,
			"x-uclient-install-id": account.install_id,
		},
	});
}

function adminRequest(path: string, method: string, body?: unknown): Request {
	const headers: Record<string, string> = {
		authorization: `Bearer ${testEnv.ADMIN_TOKEN}`,
	};
	if(body !== undefined)
		headers["content-type"] = "application/json";
	return new Request(`https://worker.test${path}`, {
		method,
		headers,
		body: body === undefined ? undefined : JSON.stringify(body),
	});
}

async function responseJson<T>(response: Response): Promise<T> {
	return await response.json<T>();
}

describe("rooms Worker integration", () => {
	beforeAll(async () => {
		await applyD1Migrations(testEnv.DB, testEnv.TEST_MIGRATIONS);
	});

	it("covers account, room, membership, leave, and ban flows", async () => {
		for(const [account, playerName] of [[owner, "Owner"], [member, "Member"]] as const) {
			const registerResponse = await SELF.fetch(jsonRequest("/account/register", {
				...account,
				player_name: playerName,
				version: "test",
			}));
			expect(registerResponse.status).toBe(201);
			expect((await responseJson<{grace_token: string}>(registerResponse)).grace_token).toContain(".");

			const verifyResponse = await SELF.fetch(jsonRequest("/account/verify", account));
			expect(verifyResponse.status).toBe(200);
		}

		const retryRegisterResponse = await SELF.fetch(jsonRequest("/account/register", owner));
		expect(retryRegisterResponse.status).toBe(200);
		expect((await responseJson<{grace_token: string}>(retryRegisterResponse)).grace_token).toContain(".");
		const ownerVersion = await testEnv.DB.prepare(
			"SELECT last_client_version FROM accounts WHERE install_id = ?1",
		).bind(owner.install_id).first<{last_client_version: string}>();
		expect(ownerVersion?.last_client_version).toBe("test");
		const conflictingRegisterResponse = await SELF.fetch(jsonRequest("/account/register", {
			install_id: owner.install_id,
			secret: "different-secret-with-at-least-32-characters",
		}));
		expect(conflictingRegisterResponse.status).toBe(409);

		const createResponse = await SELF.fetch(jsonRequest("/rooms", {
			name: "Integration Room",
			display_name: "Owner",
		}, owner));
		expect(createResponse.status).toBe(201);
		const created = await responseJson<{id: string; invite_code: string}>(createResponse);

		const joinResponse = await SELF.fetch(jsonRequest("/rooms/join", {
			code: created.invite_code,
			display_name: "Member",
		}, member));
		expect(joinResponse.status).toBe(201);

		const ownerRoomsResponse = await SELF.fetch(authenticatedRequest("/rooms", "GET", owner));
		const ownerRooms = await responseJson<{
			rooms: Array<{id: string; members: Array<{member_id: string; display_name: string; role: string}>}>;
		}>(ownerRoomsResponse);
		expect(ownerRooms.rooms).toHaveLength(1);
		expect(ownerRooms.rooms[0]?.members.map(value => value.role)).toEqual(["owner", "member"]);
		const memberId = ownerRooms.rooms[0]?.members.find(value => value.display_name === "Member")?.member_id;
		expect(memberId).toBeTruthy();

		const promoteResponse = await SELF.fetch(jsonRequest(
			`/rooms/${created.id}/members/${memberId}/role`,
			{role: "admin"},
			owner,
		));
		expect(promoteResponse.status).toBe(200);

		const colorAsAdminResponse = await SELF.fetch(jsonRequest(`/rooms/${created.id}`, {
			name_color: 123456,
		}, member));
		expect(colorAsAdminResponse.status).toBe(200);

		const colorAsOwnerResponse = await SELF.fetch(jsonRequest(`/rooms/${created.id}`, {
			name_color: 123456,
		}, owner));
		expect(colorAsOwnerResponse.status).toBe(200);

		const adminRoomsResponse = await SELF.fetch(authenticatedRequest("/rooms", "GET", member));
		const adminRooms = await responseJson<{
			rooms: Array<{is_owner: boolean; is_admin: boolean; invite_code: string}>;
		}>(adminRoomsResponse);
		expect(adminRooms.rooms[0]?.is_admin).toBe(true);
		expect(adminRooms.rooms[0]?.is_owner).toBe(false);
		expect(adminRooms.rooms[0]?.invite_code).toBe(created.invite_code);

		const renameAsAdminResponse = await SELF.fetch(jsonRequest(`/rooms/${created.id}`, {
			name: "Admin Renamed",
		}, member));
		expect(renameAsAdminResponse.status).toBe(200);

		const regenerateAsAdminResponse = await SELF.fetch(jsonRequest(
			`/rooms/${created.id}/invite-code`,
			{},
			member,
		));
		expect(regenerateAsAdminResponse.status).toBe(403);

		const demoteResponse = await SELF.fetch(jsonRequest(
			`/rooms/${created.id}/members/${memberId}/role`,
			{role: "member"},
			owner,
		));
		expect(demoteResponse.status).toBe(200);

		const transferResponse = await SELF.fetch(jsonRequest(`/rooms/${created.id}/transfer`, {
			member_id: memberId,
		}, owner));
		expect(transferResponse.status).toBe(200);

		const transferredRoomsResponse = await SELF.fetch(authenticatedRequest("/rooms", "GET", member));
		const transferredRooms = await responseJson<{
			rooms: Array<{is_owner: boolean; is_admin: boolean}>;
		}>(transferredRoomsResponse);
		expect(transferredRooms.rooms[0]?.is_owner).toBe(true);

		const kickOldOwnerResponse = await SELF.fetch(authenticatedRequest(
			`/rooms/${created.id}/members/${ownerRooms.rooms[0]?.members.find(value => value.display_name === "Owner")?.member_id}`,
			"DELETE",
			member,
		));
		expect(kickOldOwnerResponse.status).toBe(200);

		const recreateAsOwnerResponse = await SELF.fetch(jsonRequest("/rooms", {
			name: "Integration Room 2",
			display_name: "Owner",
		}, owner));
		expect(recreateAsOwnerResponse.status).toBe(201);
		const recreated = await responseJson<{id: string; invite_code: string}>(recreateAsOwnerResponse);
		const rejoinResponse = await SELF.fetch(jsonRequest("/rooms/join", {
			code: recreated.invite_code,
			display_name: "Member",
		}, member));
		expect(rejoinResponse.status).toBe(201);

		const membershipsResponse = await SELF.fetch(new Request("https://worker.test/internal/memberships", {
			headers: {authorization: `Bearer ${testEnv.RELAY_SECRET}`},
		}));
		expect(membershipsResponse.status).toBe(200);
		const memberships = await responseJson<{
			sequence: number;
			rooms: Array<{room_id: string; room_name: string; name_color: number; install_ids: string[]}>;
		}>(membershipsResponse);
		expect(memberships.sequence).toBeGreaterThan(0);
		expect(memberships.rooms).toContainEqual({
			room_id: recreated.id,
			room_name: "Integration Room 2",
			name_color: 0,
			install_ids: expect.arrayContaining([owner.install_id, member.install_id]),
		});

		const recreatedRoomsResponse = await SELF.fetch(authenticatedRequest("/rooms", "GET", owner));
		const recreatedRooms = await responseJson<{
			rooms: Array<{id: string; members: Array<{member_id: string; display_name: string; role: string}>}>;
		}>(recreatedRoomsResponse);
		const recreatedRoom = recreatedRooms.rooms.find(room => room.id === recreated.id);
		const kickMemberId = recreatedRoom?.members.find(value => value.display_name === "Member")?.member_id;
		expect(kickMemberId).toBeTruthy();

		const kickResponse = await SELF.fetch(authenticatedRequest(
			`/rooms/${recreated.id}/members/${kickMemberId}`,
			"DELETE",
			owner,
		));
		expect(kickResponse.status).toBe(200);

		const rejoinAfterKickResponse = await SELF.fetch(jsonRequest("/rooms/join", {
			code: recreated.invite_code,
			display_name: "Member",
		}, member));
		expect(rejoinAfterKickResponse.status).toBe(201);
		const leaveResponse = await SELF.fetch(authenticatedRequest(
			`/rooms/${recreated.id}/members/me`,
			"DELETE",
			member,
		));
		expect(leaveResponse.status).toBe(200);
		expect(await responseJson(leaveResponse)).toEqual({ok: true, room_deleted: false});

		// Clean up the transferred room before banning the member.
		const deleteTransferredResponse = await SELF.fetch(authenticatedRequest(
			`/rooms/${created.id}/members/me`,
			"DELETE",
			member,
		));
		expect(deleteTransferredResponse.status).toBe(200);

		await testEnv.DB.prepare(
			"INSERT INTO user_bans(install_id, reason, banned_at, expires_at, banned_by) VALUES (?1, ?2, ?3, NULL, ?4)",
		).bind(member.install_id, "integration test", Math.floor(Date.now() / 1000), "test").run();

		const bansResponse = await SELF.fetch(new Request("https://worker.test/internal/bans", {
			headers: {authorization: `Bearer ${testEnv.RELAY_SECRET}`},
		}));
		expect(bansResponse.status).toBe(200);
		expect(await responseJson(bansResponse)).toEqual({install_ids: [member.install_id]});

		const bannedVerifyResponse = await SELF.fetch(jsonRequest("/account/verify", member));
		expect(bannedVerifyResponse.status).toBe(423);

		const deleteResponse = await SELF.fetch(authenticatedRequest(
			`/rooms/${recreated.id}/members/me`,
			"DELETE",
			owner,
		));
		expect(deleteResponse.status).toBe(200);
		expect(await responseJson(deleteResponse)).toEqual({ok: true, room_deleted: true});

		for(let index = 0; index < 5; index++) {
			const roomResponse = await SELF.fetch(jsonRequest("/rooms", {
				name: `Limited Room ${index + 1}`,
				display_name: "Owner",
			}, owner));
			expect(roomResponse.status).toBe(201);
		}
		const roomLimitResponse = await SELF.fetch(jsonRequest("/rooms", {
			name: "Too Many Rooms",
			display_name: "Owner",
		}, owner));
		expect(roomLimitResponse.status).toBe(409);
		expect(await responseJson<{error: string}>(roomLimitResponse)).toMatchObject({error: "room_limit_reached"});
	});
});

describe("launcher notices", () => {
	beforeAll(async () => {
		await applyD1Migrations(testEnv.DB, testEnv.TEST_MIGRATIONS);
	});

	it("supports public listing and admin CRUD with auth", async () => {
		const unauthorized = await SELF.fetch(new Request("https://worker.test/admin/notices", {method: "GET"}));
		expect(unauthorized.status).toBe(401);

		const emptyPublic = await SELF.fetch(new Request("https://worker.test/launcher/notices"));
		expect(emptyPublic.status).toBe(200);
		expect(await responseJson<{notices: unknown[]}>(emptyPublic)).toEqual({notices: []});

		const createResponse = await SELF.fetch(adminRequest("/admin/notices", "POST", {
			title: "Scheduled Maintenance",
			body: "Servers will be unavailable for one hour.",
			severity: "warning",
			blocks_play: true,
			sort_order: 0,
			enabled: true,
		}));
		expect(createResponse.status).toBe(201);
		const created = await responseJson<{notice: {id: string; title: string}}>(createResponse);
		expect(created.notice.title).toBe("Scheduled Maintenance");

		const publicResponse = await SELF.fetch(new Request("https://worker.test/launcher/notices"));
		expect(publicResponse.status).toBe(200);
		const publicBody = await responseJson<{notices: Array<{id: string; blocks_play: boolean}>}>(publicResponse);
		expect(publicBody.notices).toHaveLength(1);
		expect(publicBody.notices[0]?.id).toBe(created.notice.id);
		expect(publicBody.notices[0]?.blocks_play).toBe(true);

		const patchResponse = await SELF.fetch(adminRequest(`/admin/notices/${created.notice.id}`, "PATCH", {
			title: "Maintenance Complete",
			enabled: false,
		}));
		expect(patchResponse.status).toBe(200);

		const hiddenPublic = await SELF.fetch(new Request("https://worker.test/launcher/notices"));
		expect(await responseJson<{notices: unknown[]}>(hiddenPublic)).toEqual({notices: []});

		const deleteResponse = await SELF.fetch(adminRequest(`/admin/notices/${created.notice.id}`, "DELETE"));
		expect(deleteResponse.status).toBe(200);
	});
});

describe("email accounts", () => {
	beforeAll(async () => {
		await applyD1Migrations(testEnv.DB, testEnv.TEST_MIGRATIONS);
	});

	it("preserves legacy accounts while linking email and adding device secrets", async () => {
		const legacy = {
			install_id: "33333333-3333-4333-8333-333333333333",
			secret: "legacy-secret-with-at-least-32-characters",
		};
		expect((await SELF.fetch(jsonRequest("/account/register", legacy))).status).toBe(201);

		const linkResponse = await SELF.fetch(jsonRequest("/account/link-email", {
			email: "  Legacy.User@Example.COM ",
			password: "a-secure-password",
		}, legacy));
		expect(linkResponse.status).toBe(200);
		expect(await responseJson<{install_id: string; has_email: boolean; email: string}>(linkResponse)).toMatchObject({
			install_id: legacy.install_id,
			has_email: true,
			email: "legacy.user@example.com",
		});

		const newDevice = {secret: "new-device-secret-with-at-least-32-characters"};
		const loginResponse = await SELF.fetch(jsonRequest("/account/login-email", {
			email: "LEGACY.USER@example.com",
			password: "a-secure-password",
			secret: newDevice.secret,
			version: "email-test",
		}));
		expect(loginResponse.status).toBe(200);
		expect(await responseJson<{install_id: string}>(loginResponse)).toMatchObject({install_id: legacy.install_id});

		for(const secret of [legacy.secret, newDevice.secret]) {
			const verifyResponse = await SELF.fetch(jsonRequest("/account/verify", {
				install_id: legacy.install_id,
				secret,
			}));
			expect(verifyResponse.status).toBe(200);
		}

		const profile = await SELF.fetch(authenticatedRequest("/account/profile", "GET", {
			install_id: legacy.install_id,
			secret: newDevice.secret,
		}));
		expect(profile.status).toBe(200);
		expect(await responseJson<{email: string; grace_token: string}>(profile)).toMatchObject({
			email: "legacy.user@example.com",
			grace_token: expect.stringContaining("."),
		});
	});

	it("supports email signup, rejects duplicates and invalid passwords, and rate limits bad logins", async () => {
		const emailAccount = {
			install_id: "44444444-4444-4444-8444-444444444444",
			secret: "email-signup-secret-with-at-least-32-characters",
			email: "Signup.User@Example.com",
			password: "signup-password",
			version: "email-test",
		};
		const signup = await SELF.fetch(jsonRequest("/account/register-email", emailAccount));
		expect(signup.status).toBe(201);
		expect(await responseJson<{install_id: string; email: string}>(signup)).toMatchObject({
			install_id: emailAccount.install_id,
			email: "signup.user@example.com",
		});

		const duplicate = await SELF.fetch(jsonRequest("/account/register-email", {
			...emailAccount,
			install_id: "55555555-5555-4555-8555-555555555555",
			secret: "duplicate-secret-with-at-least-32-characters",
			email: " signup.user@EXAMPLE.COM ",
		}));
		expect(duplicate.status).toBe(409);
		expect(await responseJson<{error: string}>(duplicate)).toMatchObject({error: "email_exists"});

		const shortPassword = await SELF.fetch(jsonRequest("/account/register-email", {
			...emailAccount,
			install_id: "66666666-6666-4666-8666-666666666666",
			email: "short@example.com",
			password: "too-short",
		}));
		expect(shortPassword.status).toBe(400);

		for(let attempt = 0; attempt < 10; attempt++) {
			const badLogin = await SELF.fetch(jsonRequest("/account/login-email", {
				email: emailAccount.email,
				password: "incorrect-password",
				secret: "rate-limit-secret-with-at-least-32-characters",
			}));
			expect(badLogin.status).toBe(403);
		}
		const limited = await SELF.fetch(jsonRequest("/account/login-email", {
			email: emailAccount.email,
			password: emailAccount.password,
			secret: "rate-limit-secret-with-at-least-32-characters",
		}));
		expect(limited.status).toBe(429);
	});
});

describe("cfg backups", () => {
	beforeAll(async () => {
		await applyD1Migrations(testEnv.DB, testEnv.TEST_MIGRATIONS);
	});

	it("uploads, lists, downloads, and deletes immutable cfg versions", async () => {
		const account = {
			install_id: "77777777-7777-4777-8777-777777777777",
			secret: "backup-secret-with-at-least-32-characters",
		};
		expect((await SELF.fetch(jsonRequest("/account/register", account))).status).toBe(201);
		const bytes = new TextEncoder().encode("seta name \"backup-test\"\n");
		const upload = await SELF.fetch(new Request("https://worker.test/backups/cfg", {
			method: "PUT",
			headers: {
				authorization: `Bearer ${account.secret}`,
				"x-uclient-install-id": account.install_id,
				"x-uclient-path": encodeURIComponent("configs\\autoexec.cfg"),
			},
			body: bytes,
		}));
		expect(upload.status).toBe(201);
		const uploaded = await responseJson<{
			version: {id: string; relative_path: string; size_bytes: number; sha256: string};
			used_bytes: number;
			quota_bytes: number;
		}>(upload);
		expect(uploaded.version).toMatchObject({
			relative_path: "configs/autoexec.cfg",
			size_bytes: bytes.byteLength,
		});
		expect(uploaded.version.sha256).toMatch(/^[0-9a-f]{64}$/);
		expect(uploaded.used_bytes).toBe(bytes.byteLength);
		expect(uploaded.quota_bytes).toBe(10 * 1024 * 1024);

		const list = await SELF.fetch(authenticatedRequest("/backups/cfg", "GET", account));
		expect(list.status).toBe(200);
		expect(await responseJson<{versions: Array<{id: string}>}>(list)).toMatchObject({
			versions: [{id: uploaded.version.id}],
		});

		const download = await SELF.fetch(authenticatedRequest(`/backups/cfg/${uploaded.version.id}`, "GET", account));
		expect(download.status).toBe(200);
		expect(download.headers.get("x-uclient-path")).toBe(encodeURIComponent("configs/autoexec.cfg"));
		expect(new Uint8Array(await download.arrayBuffer())).toEqual(bytes);

		const remove = await SELF.fetch(authenticatedRequest(`/backups/cfg/${uploaded.version.id}`, "DELETE", account));
		expect(remove.status).toBe(200);
		expect((await SELF.fetch(authenticatedRequest(`/backups/cfg/${uploaded.version.id}`, "GET", account))).status).toBe(404);
	});

	it("rejects invalid paths, oversized objects, and account quota overflow", async () => {
		const account = {
			install_id: "88888888-8888-4888-8888-888888888888",
			secret: "backup-quota-secret-with-at-least-32-characters",
		};
		expect((await SELF.fetch(jsonRequest("/account/register", account))).status).toBe(201);
		const upload = (path: string, body: Uint8Array) => SELF.fetch(new Request("https://worker.test/backups/cfg", {
			method: "PUT",
			headers: {
				authorization: `Bearer ${account.secret}`,
				"x-uclient-install-id": account.install_id,
				"x-uclient-path": encodeURIComponent(path),
			},
			body,
		}));
		expect((await upload("../stolen.cfg", new Uint8Array(1))).status).toBe(400);
		expect((await upload("readme.exe", new Uint8Array(1))).status).toBe(400);
		expect((await upload("dumps/crash.log", new TextEncoder().encode("crash"))).status).toBe(400);
		expect((await upload("DUMPS/crash.log", new TextEncoder().encode("crash"))).status).toBe(400);
		expect((await upload("downloadedskins/skin.png", new Uint8Array([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]))).status).toBe(400);
		expect((await upload("communityicons/ddnet.png", new Uint8Array([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]))).status).toBe(400);
		expect((await upload("large.cfg", new Uint8Array(10 * 1024 * 1024 + 1))).status).toBe(413);

		await testEnv.DB.prepare(
			`INSERT INTO cfg_backup_versions
			 (id, account_install_id, relative_path, object_key, created_at, size_bytes, sha256)
			 VALUES (?1, ?2, 'existing.cfg', ?3, ?4, ?5, ?6)`,
		).bind(
			"99999999-9999-4999-8999-999999999999",
			account.install_id,
			"test/quota-existing",
			Math.floor(Date.now() / 1000),
			9 * 1024 * 1024,
			"0".repeat(64),
		).run();
		expect((await upload("quota.cfg", new Uint8Array(2 * 1024 * 1024).fill(0x61))).status).toBe(413);
	});

	it("validates allowed formats and rejects disguised account credentials", async () => {
		const account = {
			install_id: "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
			secret: "backup-validation-secret-at-least-32-characters",
		};
		expect((await SELF.fetch(jsonRequest("/account/register", account))).status).toBe(201);
		const upload = (path: string, body: Uint8Array) => SELF.fetch(new Request("https://worker.test/backups/cfg", {
			method: "PUT",
			headers: {
				authorization: `Bearer ${account.secret}`,
				"x-uclient-install-id": account.install_id,
				"x-uclient-path": encodeURIComponent(path),
			},
			body,
		}));

		expect((await upload("notes.txt", new TextEncoder().encode("backup notes\n"))).status).toBe(201);
		expect((await upload("image.png", new Uint8Array([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]))).status).toBe(201);
		expect((await upload("renamed.png", new TextEncoder().encode("{\"install_id\":\"x\",\"secret\":\"y\"}"))).status).toBe(400);
		expect((await upload("renamed.txt", new TextEncoder().encode("{\"install_id\":\"x\",\"secret\":\"y\"}"))).status).toBe(400);
		expect((await upload("uclient_account.json", new TextEncoder().encode("{}"))).status).toBe(400);
	});
});
