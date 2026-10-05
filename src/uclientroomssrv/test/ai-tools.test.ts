import {describe, expect, it} from "vitest";
import {extractToolDeltas, materializeToolEvent, parseAskQuestionArgs, parseEmitShortcutArgs} from "../src/ai-tools";

describe("extractToolDeltas", () => {
	it("reads chat completion tool deltas", () => {
		expect(extractToolDeltas({
			choices: [{
				delta: {
					tool_calls: [{
						index: 0,
						id: "call_1",
						function: {name: "ask_question", arguments: "{\"pro"},
					}],
				},
			}],
		})).toEqual([{index: 0, id: "call_1", name: "ask_question", arguments: "{\"pro"}]);
	});
});

describe("parseAskQuestionArgs", () => {
	it("keeps two to five unique labels", () => {
		const q = parseAskQuestionArgs({
			prompt: "Which one?",
			options: [
				{id: "a", label: "Reply in chat"},
				{id: "b", label: "Reply in chat"},
				{label: "Notify me"},
				{label: "Do both"},
			],
		});
		expect(q?.prompt).toBe("Which one?");
		expect(q?.options).toEqual([
			{id: "a", label: "Reply in chat"},
			{id: "opt2", label: "Notify me"},
			{id: "opt3", label: "Do both"},
		]);
	});

	it("rejects a single option", () => {
		expect(parseAskQuestionArgs({prompt: "Only?", options: [{label: "Yes"}]})).toBeNull();
	});
});

describe("parseEmitShortcutArgs", () => {
	it("accepts a flat shortcut object", () => {
		const sc = parseEmitShortcutArgs({
			name: "hello",
			kind: "manual",
			actions: [{type: "chat", text: "hi"}],
		});
		expect(sc?.name).toBe("hello");
	});

	it("accepts a wrapped shortcut object", () => {
		const sc = parseEmitShortcutArgs({
			shortcut: {name: "hello", kind: "manual", actions: [{type: "chat"}]},
		});
		expect(sc?.name).toBe("hello");
	});
});

describe("materializeToolEvent", () => {
	it("waits for complete question JSON", () => {
		expect(materializeToolEvent("ask_question", "{\"prompt\":\"Pick\"")).toBeNull();
		expect(materializeToolEvent("ask_question", JSON.stringify({
			prompt: "Pick",
			options: [{label: "A"}, {label: "B"}],
		}))).toEqual({
			question: {prompt: "Pick", options: [{id: "opt1", label: "A"}, {id: "opt2", label: "B"}]},
		});
	});
});
