import {fallbackDdnetLookups, fetchDdnetLookups} from "./ai-ddnet";
import {parseRetrievalPlan, pickLiveSettings, replyLanguage, retrieveAssistantContext, type RetrievalPlan} from "./ai-retrieve";

export interface AiEnv {
	DB: D1Database;
	BEDROCK_API_KEY?: string;
	BEDROCK_REGION?: string;
	BEDROCK_MODEL_ID?: string;
	BEDROCK_ENDPOINT?: string;
	BEDROCK_PROJECT_ID?: string;
}

type ChatTurn = {
	role: "user" | "assistant";
	content: string;
};

type AiChatBody = {
	locale?: string;
	messages?: ChatTurn[];
	settingsValues?: Record<string, string | number | boolean>;
	settingsSource?: string;
	shortcuts?: Array<{id?: string; name?: string; kind?: string; enabled?: boolean; trigger?: unknown; actions?: unknown; actionCount?: number}>;
	friends?: Array<{name?: string; online?: boolean; afk?: boolean; server?: string; map?: string}>;
	launcher?: Record<string, unknown>;
};

const JSON_HEADERS = {
	"content-type": "application/json; charset=utf-8",
	"cache-control": "no-store",
} as const;
const AI_WINDOW_SECONDS = 60;
const AI_LIMIT_PER_MINUTE = 20;
const AI_DAY_SECONDS = 24 * 60 * 60;
const AI_LIMIT_PER_DAY = 200;
const AI_MAX_BODY_BYTES = 256 * 1024;
const AI_MAX_TURNS = 16;
const SECRET_NAME = /(password|token|secret|_key|apikey|uuid)/i;

function json(body: unknown, status = 200): Response {
	return new Response(JSON.stringify(body), {status, headers: JSON_HEADERS});
}

function error(status: number, code: string, message: string): Response {
	return json({error: code, message}, status);
}

function systemPrompt(): string {
	return [
		"You are the UClient/BestClient launcher personal assistant.",
		"Answer only about this launcher, this client, shortcuts/automation, friends, notices, accounts, retrieved settings, and official DDNet ranks/maps/wiki when a DDNet lookup is attached.",
		"Reply only in the Reply language stated at the top of the user message. That follows the latest user message, or an explicit request like 영어로, otherwise the Windows display language. If it is English, write English only.",
		"Keep spoken replies short. A greeting is only a brief hello plus asking how you can help, in the Reply language. Do not mention version, whether the client is running, notices, friends, settings, or play status unless they asked. Do not volunteer extra status from the snapshot.",
		"Read the latest user message with the conversation. Only emit a ```uclient-shortcut fence when they clearly ask to create, edit, add, or change a shortcut/단축어/automation. Questions, status checks, explanations, and follow-ups are not shortcut requests. After they already made one, a new question is just a question.",
		"Answer those questions in sentences from the live snapshot (settingsValues, friends, shortcuts, launcher). Do not invent a shortcut to answer. If a live value is missing, say so. Current server name and map are not config keys; they are not in the snapshot. If they ask the server name now, say you cannot see the live server from here unless they explicitly ask you to make a shortcut that reads it in-game.",
		"Each turn includes retrieved knowledge and only the setting rows that match the question. A planner chose those rows from the latest message. Use those plus the live snapshot. Do not invent settings, menus, or shortcut blocks that are not there. If the block catalog is missing, do not emit a shortcut.",
		"Game settings are only in the running client. Use the menu map in the retrieved knowledge (Esc, Settings, then General/Appearance/TClient/BestClient/UClient). Never send them to launcher Settings. UClient chat: Esc, Settings, UClient, Others, Enable UClient chat. Chat look is Appearance, Chat. Binds are General, Controls. Tee skin is General, Tee.",
		"Spoken replies may wrap short emphasis in **double asterisks**; the launcher shows that as bold. Do not use other markdown: no headings, backticks, or bullet asterisks.",
		"Use only triggers and actions from the retrieved block catalog, with the JSON shapes shown there. Combine those blocks to match the request. If you cannot do it, say so. Do not invent types or config values.",
		"When creating a shortcut, emit exactly one fenced block whose first line is ```uclient-shortcut, then the shortcut object, then ```. Never use ```json. Never paste the object in the spoken reply.",
		"To edit an existing shortcut, keep its id from the snapshot. To create a new one, omit id.",
		"chat_received must use filters:[]. Never put message/sender filters on the trigger. If the user names chat text, wrap actions in {type:\"if\",left:{source:\"messageText\",get:\"text\"},op:\"contains\",right:\"that text\"} then the then-actions and {type:\"end_if\"}.",
		"Use shortcut variables on your own when the result should include sender, received text, clipboard, player info, ask input, map, or name. Fixed phrases stay mode text. Mix words and variables with a text action. Nested {mode:\"variable\",variable:{source,get}} — never channelMode/messageText.",
		"Any other condition, branch, otherwise, else, or sender check also uses if/end_if (optional otherwise). Never skip if/end_if for a branch.",
		"In the spoken reply, call it a shortcut or 단축어. Never say JSON, code, block, or uclient-shortcut to the user.",
		"If an official DDNet lookup is attached, use only that for official DDNet player ranks, official DDNet maps, mappers, releases, and wiki facts. Do not invent missing ranks. You may point to the ddnet.org or wiki.ddnet.org links in the lookup.",
		"Map and player lookup is official DDNet only. If they ask about Gores, fng, or any other mode map or player, say you only know official DDNet maps and official DDNet ranked players. Do not guess those.",
		"If they ask to make a shortcut but do not say what it should do, ask what they want. Do not emit a shortcut fence yet. If they ask to search a player or a map but give no name, ask for the official DDNet name.",
		"Refuse other off-topic questions (cooking, homework, general web) and steer back to UClient or official DDNet data.",
		"Never request or repeat secrets, passwords, tokens, API keys, or install UUIDs.",
		"Never write reasoning, analysis, or tags such as <reasoning> in the reply. Only write the user-facing answer.",
		"Launcher notices: use title and body from the snapshot. Speak in normal sentences. Never mention severity, warning, critical, info, or field names like blocksPlay.",
		"If they ask about Play and playBlocked is true, tell them they cannot play right now, naturally (for example in Korean: 현재 플레이는 차단이 되어 있어 플레이할 수가 없어요). Do not mention blocking on a greeting. If playBlocked is absent, do not mention blocking.",
	].join(" ");
}

