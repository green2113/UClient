export const ASSISTANT_TOOLS = [
	{
		type: "function",
		function: {
			name: "ask_question",
			description: "Show the user a short multiple-choice question as launcher buttons. Use when you cannot continue without a pick. 2 to 5 options. Speak one short sentence first. Do not list the options in speech.",
			parameters: {
				type: "object",
				properties: {
					prompt: {type: "string", description: "Short question above the buttons"},
					options: {
						type: "array",
						minItems: 2,
						maxItems: 5,
						items: {
							type: "object",
							properties: {
								id: {type: "string"},
								label: {type: "string"},
							},
							required: ["label"],
						},
					},
				},
				required: ["prompt", "options"],
			},
		},
	},
	{
		type: "function",
		function: {
			name: "emit_shortcut",
			description: "Give the user one shortcut or automation to save. Same object as a uclient-shortcut fence: name, kind, optional id, optional trigger, actions. Do not also emit a fence.",
			parameters: {
				type: "object",
				properties: {
					name: {type: "string"},
					kind: {type: "string", enum: ["manual", "automation"]},
					id: {type: "string"},
					trigger: {type: "object"},
					actions: {type: "array"},
				},
				required: ["name", "kind", "actions"],
			},
		},
	},
] as const;

export type AskQuestion = {
	prompt: string;
	options: Array<{id: string; label: string}>;
};

export type ToolDelta = {
	index: number;
	id?: string;
	name?: string;
	arguments?: string;
};

export function extractToolDeltas(payload: Record<string, unknown>): ToolDelta[] {
	const choices = payload.choices;
	if(Array.isArray(choices) && choices[0] && typeof choices[0] === "object") {
		const calls = (choices[0] as {delta?: {tool_calls?: unknown}}).delta?.tool_calls;
		if(Array.isArray(calls)) {
			const out: ToolDelta[] = [];
			for(const item of calls) {
				if(!item || typeof item !== "object")
					continue;
				const row = item as {index?: unknown; id?: unknown; function?: {name?: unknown; arguments?: unknown}};
				const index = typeof row.index === "number" ? row.index : out.length;
				const name = typeof row.function?.name === "string" ? row.function.name : "";
				const args = typeof row.function?.arguments === "string" ? row.function.arguments : "";
				const id = typeof row.id === "string" ? row.id : "";
				out.push({
					index,
					id: id || undefined,
					name: name || undefined,
					arguments: args || undefined,
				});
			}
			return out;
		}
	}
	if(payload.type === "response.function_call_arguments.delta" && typeof payload.delta === "string") {
		const index = typeof payload.output_index === "number" ? payload.output_index : 0;
		const name = typeof payload.name === "string" ? payload.name : "";
		return [{index, name: name || undefined, arguments: payload.delta}];
	}
	return [];
}

export function parseAskQuestionArgs(raw: unknown): AskQuestion | null {
	if(!raw || typeof raw !== "object")
		return null;
	const row = raw as Record<string, unknown>;
	const prompt = typeof row.prompt === "string" ? row.prompt.replace(/\s+/g, " ").trim().slice(0, 200) : "";
	if(!prompt)
		return null;
	const src = Array.isArray(row.options) ? row.options : [];
	const options: AskQuestion["options"] = [];
	const seen = new Set<string>();
	for(const item of src) {
		if(!item || typeof item !== "object")
			continue;
		const opt = item as Record<string, unknown>;
		const label = typeof opt.label === "string" ? opt.label.replace(/\s+/g, " ").trim().slice(0, 80) : "";
		if(!label || seen.has(label.toLowerCase()))
			continue;
		seen.add(label.toLowerCase());
		const id = typeof opt.id === "string" && opt.id.trim() ? opt.id.trim().slice(0, 40) : `opt${options.length + 1}`;
		options.push({id, label});
		if(options.length >= 5)
			break;
	}
	return options.length >= 2 ? {prompt, options} : null;
}

export function parseEmitShortcutArgs(raw: unknown): Record<string, unknown> | null {
	if(!raw || typeof raw !== "object")
		return null;
	const row = raw as Record<string, unknown>;
	const src = row.shortcut && typeof row.shortcut === "object" && !Array.isArray(row.shortcut)
		? row.shortcut as Record<string, unknown>
		: row;
	const name = typeof src.name === "string" ? src.name.trim() : "";
	if(!name || !Array.isArray(src.actions) || !src.actions.length)
		return null;
	return src;
}

export function materializeToolEvent(name: string, argsJson: string): {question: AskQuestion} | {shortcut: Record<string, unknown>} | null {
	let parsed: unknown;
	try {
		parsed = JSON.parse(argsJson);
	}
	catch {
		return null;
	}
	if(name === "ask_question") {
		const question = parseAskQuestionArgs(parsed);
		return question ? {question} : null;
	}
	if(name === "emit_shortcut") {
		const shortcut = parseEmitShortcutArgs(parsed);
		return shortcut ? {shortcut} : null;
	}
	return null;
}
