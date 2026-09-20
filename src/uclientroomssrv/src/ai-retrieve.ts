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
	replyLanguage: string;
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

function titleLanguage(value: string): string {
	return value
		.trim()
		.replace(/\s+/g, " ")
		.split(" ")
		.map((word) => word ? word[0].toUpperCase() + word.slice(1).toLowerCase() : "")
		.join(" ");
}

function localeLanguage(locale: string): string {
	const loc = locale.trim().toLowerCase().replace(/_/g, "-");
	if(!loc)
		return "";
	if(loc.startsWith("zh-tw") || loc.startsWith("zh-hk") || loc.startsWith("zh-mo") || loc === "zh-hant")
		return "Traditional Chinese";
	if(loc.startsWith("zh"))
		return "Simplified Chinese";
	if(loc.startsWith("ko"))
		return "Korean";
	if(loc.startsWith("ja"))
		return "Japanese";
	if(loc.startsWith("en"))
		return "English";
	if(loc.startsWith("de"))
		return "German";
	if(loc.startsWith("fr"))
		return "French";
	if(loc.startsWith("es"))
		return "Spanish";
	if(loc.startsWith("pt"))
		return "Portuguese";
	if(loc.startsWith("ru"))
		return "Russian";
	if(loc.startsWith("vi"))
		return "Vietnamese";
	if(loc.startsWith("th"))
		return "Thai";
	if(loc.startsWith("ar"))
		return "Arabic";
	if(loc.startsWith("it"))
		return "Italian";
	if(loc.startsWith("pl"))
		return "Polish";
	if(loc.startsWith("tr"))
		return "Turkish";
	if(loc.startsWith("uk"))
		return "Ukrainian";
	if(loc.startsWith("nl"))
		return "Dutch";
	const base = loc.split("-")[0] ?? "";
	return base.length === 2 ? titleLanguage(base) : "";
}

function plannedLanguage(value: unknown): string {
	const raw = typeof value === "string" ? value.trim() : "";
	if(!raw || raw.length > 40)
		return "";
	const text = raw.toLowerCase().replace(/_/g, "-");
	if(text.includes("traditional") || text.startsWith("zh-tw") || text.startsWith("zh-hk") || text.startsWith("zh-mo") || text.includes("zh-hant"))
		return "Traditional Chinese";
	if(text.includes("simplified") || text.startsWith("zh-cn") || text === "zh" || text.startsWith("zh-hans") || text.includes("chinese"))
		return "Simplified Chinese";
	if(text === "en" || text.startsWith("en-") || text.includes("english"))
		return "English";
	if(text === "ko" || text.startsWith("ko-") || text.includes("korean"))
		return "Korean";
	if(text === "ja" || text.startsWith("ja-") || text.includes("japanese"))
		return "Japanese";
	if(localeLanguage(text))
		return localeLanguage(text);
	if(/^[a-z][a-z0-9\s\-()]{1,39}$/i.test(raw))
		return titleLanguage(raw);
	return "";
}

function scriptLanguage(message: string, locale = ""): string {
	const hangul = (message.match(/[\uac00-\ud7a3]/g) ?? []).length;
	const kana = (message.match(/[\u3040-\u30ff]/g) ?? []).length;
	const han = (message.match(/[\u4e00-\u9fff]/g) ?? []).length;
	const cyrillic = (message.match(/[\u0400-\u04ff]/g) ?? []).length;
	const arabic = (message.match(/[\u0600-\u06ff]/g) ?? []).length;
	const thai = (message.match(/[\u0e00-\u0e7f]/g) ?? []).length;
	const latin = (message.match(/[A-Za-z]/g) ?? []).length;
	if(hangul >= 2)
		return "Korean";
	if(kana >= 2)
		return "Japanese";
	if(han >= 2)
		return localeLanguage(locale).includes("Traditional") ? "Traditional Chinese" : "Simplified Chinese";
	if(cyrillic >= 2)
		return "Russian";
	if(arabic >= 2)
		return "Arabic";
	if(thai >= 2)
		return "Thai";
	if(latin >= 3)
		return "English";
	return "";
}