function sanitizeSettings(values: Record<string, string | number | boolean> | undefined): Record<string, string | number | boolean> {
	const out: Record<string, string | number | boolean> = {};
	if(!values)
		return out;
	for(const [name, value] of Object.entries(values)) {
		if(SECRET_NAME.test(name))
			continue;
		if(typeof value === "string" && value.length > 512)
			out[name] = value.slice(0, 512);
		else
			out[name] = value;
	}
	return out;
}

function summarizeShortcuts(shortcuts: AiChatBody["shortcuts"]): unknown {
	return (shortcuts ?? []).slice(0, 80).map((item) => ({
		id: item.id,
		name: item.name,
		kind: item.kind,
		enabled: item.enabled,
		trigger: item.trigger ?? null,
		actions: Array.isArray(item.actions) ? item.actions.slice(0, 40) : [],
	}));
}

function summarizeFriends(friends: AiChatBody["friends"]): unknown {
	return (friends ?? []).slice(0, 80).map((friend) => ({
		name: friend.name,
		online: !!friend.online,
		afk: !!friend.afk,
		server: friend.server || "",
		map: friend.map || "",
	}));
}

function sanitizeLauncher(launcher: Record<string, unknown> | undefined): Record<string, unknown> {
	const src = launcher && typeof launcher === "object" ? launcher : {};
	const raw = Array.isArray(src.notices) ? src.notices : [];
	const notices: Array<{title: string; body: string}> = [];
	let playBlocked = src.playBlocked === true;
	for(const item of raw.slice(0, 10)) {
		if(!item || typeof item !== "object")
			continue;
		const row = item as Record<string, unknown>;
		if(row.blocksPlay === true || row.blocks_play === true)
			playBlocked = true;
		const title = typeof row.title === "string" ? row.title.slice(0, 200) : "";
		const body = typeof row.body === "string" ? row.body.slice(0, 4000) : "";
		if(title || body)
			notices.push({title, body});
	}
	const out: Record<string, unknown> = {};
	for(const [key, value] of Object.entries(src)) {
		if(key === "notices" || key === "playBlocked")
			continue;
		out[key] = value;
	}
	out.notices = notices;
	if(playBlocked)
		out.playBlocked = true;
	return out;
}

