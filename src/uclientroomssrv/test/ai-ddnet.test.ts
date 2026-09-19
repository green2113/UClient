import {describe, expect, it} from "vitest";
import {
	fallbackDdnetLookups,
	parseDdnetLookups,
	summarizeMap,
	summarizePlayer,
	summarizePlayerMatches,
	summarizeReleases,
	summarizeWikiExtract,
	summarizeWikiSearch,
} from "../src/ai-ddnet";

describe("parseDdnetLookups", () => {
	it("keeps two official types and drops unknown ones", () => {
		expect(parseDdnetLookups([
			{type: "player", query: "deen"},
			{type: "wiki", query: "dummy"},
			{type: "map", query: "Multeasystraight"},
			{type: "browse", query: "https://example.com"},
		])).toEqual([
			{type: "player", query: "deen"},
			{type: "wiki", query: "dummy"},
		]);
	});

	it("allows releases without a query", () => {
		expect(parseDdnetLookups([{type: "releases"}])).toEqual([{type: "releases", query: ""}]);
	});
});

describe("fallbackDdnetLookups", () => {
	it("requests releases for recent-map questions", () => {
		expect(fallbackDdnetLookups("최근 맵 릴리스 뭐야")).toEqual([{type: "releases", query: ""}]);
	});

	it("requests wiki when the user says wiki", () => {
		expect(fallbackDdnetLookups("디디 위키 dummy")).toEqual([{type: "wiki", query: "dummy"}]);
	});
});

describe("summarizePlayer", () => {
	it("keeps ranks and recent finishes, not the full map list", () => {
		const text = summarizePlayer({
			player: "deen",
			points: {rank: 1, points: 999, total: 2000},
			rank: {rank: 2, points: 10},
			team_rank: {rank: null},
			favorite_server: {server: "GER"},
			first_finish: {timestamp: 1_700_000_000, map: "Dummy", time: 12},
			last_finishes: [
				{timestamp: 1_700_000_100, map: "Multeasystraight", time: 97.06, type: "Fun"},
			],
			types: {Novice: {maps: {A: {}, B: {}, C: {}}}},
		}, "deen");
		expect(text).toContain("Player: deen");
		expect(text).toContain("rank 1");
		expect(text).toContain("Multeasystraight");
		expect(text).toContain("https://ddnet.org/players/deen/");
		expect(text).not.toContain("Novice");
	});

	it("says missing when the payload is empty", () => {
		expect(summarizePlayer({}, "nope")).toContain("No official DDNet player");
	});
});

describe("summarizePlayerMatches", () => {
	it("lists prefix matches", () => {
		const text = summarizePlayerMatches([{name: "deen", points: 10}, {name: "Deen2", points: 3}], "dee");
		expect(text).toContain("deen");
		expect(text).toContain("Deen2");
	});
});

describe("summarizeMap", () => {
	it("keeps mapper and top ranks", () => {
		const text = summarizeMap({
			name: "Multeasystraight",
			type: "Fun",
			mapper: "catseyenebulous",
			points: 0,
			difficulty: 0,
			release: 1775037960,
			finishes: 10,
			finishers: 8,
			website: "https://ddnet.org/maps/Multeasystraight",
			ranks: [{rank: 1, player: "Freezestyler", time: 97.06}],
		}, "Multeasystraight");
		expect(text).toContain("catseyenebulous");
		expect(text).toContain("Freezestyler");
		expect(text).toContain("https://ddnet.org/maps/Multeasystraight");
	});
});

describe("summarizeReleases", () => {
	it("filters by mapper and sorts newest first", () => {
		const text = summarizeReleases([
			{name: "Old", mapper: "Welf", type: "Novice", points: 1, release: "2020-01-01"},
			{name: "Cloudberry Fields", mapper: "Welf", type: "Novice", points: 7, release: "2026-09-05"},
			{name: "Other", mapper: "Doshik", type: "Brutal", points: 27, release: "2026-08-29"},
		], "welf");
		expect(text).toContain("Cloudberry Fields");
		expect(text).toContain("Old");
		expect(text).not.toContain("Other");
		expect(text.indexOf("Cloudberry Fields")).toBeLessThan(text.indexOf("Old"));
	});
});

describe("summarizeWiki", () => {
	it("strips html snippets", () => {
		const text = summarizeWikiSearch({
			query: {search: [{title: "Dummy", snippet: "A <span>tile</span> that &amp; freezes"}]},
		}, "dummy");
		expect(text).toContain("Dummy");
		expect(text).toContain("tile");
		expect(text).not.toContain("<span>");
	});

	it("reads a wiki extract", () => {
		const text = summarizeWikiExtract({
			query: {pages: {"1": {title: "Dummy", extract: "Dummy is a freeze tile."}}},
		}, "Dummy");
		expect(text).toContain("Dummy is a freeze tile.");
		expect(text).toContain("wiki.ddnet.org/wiki/Dummy");
	});
});
