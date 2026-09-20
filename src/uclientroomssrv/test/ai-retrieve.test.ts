import {describe, expect, it} from "vitest";
import {parseRetrievalPlan, pickBinds, pickLiveSettings, replyLanguage, retrieveAssistantContext} from "../src/ai-retrieve";

describe("retrieveAssistantContext", () => {
	it("finds UClient chat from planner search queries, not Korean aliases", () => {
		const retrieved = retrieveAssistantContext("혹시 유클 챗 끄는 방법 알아?", {
			intent: "settings",
			searchQueries: ["uc_chat", "UClient chat"],
			needShortcutBlocks: false,
			needSettings: true,
			needLauncher: false,
			replyLanguage: "Korean",
			ddnet: [],
		});
		expect(retrieved.knowledgeIds).toContain("settings");
		expect(retrieved.knowledgeIds).toContain("conversation");
		expect(retrieved.knowledgeIds.some((id) => id.startsWith("block-"))).toBe(false);
		expect(retrieved.settingNames).toContain("uc_chat");
		expect(retrieved.settingNames.length).toBeLessThanOrEqual(16);
		expect(retrieved.text).toContain("Settings → UClient → Others");
		expect(retrieved.text).not.toContain("cl_predict\t");
	});

	it("loads shortcut blocks only when the planner asks for them", () => {
		const retrieved = retrieveAssistantContext("안녕이라고 하면 답장하는 단축어 만들어줘", {
			intent: "shortcut_create",
			searchQueries: ["shortcuts", "chat_received"],
			needShortcutBlocks: true,
			needSettings: false,
			needLauncher: false,
			replyLanguage: "Korean",
			ddnet: [],
		});
		expect(retrieved.knowledgeIds).toContain("shortcuts");
		expect(retrieved.knowledgeIds).toContain("block-triggers");
		expect(retrieved.knowledgeIds).toContain("block-flow");
		expect(retrieved.knowledgeIds).toContain("block-actions");
		expect(retrieved.text).toContain("chat_received");
		expect(retrieved.text).toContain("ask_for_text");
		expect(retrieved.text).toContain("There is no trigger for");
		expect(retrieved.text).toContain('type:"if"');
	});

	it("does not load the shortcut catalog for a follow-up question", () => {
		const retrieved = retrieveAssistantContext("내 서버 이름이 뭐야");
		expect(retrieved.knowledgeIds).toContain("conversation");
		expect(retrieved.knowledgeIds).not.toContain("shortcuts");
		expect(retrieved.knowledgeIds.some((id) => id.startsWith("block-"))).toBe(false);
		expect(retrieved.text).toContain("cannot see which server they are on");
	});

	it("loads File Backup steps when the planner asks for launcher backup", () => {
		const retrieved = retrieveAssistantContext("파일 백업이랑 복구 어떻게 해", {
			intent: "status",
			searchQueries: ["File Backup"],
			needShortcutBlocks: false,
			needSettings: false,
			needLauncher: true,
			replyLanguage: "Korean",
			ddnet: [],
		});
		expect(retrieved.knowledgeIds).toContain("file-backup");
		expect(retrieved.knowledgeIds).toContain("launcher");
		expect(retrieved.text).toContain("File Backup → Backup tab");
		expect(retrieved.text).toContain("two versions of the same file");
		expect(retrieved.text).toContain("10 MB");
		expect(retrieved.text).toContain("copied aside first");
	});

	it("loads auto-update steps from Launcher", () => {
		const retrieved = retrieveAssistantContext("클라이언트 자동 업데이트 어떻게 켜", {
			intent: "status",
			searchQueries: ["Launcher"],
			needShortcutBlocks: false,
			needSettings: false,
			needLauncher: true,
			replyLanguage: "Korean",
			ddnet: [],
		});
		expect(retrieved.knowledgeIds).toContain("launcher");
		expect(retrieved.text).toContain("Install client updates automatically");
		expect(retrieved.text).toContain("startup only");
		expect(retrieved.text).toContain("Default is off");
	});

	it("loads Account help when the planner asks for it", () => {
		const retrieved = retrieveAssistantContext("이메일 계정 어떻게 만들어", {
			intent: "status",
			searchQueries: ["Account", "email"],
			needShortcutBlocks: false,
			needSettings: false,
			needLauncher: true,
			replyLanguage: "Korean",
			ddnet: [],
		});
		expect(retrieved.knowledgeIds).toContain("account");
		expect(retrieved.text).toContain("Connect email");
		expect(retrieved.text).toContain("discord.gg/EN4yYypsPs");
		expect(retrieved.text).toContain("does not create a new");
	});

	it("finds system/server chat from planner keys, not UClient", () => {
		const retrieved = retrieveAssistantContext("노란색으로 메세지가 뜨는 건 뭐야? 앞에 *도 붙어있어", {
			intent: "settings",
			searchQueries: ["cl_message_system_color", "cl_show_chat_system", "system message"],
			needShortcutBlocks: false,
			needSettings: true,
			needLauncher: false,
			replyLanguage: "Korean",
			ddnet: [],
		});
		expect(retrieved.knowledgeIds).toContain("settings");
		expect(retrieved.settingNames).toContain("cl_message_system_color");
		expect(retrieved.settingNames).toContain("cl_show_chat_system");
		expect(retrieved.text).toContain("server/system message");
		expect(retrieved.text).toContain("Do not guess UClient chat for every colored line");
	});

	it("finds chat animation settings from planner keys", () => {
		const retrieved = retrieveAssistantContext("How do I turn off chat animations?", {
			intent: "settings",
			searchQueries: ["bc_chat_animation", "chat animation"],
			needShortcutBlocks: false,
			needSettings: true,
			needLauncher: false,
			replyLanguage: "English",
			ddnet: [],
		});
		expect(retrieved.settingNames).toContain("bc_chat_animation");
		expect(retrieved.knowledgeIds).toContain("settings");
	});

	it("finds camera drift settings from the English words in the question", () => {
		const retrieved = retrieveAssistantContext("What is Camera Drift?");
		expect(retrieved.settingNames).toContain("bc_camera_drift");
		expect(retrieved.settingNames.length).toBeLessThanOrEqual(16);
	});

	it("picks the matching bind from planner search terms", () => {
		const binds = pickBinds(
			[
				{key: "mouse1", command: "+fire"},
				{key: "space", command: "+jump"},
				{key: "mouse2", command: "+hook"},
			],
			"내 총 쏘는 키가 뭐로 설정되어 있지?",
			{
				intent: "settings",
				searchQueries: ["Binds", "+fire"],
				needShortcutBlocks: false,
				needSettings: true,
				needLauncher: false,
				replyLanguage: "Korean",
				ddnet: [],
			},
		);
		expect(binds).toEqual([{key: "mouse1", command: "+fire"}]);
	});

	it("keeps only retrieved live setting values", () => {
		const live = pickLiveSettings(
			{uc_chat: 1, cl_predict: 0, bc_camera_drift: 1},
			["uc_chat"],
		);
		expect(live).toEqual({uc_chat: 1});
	});

	it("does not load shortcut blocks when the planner says not to", () => {
		const retrieved = retrieveAssistantContext("만들어 둔 단축어가 뭐야", {
			intent: "status",
			searchQueries: ["shortcuts list"],
			needShortcutBlocks: false,
			needSettings: false,
			needLauncher: false,
			replyLanguage: "Korean",
			ddnet: [],
		});
		expect(retrieved.knowledgeIds).not.toContain("shortcuts");
		expect(retrieved.knowledgeIds.some((id) => id.startsWith("block-"))).toBe(false);
	});

	it("loads Binds when the planner asks for that chunk", () => {
		const retrieved = retrieveAssistantContext("e키에 웃는 이모트 만들어줘", {
			intent: "settings",
			searchQueries: ["Binds", "emote 14"],
			needShortcutBlocks: false,
			needSettings: true,
			needLauncher: false,
			replyLanguage: "Korean",
			ddnet: [],
		});
		expect(retrieved.knowledgeIds).toContain("binds");
		expect(retrieved.knowledgeIds).not.toContain("shortcuts");
		expect(retrieved.text).toContain('bind e "emote 14"');
	});

	it("follows the planner for a hammerfly bind", () => {
		const retrieved = retrieveAssistantContext("해머플라이 하는 거 만들어줘", {
			intent: "mixed",
			searchQueries: ["Binds", "cl_dummy_hammer", "Hammerfly"],
			needShortcutBlocks: false,
			needSettings: true,
			needLauncher: false,
			replyLanguage: "Korean",
			ddnet: [{type: "wiki", query: "Hammerfly"}],
		});
		expect(retrieved.knowledgeIds).toContain("binds");
		expect(retrieved.knowledgeIds).not.toContain("shortcuts");
		expect(retrieved.settingNames).toContain("cl_dummy_hammer");
		expect(retrieved.text).toContain("toggle cl_dummy_hammer 0 1");
	});

	it("uses planner search queries to find settings", () => {
		const retrieved = retrieveAssistantContext("그거 어떻게 꺼", {
			intent: "settings",
			searchQueries: ["uc_chat", "UClient chat disable"],
			needShortcutBlocks: false,
			needSettings: true,
			needLauncher: false,
			replyLanguage: "Korean",
			ddnet: [],
		});
		expect(retrieved.knowledgeIds).toContain("settings");
		expect(retrieved.settingNames).toContain("uc_chat");
	});
});

