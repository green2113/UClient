import {AI_CATALOG, AI_KNOWLEDGE} from "./ai-catalog.generated";
import {parseDdnetLookups, type DdnetLookup} from "./ai-ddnet";

const MAX_SETTINGS = 16;
const MAX_EXTRA_CHUNKS = 2;
const MIN_SETTING_SCORE = 8;
const MIN_CHUNK_SCORE = 4;

const STOPWORDS = new Set([
	"a", "an", "and", "are", "as", "at", "be", "can", "do", "does", "for", "from",
	"how", "i", "in", "is", "it", "me", "my", "of", "on", "or", "the", "to", "what",
	"when", "where", "which", "who", "with", "you", "your",
	"좀", "요", "거", "게", "수", "해", "해줘", "주세요", "알려줘", "알아", "혹시",
	"뭐", "뭐야", "뭔가", "있나", "있어요", "해주세요",
]);

type CatalogSetting = {
	name: string;
	desc: string;
	secret?: string;
};

type KnowledgeChunk = {
	id: string;
	title: string;
	text: string;
	haystack: string;
};

type IndexedSetting = {
	name: string;
	desc: string;
	nameParts: string[];
	haystack: string;
};

export type RetrievalPlan = {
	intent: string;
	searchQueries: string[];
	needShortcutBlocks: boolean;
	needSettings: boolean;
	needLauncher: boolean;
	ddnet: DdnetLookup[];
};

export type RetrievedAssistantContext = {
	text: string;
	knowledgeIds: string[];
	settingNames: string[];
};

const KNOWLEDGE_CHUNKS = splitKnowledge(AI_KNOWLEDGE);
const INDEXED_SETTINGS: IndexedSetting[] = AI_CATALOG.settings
	.filter((row: CatalogSetting) => row.secret !== "1")
	.map((row: CatalogSetting) => ({
		name: row.name,
		desc: row.desc,
		nameParts: row.name.split("_").filter((part) => part.length > 0),
		haystack: `${row.name} ${row.name.replace(/_/g, " ")} ${row.desc}`.toLowerCase(),
	}));

