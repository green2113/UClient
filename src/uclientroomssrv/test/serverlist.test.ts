import {describe, expect, it} from "vitest";

import {findPlayerServers, formatFriendGroups, formatPlayerLines, formatRoster, groupOnlineFriends} from "../src/serverlist";

const servers = {
	servers: [
		{
			addresses: ["tw-0.6+udp://1.2.3.4:3380"],
			info: {
				name: "GER",
				map: {name: "Sunny"},
				max_clients: 16,
				passworded: true,
				clients: [{name: "Under"}, {name: "그그사"}, {name: "짱쩝이"}],
			},
		},
		{
			addresses: ["tw-0.6+udp://5.6.7.8:3380"],
			info: {
				max_clients: 8,
				passworded: false,
				clients: [{name: "under"}, {name: "NEEB"}],
			},
		},
		{
			addresses: ["tw-0.6+udp://9.9.9.9:8303"],
			info: {
				max_players: 12,
				passworded: false,
				clients: [{name: "Someone"}],
			},
		},
	],
};

describe("public server list", () => {
	it("lists every server with the same player and whether it has a password", () => {
		const hits = findPlayerServers(servers, "under");
		expect(hits).toEqual([
			{address: "1.2.3.4:3380", count: 3, max: 16, password: true, player: "Under", serverName: "GER", map: "Sunny"},
			{address: "5.6.7.8:3380", count: 2, max: 8, password: false, player: "under", serverName: "", map: ""},
		]);
		expect(formatPlayerLines(hits)).toBe("1.2.3.4:3380 (3/16) password\n5.6.7.8:3380 (2/8)");
		expect(findPlayerServers(servers, "Und")).toEqual([]);
	});

	it("groups online friends on the same server", () => {
		const groups = groupOnlineFriends(servers, [], ["짱쩝이", "Under", "NEEB", "Offline"]);
		expect(formatFriendGroups(groups)).toBe([
			"1.2.3.4:3380 (3/16)",
			"Under, 짱쩝이",
			"",
			"5.6.7.8:3380 (2/8)",
			"NEEB, Under",
		].join("\n"));
		expect(formatRoster(groups[0].players, groups[0].names)).toBe("__**Under**__, 그그사, __**짱쩝이**__");
		expect(formatRoster(groups[1].players, groups[1].names)).toBe("__**NEEB, under**__");
		expect(groups[0].players).toEqual(["Under", "그그사", "짱쩝이"]);
	});

	it("uses presence when a friend is on a server outside the public list", () => {
		const groups = groupOnlineFriends(servers, [
			{"8.8.8.8:8310": {players: [{name: "Offline", last_seen: 10}]}},
		], ["Offline"]);
		expect(groups).toEqual([
			{address: "8.8.8.8:8310", count: null, max: null, names: ["Offline"], players: []},
		]);
		expect(formatFriendGroups(groups)).toBe("8.8.8.8:8310\nOffline");
	});
});