describe("replyLanguage", () => {
	it("uses English for an English message even if locale is Korean", () => {
		expect(replyLanguage("Make a shortcut", "ko-KR")).toBe("English");
		expect(replyLanguage("How do I turn off UClient chat?", "ko")).toBe("English");
	});

	it("uses Korean for a Korean message", () => {
		expect(replyLanguage("단축어 만들어줘", "en")).toBe("Korean");
	});

	it("uses the planner language for an explicit switch", () => {
		expect(replyLanguage("이거 영어로 답해줘", "ko-KR")).toBe("Korean");
		expect(replyLanguage("이거 영어로 답해줘", "ko-KR", "English")).toBe("English");
	});

	it("falls back to the Windows locale when the message has no language", () => {
		expect(replyLanguage("?", "ko-KR")).toBe("Korean");
		expect(replyLanguage("?", "")).toBe("English");
		expect(replyLanguage("?", "zh-CN")).toBe("Simplified Chinese");
		expect(replyLanguage("?", "zh-TW")).toBe("Traditional Chinese");
	});

	it("answers Chinese from the message, and Taiwan locale as traditional", () => {
		expect(replyLanguage("怎么关掉 UClient 聊天", "en-US")).toBe("Simplified Chinese");
		expect(replyLanguage("怎麼關掉聊天", "zh-TW")).toBe("Traditional Chinese");
		expect(replyLanguage("怎么关掉聊天", "zh-TW", "Traditional Chinese")).toBe("Traditional Chinese");
	});
});

describe("parseRetrievalPlan", () => {
	it("reads JSON after reasoning tags", () => {
		const plan = parseRetrievalPlan(`<reasoning>think</reasoning>
{"intent":"settings","search_queries":["uc_chat","UClient chat"],"need_shortcut_blocks":false,"need_settings":true,"need_launcher":false,"reply_language":"Korean"}`);
		expect(plan).toEqual({
			intent: "settings",
			searchQueries: ["uc_chat", "UClient chat"],
			needShortcutBlocks: false,
			needSettings: true,
			needLauncher: false,
			replyLanguage: "Korean",
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
		expect(plan?.replyLanguage).toBe("");
	});
});
