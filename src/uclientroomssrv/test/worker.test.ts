import {applyD1Migrations, env, SELF} from "cloudflare:test";
import {beforeAll, describe, expect, it} from "vitest";

interface TestEnv extends Cloudflare.Env {
	ACCOUNT_PEPPER: string;
	GRACE_PRIVATE_KEY_SEED_HEX: string;
	RELAY_SECRET: string;
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
			room_id: created.id,
			room_name: "Integration Room",
			install_ids: expect.arrayContaining([owner.install_id, member.install_id]),
		});

		const kickResponse = await SELF.fetch(authenticatedRequest(
			`/rooms/${created.id}/members/${memberId}`,
			"DELETE",
			owner,
		));
		expect(kickResponse.status).toBe(200);

		const rejoinResponse = await SELF.fetch(jsonRequest("/rooms/join", {
			code: created.invite_code,
			display_name: "Member",
		}, member));
		expect(rejoinResponse.status).toBe(201);
		const leaveResponse = await SELF.fetch(authenticatedRequest(
			`/rooms/${created.id}/members/me`,
			"DELETE",
			member,
		));
		expect(leaveResponse.status).toBe(200);
		expect(await responseJson(leaveResponse)).toEqual({ok: true, room_deleted: false});

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
			`/rooms/${created.id}/members/me`,
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
