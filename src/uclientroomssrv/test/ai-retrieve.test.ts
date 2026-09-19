import {describe, expect, it} from "vitest";
import {parseRetrievalPlan, pickLiveSettings, replyLanguage, retrieveAssistantContext, shortcutIntent} from "../src/ai-retrieve";

describe("retrieveAssistantContext", () => {
	it("finds UClient chat disable path without dumping the catalog", () => {
		const retrieved = retrieveAssistantContext("혹시 유클 챗 끄는 방법 알아?");
		expect(retrieved.knowledgeIds).toContain("settings");
		expect(retrieved.knowledgeIds).toContain("conversation");
		expect(retrieved.knowledgeIds.some((id) => id.startsWith("block-"))).toBe(false);
		expect(retrieved.settingNames).toContain("uc_chat");
		expect(retrieved.settingNames.length).toBeLessThanOrEqual(16);
		expect(retrieved.text).toContain("Settings → UClient → Others");
		expect(retrieved.text).not.toContain("cl_predict\t");
	});

	it("loads shortcut blocks only when asked to make a shortcut", () => {
		expect(shortcutIntent("안녕이라고 하면 답장하는 단축어 만들어줘")).toBe(true);
		const retrieved = retrieveAssistantContext("안녕이라고 하면 답장하는 단축어 만들어줘");
		expect(retrieved.knowledgeIds).toContain("shortcuts");
		expect(retrieved.knowledgeIds).toContain("block-triggers");
		expect(retrieved.knowledgeIds).toContain("block-flow");
		expect(retrieved.knowledgeIds).toContain("block-actions");
		expect(retrieved.text).toContain("chat_received");
		expect(retrieved.text).toContain('type:"if"');
	});

	it("does not load the shortcut catalog for a follow-up question", () => {
		expect(shortcutIntent("내 서버 이름이 뭐야")).toBe(false);
		const retrieved = retrieveAssistantContext("내 서버 이름이 뭐야");
		expect(retrieved.knowledgeIds).toContain("conversation");
		expect(retrieved.knowledgeIds).not.toContain("shortcuts");
		expect(retrieved.knowledgeIds.some((id) => id.startsWith("block-"))).toBe(false);
		expect(retrieved.text).toContain("cannot see the live server");
	});

	it("finds chat animation settings from an English question", () => {
		const retrieved = retrieveAssistantContext("How do I turn off chat animations?");
		expect(retrieved.settingNames).toContain("bc_chat_animation");
		expect(retrieved.knowledgeIds).toContain("settings");
	});

	it("finds camera drift settings from an English question", () => {
		const retrieved = retrieveAssistantContext("What is Camera Drift?");
		expect(retrieved.settingNames).toContain("bc_camera_drift");
		expect(retrieved.settingNames.length).toBeLessThanOrEqual(16);
	});

	it("keeps only retrieved live setting values", () => {
		const live = pickLiveSettings(
			{uc_chat: 1, cl_predict: 0, bc_camera_drift: 1},
			["uc_chat"],
		);
		expect(live).toEqual({uc_chat: 1});
	});

	it("lets a planner override keyword shortcut detection", () => {
		const retrieved = retrieveAssistantContext("만들어 둔 단축어가 뭐야", {
			intent: "status",
			searchQueries: ["shortcuts list"],
			needShortcutBlocks: false,
			needSettings: false,
			needLauncher: false,
			ddnet: [],
		});
		expect(retrieved.knowledgeIds).not.toContain("shortcuts");
		expect(retrieved.knowledgeIds.some((id) => id.startsWith("block-"))).toBe(false);
	});

	it("uses planner search queries to find settings", () => {
		const retrieved = retrieveAssistantContext("그거 어떻게 꺼", {
			intent: "settings",
			searchQueries: ["uc_chat", "UClient chat disable"],
			needShortcutBlocks: false,
			needSettings: true,
			needLauncher: false,
			ddnet: [],
		});
		expect(retrieved.knowledgeIds).toContain("settings");
		expect(retrieved.settingNames).toContain("uc_chat");
	});
});

describe("replyLanguage", () => {
	it("uses English for an English suggestion even if locale is Korean", () => {
		expect(replyLanguage("Make a shortcut", "korean")).toBe("English");
		expect(replyLanguage("How do I turn off UClient chat?", "ko")).toBe("English");
	});

	it("uses Korean for a Korean message", () => {
		expect(replyLanguage("단축어 만들어줘", "en")).toBe("Korean");
	});

	it("honors an explicit language request", () => {
		expect(replyLanguage("이거 영어로 답해줘", "ko-KR")).toBe("English");
	});

	it("falls back to the Windows locale when the message has no language", () => {
		expect(replyLanguage("?", "ko-KR")).toBe("Korean");
		expect(replyLanguage("?", "")).toBe("English");
	});
});

describe("parseRetrievalPlan", () => {
	it("reads JSON after reasoning tags", () => {
		const plan = parseRetrievalPlan(`<reasoning>think</reasoning>
{"intent":"settings","search_queries":["uc_chat","UClient chat"],"need_shortcut_blocks":false,"need_settings":true,"need_launcher":false}`);
		expect(plan).toEqual({
			intent: "settings",
			searchQueries: ["uc_chat", "UClient chat"],
			needShortcutBlocks: false,
			needSettings: true,
			needLauncher: false,
			ddnet: [],
		});
	});

	it("returns null for empty junk", () => {
		expect(parseRetrievalPlan("not json")).toBeNull();
	});

	it("keeps official DDNet lookups from the planner", () => {
		const plan = parseRetrievalPlan(JSON.stringify({
			intent: "ddnet",
			search_queries: [],
			need_shortcut_blocks: false,
			need_settings: false,
			need_launcher: false,
			ddnet: [
				{type: "player", query: "deen"},
				{type: "map", query: "Multeasystraight"},
				{type: "http", query: "https://evil.example"},
			],
		}));
		expect(plan?.ddnet).toEqual([
			{type: "player", query: "deen"},
			{type: "map", query: "Multeasystraight"},
		]);
	});
});