function splitKnowledge(markdown: string): KnowledgeChunk[] {
	const chunks: KnowledgeChunk[] = [];
	const parts = markdown.split(/^## /m);
	for(const part of parts) {
		const trimmed = part.trim();
		if(!trimmed || trimmed.startsWith("# "))
			continue;
		const nl = trimmed.indexOf("\n");
		const title = (nl < 0 ? trimmed : trimmed.slice(0, nl)).trim();
		const body = (nl < 0 ? "" : trimmed.slice(nl + 1)).trim();
		if(title === "Block catalog") {
			const subs = body.split(/^### /m);
			const intro = (subs[0] ?? "").trim();
			if(intro)
				chunks.push(makeChunk("block-intro", "Block catalog", `## Block catalog\n${intro}`));
			for(let i = 1; i < subs.length; i++) {
				const sub = subs[i];
				const subNl = sub.indexOf("\n");
				const subTitle = (subNl < 0 ? sub : sub.slice(0, subNl)).trim();
				const subBody = (subNl < 0 ? "" : sub.slice(subNl + 1)).trim();
				chunks.push(makeChunk(
					`block-${subTitle.toLowerCase().replace(/\s+/g, "-")}`,
					subTitle,
					`## Block catalog\n### ${subTitle}\n${subBody}`,
				));
			}
			continue;
		}
		chunks.push(makeChunk(slug(title), title, `## ${title}\n${body}`));
	}
	return chunks;
}

function makeChunk(id: string, title: string, text: string): KnowledgeChunk {
	return {id, title, text, haystack: `${title}\n${text}`.toLowerCase()};
}

function slug(title: string): string {
	return title.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "") || "chunk";
}

function tokenize(text: string): string[] {
	const lower = text.toLowerCase();
	const tokens = new Set<string>();
	for(const part of lower.split(/[^\p{L}\p{N}_]+/u)) {
		if(!part)
			continue;
		if(part.length >= 2 && !STOPWORDS.has(part))
			tokens.add(part);
		for(const piece of part.split("_")) {
			if(piece.length >= 2 && !STOPWORDS.has(piece))
				tokens.add(piece);
		}
		if(/[\uac00-\ud7a3]/.test(part)) {
			for(let i = 0; i < part.length - 1; i++)
				tokens.add(part.slice(i, i + 2));
			if(part.length >= 3) {
				for(let i = 0; i < part.length - 2; i++)
					tokens.add(part.slice(i, i + 3));
			}
		}
	}
	return [...tokens];
}

function expandTokens(query: string, tokens: string[]): Set<string> {
	const out = new Set(tokens);
	const q = query.toLowerCase();
	if(/유클|uclient|유클라이언트/.test(q)) {
		out.add("uclient");
		out.add("uc");
	}
	if(/챗|채팅|chat/.test(q))
		out.add("chat");
	if((/유클|uclient/.test(q) && /챗|채팅|chat/.test(q)) || /유클챗/.test(q))
		out.add("uc_chat");
	if(/끄|꺼|비활성|disable/.test(q)) {
		out.add("enable");
		out.add("disable");
	}
	if(/켜|활성/.test(q) && !/비활성/.test(q))
		out.add("enable");
	if(/단축어|숏컷|shortcut|automation|자동화/.test(q)) {
		out.add("shortcut");
		out.add("automation");
	}
	if(/친구|friend/.test(q))
		out.add("friend");
	if(/공지|notice/.test(q))
		out.add("notice");
	if(/계정|로그인|account|email/.test(q)) {
		out.add("account");
		out.add("email");
	}
	if(/스킨|skin/.test(q))
		out.add("skin");
	if(/바인드|키설정|controls|bind/.test(q)) {
		out.add("bind");
		out.add("controls");
	}
	if(/그래픽|graphics/.test(q))
		out.add("graphics");
	if(/사운드|소리|sound/.test(q))
		out.add("sound");
	if(/camera\s*drift|카메라/.test(q) && /drift|드리프트|camera/.test(q)) {
		out.add("camera");
		out.add("drift");
		out.add("bc_camera_drift");
	}
	return out;
}

export function replyLanguage(message: string, locale = ""): string {
	const text = message.trim();
	if(/영어로|영문으로|in english|reply in english|answer in english|speak english/i.test(text))
		return "English";
	if(/한국어로|한글로|in korean|reply in korean|answer in korean/i.test(text))
		return "Korean";
	const hangul = (text.match(/[\uac00-\ud7a3]/g) ?? []).length;
	const latin = (text.match(/[A-Za-z]/g) ?? []).length;
	if(hangul >= 2 && hangul >= latin)
		return "Korean";
	if(latin >= 3 && hangul === 0)
		return "English";
	if(hangul > latin)
		return "Korean";
	if(latin > hangul)
		return "English";
	const loc = locale.toLowerCase();
	if(loc.startsWith("ko") || loc.includes("korean"))
		return "Korean";
	if(loc.startsWith("en") || loc.includes("english"))
		return "English";
	return "English";
}

export function shortcutIntent(query: string): boolean {
	return /단축어|숏컷|shortcut|automation|자동화|만들어|만들어줘|추가해|수정해|바꿔줘|바꿔 줘|create shortcut|edit shortcut/i.test(query);
}

function settingsIntent(query: string): boolean {
	return /설정|settings|끄|꺼|켜|방법|어디|어떻게|disable|enable|option|config|콘솔|\bf1\b|끄는/i.test(query);
}

function launcherIntent(query: string): boolean {
	return /런처|launcher|친구|friend|공지|notice|계정|account|업데이트|update|플레이|playblocked|차단/i.test(query);
}

function asBoolean(value: unknown): boolean {
	return value === true || value === "true" || value === 1 || value === "1";
}

function asStringList(value: unknown): string[] {
	if(Array.isArray(value)) {
		return value
			.filter((item): item is string => typeof item === "string")
			.map((item) => item.trim())
			.filter((item) => item.length > 0)
			.slice(0, 8)
			.map((item) => item.slice(0, 80));
	}
	if(typeof value === "string" && value.trim())
		return [value.trim().slice(0, 80)];
	return [];
}

export function parseRetrievalPlan(raw: string): RetrievalPlan | null {
	const cleaned = raw
		.replace(/<reasoning>[\s\S]*?<\/reasoning>/gi, " ")
		.replace(/<think>[\s\S]*?<\/think>/gi, " ")
		.trim();
	const start = cleaned.indexOf("{");
	const end = cleaned.lastIndexOf("}");
	if(start < 0 || end <= start)
		return null;
	try {
		const parsed = JSON.parse(cleaned.slice(start, end + 1)) as Record<string, unknown>;
		const searchQueries = asStringList(parsed.search_queries ?? parsed.searchQueries);
		const intent = typeof parsed.intent === "string" ? parsed.intent.trim().slice(0, 40) : "";
		const ddnet = parseDdnetLookups(parsed.ddnet);
		const hasFlag = parsed.need_shortcut_blocks !== undefined || parsed.needShortcutBlocks !== undefined
			|| parsed.need_settings !== undefined || parsed.needSettings !== undefined
			|| parsed.need_launcher !== undefined || parsed.needLauncher !== undefined;
		if(!intent && !searchQueries.length && !hasFlag && !ddnet.length)
			return null;
		return {
			intent: intent || "question",
			searchQueries,
			needShortcutBlocks: asBoolean(parsed.need_shortcut_blocks ?? parsed.needShortcutBlocks),
			needSettings: asBoolean(parsed.need_settings ?? parsed.needSettings),
			needLauncher: asBoolean(parsed.need_launcher ?? parsed.needLauncher),
			ddnet,
		};
	}
	catch {
		return null;
	}
}

function aliasSettingNames(query: string): string[] {
	const q = query.toLowerCase();
	const names: string[] = [];
	if((/유클|uclient/.test(q) && /챗|채팅|chat/.test(q)) || /유클챗/.test(q))
		names.push("uc_chat");
	if((/챗|채팅|chat/.test(q) && /애니|animation/.test(q)) || /bc_chat_animation/.test(q))
		names.push("bc_chat_animation", "bc_chat_open_animation", "bc_chat_typing_animation");
	if(/camera\s*drift|카메라\s*드리프트/.test(q) || (/\bcamera\b/.test(q) && /\bdrift\b/.test(q)))
		names.push("bc_camera_drift", "bc_camera_drift_amount", "bc_camera_drift_smoothness", "bc_camera_drift_reverse");
	return names;
}

function scoreHaystack(haystack: string, tokens: Iterable<string>, titleBoost = 0): number {
	let score = 0;
	for(const token of tokens) {
		if(token.length < 2)
			continue;
		if(haystack.includes(token))
			score += token.length >= 4 ? 4 : 2;
	}
	return score + titleBoost;
}

function wantShortcuts(query: string, plan?: RetrievalPlan | null): boolean {
	return plan ? plan.needShortcutBlocks : shortcutIntent(query);
}

function wantSettings(query: string, plan?: RetrievalPlan | null): boolean {
	return plan ? plan.needSettings : settingsIntent(query);
}

function wantLauncher(query: string, plan?: RetrievalPlan | null): boolean {
	return plan ? plan.needLauncher : launcherIntent(query);
}

function pickKnowledge(query: string, tokens: Set<string>, plan?: RetrievalPlan | null): KnowledgeChunk[] {
	const selected = new Map<string, KnowledgeChunk>();
	const conversation = KNOWLEDGE_CHUNKS.find((chunk) => chunk.id === "conversation");
	if(conversation)
		selected.set(conversation.id, conversation);

	if(wantShortcuts(query, plan)) {
		for(const chunk of KNOWLEDGE_CHUNKS) {
			if(chunk.id === "shortcuts" || chunk.id.startsWith("block-"))
				selected.set(chunk.id, chunk);
		}
	}
	if(wantSettings(query, plan)) {
		const settings = KNOWLEDGE_CHUNKS.find((chunk) => chunk.id === "settings");
		if(settings)
			selected.set(settings.id, settings);
	}
	if(wantLauncher(query, plan)) {
		const launcher = KNOWLEDGE_CHUNKS.find((chunk) => chunk.id === "launcher");
		if(launcher)
			selected.set(launcher.id, launcher);
	}

	const ranked = KNOWLEDGE_CHUNKS
		.filter((chunk) => !selected.has(chunk.id) && chunk.id !== "shortcuts" && !chunk.id.startsWith("block-"))
		.map((chunk) => ({
			chunk,
			score: scoreHaystack(chunk.haystack, tokens, chunk.title.toLowerCase().split(/\s+/).some((word) => tokens.has(word)) ? 6 : 0),
		}))
		.filter((row) => row.score >= MIN_CHUNK_SCORE)
		.sort((a, b) => b.score - a.score)
		.slice(0, MAX_EXTRA_CHUNKS);
	for(const row of ranked)
		selected.set(row.chunk.id, row.chunk);

	const order = KNOWLEDGE_CHUNKS.map((chunk) => chunk.id);
	return [...selected.values()].sort((a, b) => order.indexOf(a.id) - order.indexOf(b.id));
}

function pickSettings(query: string, tokens: Set<string>): IndexedSetting[] {
	const forced = new Set(aliasSettingNames(query));
	const scored = INDEXED_SETTINGS.map((row) => {
		let score = 0;
		const q = query.toLowerCase();
		if(q.includes(row.name) || tokens.has(row.name))
			score += 50;
		if(forced.has(row.name))
			score += 100;
		for(const part of row.nameParts) {
			if(tokens.has(part))
				score += 12;
		}
		for(const token of tokens) {
			if(token.length >= 3 && row.name.includes(token))
				score += 8;
			if(token.length >= 4 && row.haystack.includes(token))
				score += 3;
		}
		return {row, score};
	})
		.filter((item) => item.score >= MIN_SETTING_SCORE)
		.sort((a, b) => b.score - a.score || a.row.name.localeCompare(b.row.name));

	const picked: IndexedSetting[] = [];
	const seen = new Set<string>();
	for(const name of forced) {
		const row = INDEXED_SETTINGS.find((item) => item.name === name);
		if(row && !seen.has(row.name)) {
			picked.push(row);
			seen.add(row.name);
		}
	}
	for(const item of scored) {
		if(seen.has(item.row.name))
			continue;
		picked.push(item.row);
		seen.add(item.row.name);
		if(picked.length >= MAX_SETTINGS)
			break;
	}
	return picked;
}

export function retrieveAssistantContext(query: string, plan?: RetrievalPlan | null): RetrievedAssistantContext {
	const searchText = plan?.searchQueries.length ? `${query}\n${plan.searchQueries.join("\n")}` : query;
	const tokens = expandTokens(searchText, tokenize(searchText));
	const knowledge = pickKnowledge(query, tokens, plan);
	const settings = pickSettings(searchText, tokens);
	const parts = [
		"Retrieved knowledge for this question (not the full catalog):",
		knowledge.map((chunk) => chunk.text).join("\n\n") || "(no extra knowledge)",
		"",
		"Relevant settings (name then description). Only these setting names exist for this turn:",
	];
	if(settings.length)
		parts.push(settings.map((row) => `${row.name}\t${row.desc}`).join("\n"));
	else
		parts.push("(none)");
	return {
		text: parts.join("\n"),
		knowledgeIds: knowledge.map((chunk) => chunk.id),
		settingNames: settings.map((row) => row.name),
	};
}

export function pickLiveSettings(
	values: Record<string, string | number | boolean>,
	settingNames: string[],
): Record<string, string | number | boolean> {
	const out: Record<string, string | number | boolean> = {};
	for(const name of settingNames) {
		if(Object.prototype.hasOwnProperty.call(values, name))
			out[name] = values[name];
	}
	return out;
}