async function rateLimited(db: D1Database, installId: string, now: number): Promise<boolean> {
	const minute = await db.prepare(
		"SELECT COUNT(*) AS count FROM ai_requests WHERE install_id = ?1 AND created_at > ?2",
	).bind(installId, now - AI_WINDOW_SECONDS).first<{count: number}>();
	if((minute?.count ?? 0) >= AI_LIMIT_PER_MINUTE)
		return true;
	const day = await db.prepare(
		"SELECT COUNT(*) AS count FROM ai_requests WHERE install_id = ?1 AND created_at > ?2",
	).bind(installId, now - AI_DAY_SECONDS).first<{count: number}>();
	return (day?.count ?? 0) >= AI_LIMIT_PER_DAY;
}

function usesMantle(env: AiEnv): boolean {
	return (env.BEDROCK_ENDPOINT || "runtime").toLowerCase() === "mantle";
}

function usesOss(env: AiEnv): boolean {
	return modelId(env).toLowerCase().includes("gpt-oss");
}

function bedrockUrl(env: AiEnv): string {
	const region = env.BEDROCK_REGION || "us-east-1";
	if(usesMantle(env)) {
		if(usesOss(env))
			return `https://bedrock-mantle.${region}.api.aws/v1/responses`;
		return `https://bedrock-mantle.${region}.api.aws/openai/v1/responses`;
	}
	if(usesOss(env))
		return `https://bedrock-runtime.${region}.amazonaws.com/openai/v1/chat/completions`;
	return `https://bedrock-runtime.${region}.amazonaws.com/openai/v1/responses`;
}

function modelId(env: AiEnv): string {
	if(env.BEDROCK_MODEL_ID)
		return env.BEDROCK_MODEL_ID;
	return usesMantle(env) ? "openai.gpt-oss-120b" : "openai.gpt-oss-120b-1:0";
}

function bedrockBody(env: AiEnv, instructions: string, input: string, options?: {stream?: boolean; maxTokens?: number}): Record<string, unknown> {
	const stream = options?.stream !== false;
	const maxTokens = options?.maxTokens;
	if(usesOss(env) && !usesMantle(env)) {
		const body: Record<string, unknown> = {
			model: modelId(env),
			messages: [
				{role: "system", content: instructions},
				{role: "user", content: input},
			],
			stream,
		};
		if(maxTokens)
			body.max_tokens = maxTokens;
		return body;
	}
	const body: Record<string, unknown> = {
		model: modelId(env),
		instructions,
		input,
		stream,
	};
	if(maxTokens)
		body.max_output_tokens = maxTokens;
	if(stream) {
		body.prompt_cache_key = "uclient-assistant-v8";
		body.reasoning = {effort: "none"};
	}
	return body;
}