export function replyLanguage(message: string, locale = "", planned = ""): string {
	const fromPlan = plannedLanguage(planned);
	if(fromPlan)
		return fromPlan;
	return scriptLanguage(message, locale) || localeLanguage(locale) || "English";
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
		const replyLanguage = plannedLanguage(parsed.reply_language ?? parsed.replyLanguage);
		const hasFlag = parsed.need_shortcut_blocks !== undefined || parsed.needShortcutBlocks !== undefined
			|| parsed.need_settings !== undefined || parsed.needSettings !== undefined
			|| parsed.need_launcher !== undefined || parsed.needLauncher !== undefined
			|| !!replyLanguage;
		if(!intent && !searchQueries.length && !hasFlag && !ddnet.length)
			return null;
		return {
			intent: intent || "question",
			searchQueries,
			needShortcutBlocks: asBoolean(parsed.need_shortcut_blocks ?? parsed.needShortcutBlocks),
			needSettings: asBoolean(parsed.need_settings ?? parsed.needSettings),
			needLauncher: asBoolean(parsed.need_launcher ?? parsed.needLauncher),
			replyLanguage,
			ddnet,
		};
	}
	catch {
		return null;
	}
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

function pickKnowledge(tokens: Set<string>, plan?: RetrievalPlan | null): KnowledgeChunk[] {
	const selected = new Map<string, KnowledgeChunk>();
	const conversation = KNOWLEDGE_CHUNKS.find((chunk) => chunk.id === "conversation");
	if(conversation)
		selected.set(conversation.id, conversation);

	if(plan?.needShortcutBlocks) {
		for(const chunk of KNOWLEDGE_CHUNKS) {
			if(chunk.id === "shortcuts" || chunk.id.startsWith("block-"))
				selected.set(chunk.id, chunk);
		}
	}
	if(plan?.needSettings) {
		const settings = KNOWLEDGE_CHUNKS.find((chunk) => chunk.id === "settings");
		if(settings)
			selected.set(settings.id, settings);
	}
	if(plan?.needLauncher) {
		for(const id of ["launcher", "file-backup"]) {
			const chunk = KNOWLEDGE_CHUNKS.find((item) => item.id === id);
			if(chunk)
				selected.set(chunk.id, chunk);
		}
	}
	for(const query of plan?.searchQueries ?? []) {
		const id = slug(query);
		const chunk = KNOWLEDGE_CHUNKS.find((item) => item.id === id || item.title.toLowerCase() === query.trim().toLowerCase());
		if(chunk && chunk.id !== "shortcuts" && !chunk.id.startsWith("block-"))
			selected.set(chunk.id, chunk);
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
	const scored = INDEXED_SETTINGS.map((row) => {
		let score = 0;
		const q = query.toLowerCase();
		if(q.includes(row.name) || tokens.has(row.name))
			score += 50;
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
	const tokens = new Set(tokenize(searchText));
	const knowledge = pickKnowledge(tokens, plan);
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

export type BindRow = {
	key: string;
	command: string;
};

export function pickBinds(binds: BindRow[] | undefined, query: string, plan?: RetrievalPlan | null): BindRow[] {
	const rows = (binds ?? [])
		.filter((row) => row && typeof row.key === "string" && typeof row.command === "string" && row.key && row.command)
		.slice(0, 300);
	if(!rows.length)
		return [];
	const searchText = plan?.searchQueries.length ? `${query}\n${plan.searchQueries.join("\n")}` : query;
	const tokens = new Set(tokenize(searchText));
	const lower = searchText.toLowerCase();
	const scored = rows.map((row) => {
		const hay = `${row.key} ${row.command}`.toLowerCase();
		let score = 0;
		if(lower.includes(row.key.toLowerCase()))
			score += 20;
		if(lower.includes(row.command.toLowerCase()))
			score += 20;
		for(const token of tokens) {
			if(token.length < 2)
				continue;
			if(hay.includes(token))
				score += token.length >= 4 ? 4 : 2;
		}
		return {row, score};
	})
		.filter((item) => item.score >= 2)
		.sort((a, b) => b.score - a.score || a.row.key.localeCompare(b.row.key));
	return scored.slice(0, 40).map((item) => item.row);
}
