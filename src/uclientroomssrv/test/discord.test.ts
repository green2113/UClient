import {applyD1Migrations, env, SELF} from "cloudflare:test";
import {beforeAll, describe, expect, it} from "vitest";

import {DISCORD_TEXT, discordChatPayload, linkedMessage, neutralizeMentions} from "../src/discord";

interface TestEnv extends Cloudflare.Env {
	ACCOUNT_PEPPER: string;
	GRACE_PRIVATE_KEY_SEED_HEX: string;
	RELAY_SECRET: string;
	ADMIN_TOKEN: string;
	DISCORD_INTERNAL_SECRET: string;
	TEST_MIGRATIONS: D1Migration[];
}

interface D1Migration {
	name: string;
	queries: string[];
}

const testEnv = env as TestEnv;
const account = {
	install_id: "33333333-3333-4333-8333-333333333333",
	secret: "discord-owner-secret-with-32-characters",
};
const discordUserId = "123456789012345678";

function jsonRequest(path: string, body: unknown, method = "POST", accountAuth = false): Request {
	const headers: Record<string, string> = {"content-type": "application/json"};
	if(accountAuth) {
		headers.authorization = `Bearer ${account.secret}`;
		headers["x-uclient-install-id"] = account.install_id;
	}
	return new Request(`https://worker.test${path}`, {method, headers, body: JSON.stringify(body)});
}