function bedrockApiKey(env: AiEnv): string {
	return (env.BEDROCK_API_KEY || "").trim().replace(/^["']|["']$/g, "");
}

function bedrockDetailMessage(detail: string): string {
	try {
		const parsed = JSON.parse(detail) as {message?: unknown; error?: {message?: unknown}};
		const nested = typeof parsed.error?.message === "string" ? parsed.error.message : "";
		const msg = typeof parsed.message === "string" ? parsed.message : nested;
		if(msg)
			return msg.replace(/\s+/g, " ").trim().slice(0, 180);
	}
	catch {
		// not JSON
	}
	return detail.replace(/\s+/g, " ").trim().slice(0, 180);
}

function bedrockUserMessage(status: number, detail: string): string {
	const aws = bedrockDetailMessage(detail);
	const lower = aws.toLowerCase();
	if(lower.includes("invalid bearer") || lower.includes("invalid api key"))
		return "Amazon Bedrock rejected the API key. Re-copy the long-term key from us-east-1 without quotes.";
	if(lower.includes("not available for this account"))
		return "This AWS account cannot call that Bedrock model. Try gpt-oss-120b in the Playground, or ask AWS Support to enable GPT-5.6.";
	if(status === 401 || status === 403)
		return aws || "Amazon Bedrock denied the call. Use a long-term API key from the Bedrock console in us-east-1.";
	if(status === 404)
		return "Amazon Bedrock could not find that model in this region.";
	if(status === 429)
		return "Amazon Bedrock is rate limiting. Try again shortly.";
	if(status >= 400 && status < 500)
		return aws ? `Amazon Bedrock rejected the request (${status}): ${aws}` : `Amazon Bedrock rejected the request (${status}).`;
	return `The assistant could not complete that request. (${status || "no response"})`;
}

const HIDDEN_TAGS = ["reasoning", "think"] as const;

function indexOfIgnoreCase(haystack: string, needle: string): number {
	return haystack.toLowerCase().indexOf(needle.toLowerCase());
}

function longestPrefixHold(text: string, candidates: string[]): number {
	const lower = text.toLowerCase();
	let hold = 0;
	for(const candidate of candidates) {
		const want = candidate.toLowerCase();
		const max = Math.min(text.length, want.length - 1);
		for(let n = max; n > 0; n--) {
			if(want.startsWith(lower.slice(-n))) {
				hold = Math.max(hold, n);
				break;
			}
		}
	}
	return hold;
}

export function createHiddenTagFilter(): {feed: (chunk: string) => string; flush: () => string} {
	let pending = "";
	let hiding: string | null = null;
	const opens = HIDDEN_TAGS.map((name) => `<${name}>`);
	const feed = (chunk: string): string => {
		pending += chunk;
		let visible = "";
		for(;;) {
			if(hiding) {
				const idx = indexOfIgnoreCase(pending, hiding);
				if(idx < 0) {
					const hold = longestPrefixHold(pending, [hiding]);
					pending = hold ? pending.slice(-hold) : "";
					break;
				}
				pending = pending.slice(idx + hiding.length);
				hiding = null;
				continue;
			}
			let best = -1;
			let bestName = "";
			for(const name of HIDDEN_TAGS) {
				const idx = indexOfIgnoreCase(pending, `<${name}>`);
				if(idx >= 0 && (best < 0 || idx < best)) {
					best = idx;
					bestName = name;
				}
			}
			if(best < 0) {
				const hold = longestPrefixHold(pending, opens);
				visible += pending.slice(0, pending.length - hold);
				pending = hold ? pending.slice(-hold) : "";
				break;
			}
			visible += pending.slice(0, best);
			pending = pending.slice(best + `<${bestName}>`.length);
			hiding = `</${bestName}>`;
		}
		return visible;
	};
	const flush = (): string => (hiding ? "" : pending);
	return {feed, flush};
}

export function visibleAssistantText(raw: string): string {
	const filter = createHiddenTagFilter();
	return filter.feed(raw) + filter.flush();
}

function extractDelta(payload: Record<string, unknown>): string {
	if(typeof payload.delta === "string")
		return payload.delta;
	const delta = payload.delta;
	if(delta && typeof delta === "object" && "text" in delta && typeof (delta as {text: unknown}).text === "string")
		return (delta as {text: string}).text;
	if(typeof payload.text === "string" && payload.type === "response.output_text.delta")
		return payload.text;
	const choices = payload.choices;
	if(Array.isArray(choices) && choices[0] && typeof choices[0] === "object") {
		const choice = choices[0] as {delta?: {content?: unknown}};
		if(typeof choice.delta?.content === "string")
			return choice.delta.content;
	}
	return "";
}

function plannerPrompt(): string {
	return [
		"You plan retrieval for the UClient launcher assistant. Output JSON only. No markdown, no reasoning.",
		"Read the latest user message using the conversation. A follow-up question is not a shortcut request.",
		'JSON shape: {"intent":"settings|shortcut_create|shortcut_edit|status|friends|notices|ddnet|off_topic|mixed","search_queries":["..."],"need_shortcut_blocks":false,"need_settings":false,"need_launcher":false,"ddnet":[]}',
		"search_queries: 1 to 5 short Korean or English terms to find docs and config keys (cl_, tc_, uc_, bc_). Include likely key names when you know them.",
		"need_shortcut_blocks: true only if they clearly ask to create, edit, add, or change a shortcut/단축어/automation.",
		"need_settings: true if they ask how to change, find, enable, disable, or explain a client setting.",
		"need_launcher: true for friends, notices, account, updates, or Play.",
		"ddnet: 0 to 2 official lookups. Types: player (ranks/points), map (one map), mapper (maps by mapper name), releases (recent official maps), wiki (DDNet wiki). Each item is {\"type\":\"player\",\"query\":\"name\"}. releases may omit query. Only official DDNet race maps and official DDNet ranked players. Never for Gores, fng, or other modes. Only when they name a player/map or ask official releases/wiki. Never invent a name. Never for greetings, UClient settings, or shortcuts.",
		"Known keys: UClient chat on/off is uc_chat. Chat animations are bc_chat_animation. Camera drift is bc_camera_drift. Chat look is Appearance Chat, not uc_chat.",
		"You may aim local queries at: Launcher, Settings menu map, Shortcuts, Block catalog.",
	].join(" ");
}

function extractCompletionText(payload: Record<string, unknown>): string {
	const choices = payload.choices;
	if(Array.isArray(choices) && choices[0] && typeof choices[0] === "object") {
		const choice = choices[0] as {message?: {content?: unknown}; text?: unknown};
		if(typeof choice.message?.content === "string")
			return choice.message.content;
		if(typeof choice.text === "string")
			return choice.text;
	}
	if(typeof payload.output_text === "string")
		return payload.output_text;
	const output = payload.output;
	if(Array.isArray(output)) {
		const texts: string[] = [];
		for(const item of output) {
			if(!item || typeof item !== "object")
				continue;
			const content = (item as {content?: unknown}).content;
			if(typeof content === "string")
				texts.push(content);
			if(Array.isArray(content)) {
				for(const part of content) {
					if(part && typeof part === "object" && typeof (part as {text?: unknown}).text === "string")
						texts.push((part as {text: string}).text);
				}
			}
		}
		if(texts.length)
			return texts.join("");
	}
	return "";
}

async function planAssistantRetrieval(
	env: AiEnv,
	query: string,
	recent: string,
	locale: string,
): Promise<RetrievalPlan | null> {
	const key = bedrockApiKey(env);
	if(!key)
		return null;
	try {
		const headers: Record<string, string> = {
			authorization: `Bearer ${key}`,
			"content-type": "application/json",
		};
		if(env.BEDROCK_PROJECT_ID)
			headers["openai-project"] = env.BEDROCK_PROJECT_ID.trim();
		const upstream = await fetch(bedrockUrl(env), {
			method: "POST",
			headers,
			body: JSON.stringify(bedrockBody(env, plannerPrompt(), [
				"Reply language: " + replyLanguage(query, locale),
				"OS locale: " + (locale || "unknown"),
				"Latest user message:",
				query.slice(0, 4000),
				"",
				"Recent conversation:",
				recent.slice(0, 6000) || "(none)",
			].join("\n"), {stream: false, maxTokens: 400})),
			signal: AbortSignal.timeout(8000),
		});
		if(!upstream.ok) {
			const detail = await upstream.text().catch(() => "");
			console.error(JSON.stringify({
				event: "bedrock_plan_failed",
				status: upstream.status,
				detail: detail.slice(0, 400),
			}));
			return null;
		}
		const payload = await upstream.json<Record<string, unknown>>();
		const plan = parseRetrievalPlan(extractCompletionText(payload));
		if(plan) {
			console.log(JSON.stringify({
				event: "assistant_plan",
				intent: plan.intent,
				searchQueries: plan.searchQueries,
				needShortcutBlocks: plan.needShortcutBlocks,
				needSettings: plan.needSettings,
				needLauncher: plan.needLauncher,
				ddnet: plan.ddnet,
			}));
		}
		return plan;
	}
	catch(errorValue) {
		console.error(JSON.stringify({
			event: "bedrock_plan_failed",
			message: errorValue instanceof Error ? errorValue.message : String(errorValue),
		}));
		return null;
	}
}

function requestLocation(request: Request): {colo?: string; country?: string} {
	const cf = request.cf as {colo?: string; country?: string} | undefined;
	return {colo: cf?.colo, country: cf?.country};
}

async function streamBedrock(env: AiEnv, request: Request, instructions: string, input: string): Promise<Response> {
	const key = bedrockApiKey(env);
	if(!key)
		return error(503, "ai_unconfigured", "The assistant is not configured.");

	const headers: Record<string, string> = {
		authorization: `Bearer ${key}`,
		"content-type": "application/json",
	};
	if(env.BEDROCK_PROJECT_ID)
		headers["openai-project"] = env.BEDROCK_PROJECT_ID.trim();

	const upstream = await fetch(bedrockUrl(env), {
		method: "POST",
		headers,
		body: JSON.stringify(bedrockBody(env, instructions, input)),
	});
	if(!upstream.ok || !upstream.body) {
		const detail = await upstream.text().catch(() => "");
		console.error(JSON.stringify({
			event: "bedrock_failed",
			status: upstream.status,
			endpoint: usesMantle(env) ? "mantle" : "runtime",
			model: modelId(env),
			...requestLocation(request),
			detail: detail.slice(0, 800),
		}));
		return error(502, "ai_upstream", bedrockUserMessage(upstream.status, detail));
	}

	const encoder = new TextEncoder();
	const decoder = new TextDecoder();
	let leftover = "";
	const hidden = createHiddenTagFilter();
	const stream = new ReadableStream<Uint8Array>({
		async start(controller) {
			const reader = upstream.body!.getReader();
			const send = (obj: unknown) => {
				controller.enqueue(encoder.encode(`data: ${JSON.stringify(obj)}\n\n`));
			};
			try {
				for(;;) {
					const {done, value} = await reader.read();
					if(done)
						break;
					leftover += decoder.decode(value, {stream: true});
					const chunks = leftover.split("\n\n");
					leftover = chunks.pop() ?? "";
					for(const chunk of chunks) {
						for(const line of chunk.split("\n")) {
							const trimmed = line.trim();
							if(!trimmed.startsWith("data:"))
								continue;
							const data = trimmed.slice(5).trim();
							if(!data || data === "[DONE]")
								continue;
							try {
								const parsed = JSON.parse(data) as Record<string, unknown>;
								const text = extractDelta(parsed);
								const visible = text ? hidden.feed(text) : "";
								if(visible)
									send({text: visible});
							}
							catch {
								// ignore malformed SSE leftovers
							}
						}
					}
				}
				const tail = hidden.flush();
				if(tail)
					send({text: tail});
				send({done: true});
			}
			catch(errorValue) {
				console.error(JSON.stringify({
					event: "bedrock_stream_failed",
					message: errorValue instanceof Error ? errorValue.message : String(errorValue),
				}));
				send({error: "stream_failed"});
			}
			finally {
				controller.close();
			}
		},
	});

	return new Response(stream, {
		status: 200,
		headers: {
			"content-type": "text/event-stream; charset=utf-8",
			"cache-control": "no-store",
			"x-accel-buffering": "no",
		},
	});
}

export async function handleAiChat(
	request: Request,
	env: AiEnv,
	installId: string,
): Promise<Response> {
	if(request.method !== "POST")
		return error(405, "method_not_allowed", "Use POST.");
	const now = Math.floor(Date.now() / 1000);
	if(await rateLimited(env.DB, installId, now))
		return error(429, "rate_limited", "Too many assistant requests. Try again shortly.");

	const length = Number(request.headers.get("content-length") ?? "0");
	if(length > AI_MAX_BODY_BYTES)
		return error(413, "payload_too_large", "The assistant request is too large.");

	let body: AiChatBody;
	try {
		body = await request.json<AiChatBody>();
	}
	catch {
		return error(400, "invalid_json", "Invalid JSON.");
	}

	const messages = (body.messages ?? [])
		.filter((item) => item && (item.role === "user" || item.role === "assistant") && typeof item.content === "string")
		.slice(-AI_MAX_TURNS);
	if(!messages.length || messages[messages.length - 1]?.role !== "user")
		return error(400, "missing_message", "Send a user message.");

	await env.DB.prepare(
		"INSERT INTO ai_requests(install_id, created_at) VALUES (?1, ?2)",
	).bind(installId, now).run();
	await env.DB.prepare(
		"DELETE FROM ai_requests WHERE created_at <= ?1",
	).bind(now - AI_DAY_SECONDS).run();

	const lastUser = messages[messages.length - 1]?.content ?? "";
	const locale = typeof body.locale === "string" ? body.locale.slice(0, 32) : "";
	const language = replyLanguage(lastUser, locale);
	const recent = messages.slice(-6).map((item) => `${item.role}: ${item.content.slice(0, 1500)}`).join("\n\n");
	const plan = await planAssistantRetrieval(env, lastUser, recent, locale);
	const retrieved = retrieveAssistantContext(lastUser, plan);
	const ddnetLookups = plan?.ddnet.length ? plan.ddnet : fallbackDdnetLookups(lastUser);
	const ddnetText = await fetchDdnetLookups(ddnetLookups);
	const snapshot = {
		locale,
		settingsSource: body.settingsSource || "unknown",
		settingsValues: pickLiveSettings(sanitizeSettings(body.settingsValues), retrieved.settingNames),
		shortcuts: summarizeShortcuts(body.shortcuts),
		friends: summarizeFriends(body.friends),
		launcher: sanitizeLauncher(body.launcher),
	};
	const transcript = messages.map((item) => `${item.role}: ${item.content.slice(0, 8000)}`).join("\n\n");
	const input = [
		`Reply language: ${language}. Write the whole spoken reply in ${language} only. Ignore OS locale if it disagrees.`,
		"OS locale: " + (snapshot.locale || "unknown"),
		retrieved.text,
		ddnetText ? `\n${ddnetText}` : "",
		"",
		"Live snapshot JSON:",
		JSON.stringify(snapshot),
		"",
		"Conversation:",
		transcript,
	].join("\n");

	return streamBedrock(env, request, systemPrompt(), input);
}
