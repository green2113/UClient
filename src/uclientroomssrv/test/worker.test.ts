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
		expect(colorAsAdminResponse.status).toBe(403);

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
			rooms: Array<{room_id: string; install_ids: string[]}>;
		}>(membershipsResponse);
		expect(memberships.sequence).toBeGreaterThan(0);
		expect(memberships.rooms).toContainEqual({
			room_id: recreated.id,
			room_name: "Integration Room 2",
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
