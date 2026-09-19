#!/usr/bin/env python3
"""Build the cached UClient assistant catalog from config headers and launcher blocks."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADERS = [
    ROOT / "src/engine/shared/config_variables.h",
    ROOT / "src/engine/shared/config_variables_tclient.h",
    ROOT / "src/engine/shared/config_variables_uclient.h",
    ROOT / "src/engine/shared/config_variables_bestclient.h",
]
LAUNCHER_UI = ROOT / "src/tools/uclient_launcher_ui.h"
KNOWLEDGE = ROOT / "src/uclientroomssrv/src/ai-knowledge.md"
OUT = ROOT / "src/uclientroomssrv/src/ai-catalog.generated.ts"

MACRO_RE = re.compile(
    r"MACRO_CONFIG_(INT|COL|STR)\(\s*([A-Za-z0-9_]+)\s*,\s*([a-z0-9_]+)\s*,",
    re.MULTILINE,
)
DESC_RE = re.compile(r'"((?:\\.|[^"\\])*)"\s*\)\s*$')
SECRET_RE = re.compile(r"(password|token|secret|_key|apikey|uuid)", re.I)
BLOCK_RE = re.compile(
    r"\{\s*id:\s*\"([^\"]+)\"[\s\S]*?title:\s*\"((?:\\.|[^\"\\])*)\"[\s\S]*?hint:\s*\"((?:\\.|[^\"\\])*)\"[\s\S]*?defaults:\s*(\{[\s\S]*?\})\s*\}",
)
TRIGGER_RE = re.compile(
    r"\{\s*id:\s*\"([^\"]+)\"[\s\S]*?title:\s*\"((?:\\.|[^\"\\])*)\"[\s\S]*?whenHint:\s*\"((?:\\.|[^\"\\])*)\"[\s\S]*?defaults:\s*(\{[\s\S]*?\})\s*\}",
)


def unescape(text: str) -> str:
    return text.replace('\\"', '"').replace("\\\\", "\\")


def is_client(flags: str) -> bool:
    return "CFGFLAG_CLIENT" in flags or "CFGFLAG_DEBUG_CLIENT" in flags


def is_server_only(flags: str) -> bool:
    return "CFGFLAG_SERVER" in flags and "CFGFLAG_CLIENT" not in flags and "CFGFLAG_DEBUG_CLIENT" not in flags


def parse_header(path: Path) -> list[dict[str, str]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    rows: list[dict[str, str]] = []
    for match in MACRO_RE.finditer(text):
        start = match.start()
        depth = 0
        end = None
        for i in range(match.start(), len(text)):
            if text[i] == "(":
                depth += 1
            elif text[i] == ")":
                depth -= 1
                if depth == 0:
                    end = i
                    break
        if end is None:
            continue
        blob = text[start : end + 1]
        desc_match = DESC_RE.search(blob.replace("\n", " "))
        desc = unescape(desc_match.group(1)) if desc_match else ""
        name = match.group(3)
        flags_part = blob
        if is_server_only(flags_part):
            continue
        if not is_client(flags_part):
            continue
        rows.append(
            {
                "name": name,
                "desc": desc,
                "secret": "1" if SECRET_RE.search(name) else "0",
            }
        )
    return rows


def extract_array(js: str, name: str) -> str:
    needle = f"var {name} = ["
    start = js.find(needle)
    if start < 0:
        return ""
    i = js.find("[", start)
    depth = 0
    in_str = False
    escape = False
    for j in range(i, len(js)):
        ch = js[j]
        if in_str:
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == '"':
                in_str = False
            continue
        if ch == '"':
            in_str = True
            continue
        if ch == "[":
            depth += 1
        elif ch == "]":
            depth -= 1
            if depth == 0:
                return js[i : j + 1]
    return ""


def parse_blocks(blob: str, trigger: bool) -> list[dict[str, str]]:
    regex = TRIGGER_RE if trigger else BLOCK_RE
    out: list[dict[str, str]] = []
    for match in regex.finditer(blob):
        defaults = re.sub(r"\s+", " ", match.group(4)).strip()
        type_match = re.search(r"type:\s*\"([^\"]+)\"", defaults)
        row = {
            "id": match.group(1),
            "title": unescape(match.group(2)),
            "hint": unescape(match.group(3)),
            "type": type_match.group(1) if type_match else match.group(1),
            "defaults": defaults,
        }
        out.append(row)
    return out


def main() -> int:
    settings: list[dict[str, str]] = []
    seen: set[str] = set()
    for header in HEADERS:
        if not header.exists():
            print(f"missing {header}", file=sys.stderr)
            return 1
        for row in parse_header(header):
            if row["name"] in seen:
                continue
            seen.add(row["name"])
            settings.append(row)

    ui = LAUNCHER_UI.read_text(encoding="utf-8", errors="replace")
    script = re.search(r"<script>(.*?)</script>", ui, re.DOTALL)
    if not script:
        print("no launcher script", file=sys.stderr)
        return 1
    js = script.group(1)
    triggers = parse_blocks(extract_array(js, "SC_TRIGGERS"), True)
    actions = parse_blocks(extract_array(js, "SC_ACTIONS"), False)

    payload = {
        "settings": settings,
        "triggers": triggers,
        "actions": actions,
    }
    knowledge = KNOWLEDGE.read_text(encoding="utf-8") if KNOWLEDGE.exists() else ""
    body = (
        "// Generated by scripts/generate_ai_knowledge.py. Do not edit.\n"
        "export const AI_KNOWLEDGE = "
        + json.dumps(knowledge, ensure_ascii=True)
        + ";\nexport const AI_CATALOG = "
        + json.dumps(payload, ensure_ascii=True, indent=2)
        + " as const;\n"
    )
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(body, encoding="utf-8")
    print(
        f"wrote {OUT} settings={len(settings)} triggers={len(triggers)} actions={len(actions)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