function botRequest(path: string, body?: unknown, method = "POST"): Request {
	const headers: Record<string, string> = {
		authorization: `Bearer ${testEnv.DISCORD_INTERNAL_SECRET}`,
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

async function startChallenge(): Promise<string> {
	const response = await SELF.fetch(botRequest("/internal/discord/link/start", {
		discord_user_id: discordUserId,
		application_id: "987654321098765432",
		interaction_token: "interaction-token-with-enough-length",
		guild_id: "111111111111111111",
	}));
	expect(response.status).toBe(200);
	const body = await responseJson<{url: string}>(response);
	const token = body.url.split("/").pop() ?? "";
	expect(token.length).toBeGreaterThan(20);
	return token;
}

describe("discord account bridge", () => {
	beforeAll(async () => {
		await applyD1Migrations(testEnv.DB, testEnv.TEST_MIGRATIONS);
		const register = await SELF.fetch(jsonRequest("/account/register", {
			...account,
			player_name: "Discord",
			version: "test",
		}));
		expect(register.status).toBe(201);
	});

	it("neutralizes everyone and here mentions", () => {
		expect(neutralizeMentions("hi @everyone and @here")).toBe("hi @\u200beveryone and @\u200bhere");
		expect(neutralizeMentions("@EVERYONE")).toBe("@\u200bEVERYONE");
		const payload = discordChatPayload("say @here");
		expect(payload.content).toBe("say @\u200bhere");
		expect(payload.allowed_mentions.parse).toEqual([]);
	});

	it("rejects a logged-out launcher and an account without email", async () => {
		const loggedOut = await startChallenge();
		const loggedOutResponse = await SELF.fetch(jsonRequest(`/discord/link/${loggedOut}/reject`, {reason: "not_logged_in"}));
		expect(loggedOutResponse.status).toBe(200);
		expect(await responseJson<{message: string}>(loggedOutResponse)).toMatchObject({message: DISCORD_TEXT.notLoggedIn});

		const noEmail = await startChallenge();
		const confirmResponse = await SELF.fetch(jsonRequest(`/discord/link/${noEmail}/confirm`, {}, "POST", true));
		expect(confirmResponse.status).toBe(403);
		expect(await responseJson<{message: string}>(confirmResponse)).toMatchObject({message: DISCORD_TEXT.noEmail});
		const stillOpen = await SELF.fetch(`https://worker.test/discord/link/${noEmail}`);
		expect(stillOpen.status).toBe(200);

		const rejected = await SELF.fetch(jsonRequest(`/discord/link/${noEmail}/reject`, {reason: "no_email"}));
		expect(rejected.status).toBe(200);
		expect(await responseJson<{message: string}>(rejected)).toMatchObject({message: DISCORD_TEXT.noEmail});
	});

	it("links an email account and replaces the previous link", async () => {
		await testEnv.DB.prepare("UPDATE accounts SET email_normalized = ?1 WHERE install_id = ?2")
			.bind("discord@example.com", account.install_id).run();
		const token = await startChallenge();
		const response = await SELF.fetch(jsonRequest(`/discord/link/${token}/confirm`, {}, "POST", true));
		expect(response.status).toBe(200);
		const body = await responseJson<{message: string}>(response);
		expect(body.message).toBe(linkedMessage(account.install_id));
		const link = await testEnv.DB.prepare("SELECT install_id FROM discord_links WHERE discord_user_id = ?1")
			.bind(discordUserId).first<{install_id: string}>();
		expect(link?.install_id).toBe(account.install_id);
		const page = await SELF.fetch(`https://worker.test/discord/link/${token}`);
		expect(page.status).toBe(410);
	});

	it("rejects an expired challenge", async () => {
		const token = await startChallenge();
		await testEnv.DB.prepare("UPDATE discord_link_challenges SET expires_at = 1 WHERE token = ?1").bind(token).run();
		const response = await SELF.fetch(jsonRequest(`/discord/link/${token}/reject`, {reason: "not_logged_in"}));
		expect(response.status).toBe(410);
	});

	it("keeps one message channel and forwards chat only while the game is online", async () => {
		const missing = await SELF.fetch(botRequest("/internal/discord/status?discord_user_id=999", undefined, "GET"));
		expect(missing.status).toBe(400);

		const unlinkedStatus = await SELF.fetch(botRequest("/internal/discord/status?discord_user_id=555555555555555555", undefined, "GET"));
		expect(await responseJson<{message: string}>(unlinkedStatus)).toMatchObject({linked: false, message: DISCORD_TEXT.notLinked});

		const created = await SELF.fetch(botRequest("/internal/discord/channels", {
			discord_user_id: discordUserId,
			guild_id: "111111111111111111",
			channel_id: "222222222222222222",
		}));
		expect(created.status).toBe(200);
		const again = await SELF.fetch(botRequest("/internal/discord/channels", {
			discord_user_id: discordUserId,
			guild_id: "111111111111111111",
			channel_id: "333333333333333333",
		}));
		expect(again.status).toBe(409);
		expect(await responseJson<{message: string}>(again)).toMatchObject({message: DISCORD_TEXT.channelExists});

		const offline = await SELF.fetch(botRequest("/internal/discord/inbound", {
			channel_id: "222222222222222222",
			content: "hello from discord",
		}));
		expect(offline.status).toBe(200);
		expect(await responseJson<{message: string}>(offline)).toMatchObject({
			ok: false,
			reason: "offline",
			message: DISCORD_TEXT.notInGame,
		});

		const poll = await SELF.fetch(new Request("https://worker.test/discord/chat/outbound", {
			headers: {
				authorization: `Bearer ${account.secret}`,
				"x-uclient-install-id": account.install_id,
			},
		}));
		expect(poll.status).toBe(200);
		expect(await responseJson<{linked: boolean; messages: unknown[]}>(poll)).toMatchObject({linked: true, messages: []});

		const online = await SELF.fetch(botRequest("/internal/discord/inbound", {
			channel_id: "222222222222222222",
			content: "  hello from discord  ",
		}));
		expect(online.status).toBe(200);
		expect(await responseJson<{ok: boolean}>(online)).toMatchObject({ok: true});

		const taken = await SELF.fetch(new Request("https://worker.test/discord/chat/outbound", {
			headers: {
				authorization: `Bearer ${account.secret}`,
				"x-uclient-install-id": account.install_id,
			},
		}));
		const takenBody = await responseJson<{messages: Array<{body: string}>}>(taken);
		expect(takenBody.messages.map(message => message.body)).toEqual(["hello from discord"]);
		const empty = await SELF.fetch(new Request("https://worker.test/discord/chat/outbound", {
			headers: {
				authorization: `Bearer ${account.secret}`,
				"x-uclient-install-id": account.install_id,
			},
		}));
		expect(await responseJson<{messages: unknown[]}>(empty)).toMatchObject({messages: []});

		await testEnv.DB.prepare(
			"INSERT INTO rooms (id, name, owner_install_id, invite_code, created_at) VALUES (?1, ?2, ?3, ?4, ?5)",
		).bind("room-send-1", "Gores", account.install_id, "SENDCODE1", 1).run();
		await testEnv.DB.prepare(
			"INSERT INTO room_members (room_id, install_id, member_id, display_name, role, joined_at) VALUES (?1, ?2, ?3, ?4, 'owner', ?5)",
		).bind("room-send-1", account.install_id, "member-send-1", "Discord", 1).run();
		const rooms = await SELF.fetch(botRequest(`/internal/discord/rooms?discord_user_id=${discordUserId}`, undefined, "GET"));
		expect(rooms.status).toBe(200);
		expect(await responseJson<{rooms: Array<{id: string; name: string; member_count: number}>}>(rooms)).toMatchObject({
			rooms: [{id: "room-send-1", name: "Gores", member_count: 1}],
		});
		const conflict = await SELF.fetch(botRequest("/internal/discord/send", {
			discord_user_id: discordUserId,
			content: "nope",
			mode: "team",
			room_id: "room-send-1",
		}));
		expect(conflict.status).toBe(400);
		const missingRoom = await SELF.fetch(botRequest("/internal/discord/send", {
			discord_user_id: discordUserId,
			content: "hi",
			room_id: "missing-room",
		}));
		expect(missingRoom.status).toBe(404);
		const team = await SELF.fetch(botRequest("/internal/discord/send", {
			discord_user_id: discordUserId,
			content: "team line",
			mode: "team",
		}));
		expect(team.status).toBe(200);
		const roomSend = await SELF.fetch(botRequest("/internal/discord/send", {
			discord_user_id: discordUserId,
			content: "room line",
			room_id: "room-send-1",
		}));
		expect(roomSend.status).toBe(200);
		const globalSend = await SELF.fetch(botRequest("/internal/discord/send", {
			discord_user_id: discordUserId,
			content: "global line",
			mode: "uclient",
		}));
		expect(globalSend.status).toBe(200);
		const sentLines = await SELF.fetch(new Request("https://worker.test/discord/chat/outbound", {
			headers: {
				authorization: `Bearer ${account.secret}`,
				"x-uclient-install-id": account.install_id,
			},
		}));
		const sentBody = await responseJson<{messages: Array<{body: string; mode: string; room_id: string}>}>(sentLines);
		expect(sentBody.messages).toEqual([
			{id: expect.any(Number), body: "team line", mode: "team", room_id: "", discord_channel_id: "", discord_message_id: ""},
			{id: expect.any(Number), body: "room line", mode: "room", room_id: "room-send-1", discord_channel_id: "", discord_message_id: ""},
			{id: expect.any(Number), body: "global line", mode: "uclient", room_id: "", discord_channel_id: "", discord_message_id: ""},
		]);

		const firstIngest = await SELF.fetch(jsonRequest("/discord/chat/ingest", {
			lines: ["2026-10-04 23:58:35 I chat/all: same line"],
		}, "POST", true));
		expect(firstIngest.status).toBe(200);
		expect(await responseJson<{accepted: number}>(firstIngest)).toMatchObject({accepted: 1});
		const repeatIngest = await SELF.fetch(jsonRequest("/discord/chat/ingest", {
			lines: ["2026-10-04 23:58:35 I chat/all: same line"],
		}, "POST", true));
		expect(repeatIngest.status).toBe(200);
		expect(await responseJson<{accepted: number}>(repeatIngest)).toMatchObject({accepted: 0});

		const topic = await SELF.fetch(jsonRequest("/discord/chat/topic", {
			topic: "2 players: Ann, @everyone",
		}, "POST", true));
		expect(topic.status).toBe(200);
		expect(await responseJson<{ok: boolean; applied: boolean}>(topic)).toMatchObject({ok: true, applied: true});
		const stored = await testEnv.DB.prepare(
			"SELECT topic_desired FROM discord_message_channels WHERE install_id = ?1",
		).bind(account.install_id).first<{topic_desired: string}>();
		expect(stored?.topic_desired).toBe("2 players: Ann, @\u200beveryone");

		const shrunk = await SELF.fetch(jsonRequest("/discord/chat/topic", {
			topic: "1 player: Ann",
		}, "POST", true));
		expect(await responseJson<{applied: boolean}>(shrunk)).toMatchObject({applied: true});
		const shrunkRow = await testEnv.DB.prepare(
			"SELECT topic_desired FROM discord_message_channels WHERE install_id = ?1",
		).bind(account.install_id).first<{topic_desired: string}>();
		expect(shrunkRow?.topic_desired).toBe("1 player: Ann");

		const cleared = await SELF.fetch(jsonRequest("/discord/chat/topic", {topic: ""}, "POST", true));
		expect(cleared.status).toBe(200);
		const clearedRow = await testEnv.DB.prepare(
			"SELECT topic_desired FROM discord_message_channels WHERE install_id = ?1",
		).bind(account.install_id).first<{topic_desired: string}>();
		expect(clearedRow?.topic_desired).toBe("");

		const removed = await SELF.fetch(botRequest("/internal/discord/channels", {discord_user_id: discordUserId}, "DELETE"));
		expect(removed.status).toBe(200);
		const missingChannel = await SELF.fetch(botRequest("/internal/discord/channels", {discord_user_id: discordUserId}, "DELETE"));
		expect(missingChannel.status).toBe(404);
		expect(await responseJson<{message: string}>(missingChannel)).toMatchObject({message: DISCORD_TEXT.noChannel});
	});

	it("connects only while the launcher and game are running", async () => {
		const control = (extra: Record<string, string> = {}) => botRequest("/internal/discord/connect", {
			discord_user_id: discordUserId,
			application_id: "987654321098765432",
			interaction_token: "interaction-token-with-enough-length",
			...extra,
		});
		const accountCall = (path: string, body?: unknown, method = "GET") => {
			const headers: Record<string, string> = {
				authorization: `Bearer ${account.secret}`,
				"x-uclient-install-id": account.install_id,
				"x-uclient-game-session": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
				"x-uclient-game-started": "1700000000",
			};
			if(body !== undefined)
				headers["content-type"] = "application/json";
			return new Request(`https://worker.test${path}`, {
				method,
				headers,
				body: body === undefined ? undefined : JSON.stringify(body),
			});
		};

		const noLauncher = await SELF.fetch(control({address: "127.0.0.1:8303"}));
		expect(await responseJson<{message: string}>(noLauncher)).toMatchObject({queued: false, message: DISCORD_TEXT.launcherDown});

		expect((await SELF.fetch(accountCall("/discord/launcher/commands"))).status).toBe(200);
		const noGame = await SELF.fetch(control({address: "127.0.0.1:8303"}));
		expect(await responseJson<{message: string}>(noGame)).toMatchObject({queued: false, message: DISCORD_TEXT.gameDown});

		expect((await SELF.fetch(accountCall("/discord/control"))).status).toBe(200);
		const queued = await SELF.fetch(control({address: "127.0.0.1:8303"}));
		expect(await responseJson<{queued: boolean; message: string}>(queued)).toMatchObject({
			queued: true,
			message: "Connecting to 127.0.0.1:8303...",
		});
		const claimed = await responseJson<{commands: Array<{id: number; kind: string; password: string; had_password: boolean}>}>(
			await SELF.fetch(accountCall("/discord/control")),
		);
		expect(claimed.commands).toHaveLength(1);
		expect(claimed.commands[0]).toMatchObject({kind: "connect", password: "", had_password: false});
		const result = await SELF.fetch(accountCall("/discord/control/result", {
			id: claimed.commands[0].id,
			code: "password",
			detail: "This server requires a password",
		}, "POST"));
		const resultBody = await responseJson<{message: string}>(result);
		expect(resultBody.message).toBe("This server requires a password. Use /connect 127.0.0.1:8303 <password>.");

		const withPassword = await SELF.fetch(control({address: "127.0.0.1:8303", password: "secret-pass"}));
		expect((await responseJson<{queued: boolean}>(withPassword)).queued).toBe(true);
		const claimedPassword = await responseJson<{commands: Array<{id: number; password: string; had_password: boolean}>}>(
			await SELF.fetch(accountCall("/discord/control")),
		);
		expect(claimedPassword.commands[0]).toMatchObject({password: "secret-pass", had_password: true});
		const rejected = await SELF.fetch(accountCall("/discord/control/result", {
			id: claimedPassword.commands[0].id,
			code: "password",
			detail: "Wrong password",
		}, "POST"));
		const rejectedBody = await responseJson<{message: string}>(rejected);
		expect(rejectedBody.message).toBe("Could not connect to 127.0.0.1:8303. The password was not accepted.");
		expect(rejectedBody.message).not.toContain("secret-pass");
	});

	it("gives discord control and chat to the earliest game client", async () => {
		const gameCall = (session: string, started: string, path: string) => new Request(`https://worker.test${path}`, {
			method: "GET",
			headers: {
				authorization: `Bearer ${account.secret}`,
				"x-uclient-install-id": account.install_id,
				"x-uclient-game-session": session,
				"x-uclient-game-started": started,
			},
		});
		const first = "11111111111111111111111111111111";
		const second = "22222222222222222222222222222222";
		await SELF.fetch(new Request("https://worker.test/discord/launcher/commands", {
			headers: {
				authorization: `Bearer ${account.secret}`,
				"x-uclient-install-id": account.install_id,
			},
		}));
		expect((await responseJson<{owner: boolean}>(await SELF.fetch(gameCall(first, "1600000001", "/discord/control")))).owner).toBe(true);
		expect((await responseJson<{owner: boolean; commands: unknown[]}>(await SELF.fetch(gameCall(second, "1700000100", "/discord/control"))))).toMatchObject({
			owner: false,
			commands: [],
		});

		const queued = await SELF.fetch(botRequest("/internal/discord/connect", {
			discord_user_id: discordUserId,
			application_id: "987654321098765432",
			interaction_token: "interaction-token-with-enough-length",
			address: "127.0.0.1:8303",
		}));
		expect((await responseJson<{queued: boolean}>(queued)).queued).toBe(true);
		const missed = await responseJson<{owner: boolean; commands: unknown[]}>(await SELF.fetch(gameCall(second, "1700000100", "/discord/control")));
		expect(missed).toMatchObject({owner: false, commands: []});
		const claimed = await responseJson<{owner: boolean; commands: Array<{kind: string}>}>(await SELF.fetch(gameCall(first, "1600000001", "/discord/control")));
		expect(claimed.owner).toBe(true);
		expect(claimed.commands).toHaveLength(1);

		await testEnv.DB.prepare(
			"INSERT INTO discord_outbound_messages (install_id, body, created_at, mode, room_id) VALUES (?1, 'hello', ?2, 'all', '')",
		).bind(account.install_id, 1_700_000_200).run();
		const ignored = await responseJson<{messages: unknown[]}>(await SELF.fetch(gameCall(second, "1700000100", "/discord/chat/outbound")));
		expect(ignored.messages).toEqual([]);
		const taken = await responseJson<{messages: Array<{body: string}>}>(await SELF.fetch(gameCall(first, "1600000001", "/discord/chat/outbound")));
		expect(taken.messages.map(row => row.body)).toEqual(["hello"]);

		const started = await SELF.fetch(botRequest("/internal/discord/start-game", {
			discord_user_id: discordUserId,
			application_id: "987654321098765432",
			interaction_token: "interaction-token-with-enough-length",
		}));
		expect(await responseJson<{message: string}>(started)).toMatchObject({message: DISCORD_TEXT.gameAlreadyRunning});
	});

	it("waits to delete a discord message until the game has it", async () => {
		const headers = {
			authorization: `Bearer ${account.secret}`,
			"x-uclient-install-id": account.install_id,
			"x-uclient-game-session": "11111111111111111111111111111111",
			"x-uclient-game-started": "1600000001",
		};
		await SELF.fetch(new Request("https://worker.test/discord/control", {headers}));
		await testEnv.DB.prepare(
			"INSERT INTO discord_outbound_messages (install_id, body, created_at, mode, room_id, discord_channel_id, discord_message_id) VALUES (?1, 'from discord', ?2, 'all', '', ?3, ?4)",
		).bind(account.install_id, 1_700_000_300, "111111111111111111", "222222222222222222").run();
		const taken = await responseJson<{messages: Array<{body: string; discord_channel_id: string; discord_message_id: string}>}>(
			await SELF.fetch(new Request("https://worker.test/discord/chat/outbound", {headers})),
		);
		expect(taken.messages).toEqual([{
			id: expect.any(Number),
			body: "from discord",
			mode: "all",
			room_id: "",
			discord_channel_id: "111111111111111111",
			discord_message_id: "222222222222222222",
		}]);
		const pending = await testEnv.DB.prepare(
			"SELECT message_id FROM discord_pending_deletes WHERE install_id = ?1 AND message_id = ?2",
		).bind(account.install_id, "222222222222222222").first<{message_id: string}>();
		expect(pending?.message_id).toBe("222222222222222222");
	});

	it("stores the launcher friend list for the linked account", async () => {
		const saved = await SELF.fetch(jsonRequest("/discord/friends", {names: ["Under", " under ", "NEEB", ""]}, "POST", true));
		expect(saved.status).toBe(200);
		const row = await testEnv.DB.prepare("SELECT names_json FROM launcher_friends WHERE install_id = ?1")
			.bind(account.install_id).first<{names_json: string}>();
		expect(JSON.parse(row?.names_json ?? "[]")).toEqual(["Under", "NEEB"]);
	});
});
