#!/usr/bin/env python3
"""Generate no-op definitions for BestClient / TClient methods left out of a lower profile.

The BestClient profile does not call these definitions. Lower profiles compile this
file instead of the real component sources so shared call sites still link.
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLIENT = ROOT / "src" / "game" / "client" / "components"

SKIP_DIR_PARTS = {
    "clientindicator",
}


def strip_comments(text: str) -> str:
    out = []
    i = 0
    n = len(text)
    while i < n:
        if text.startswith("//", i):
            i = text.find("\n", i)
            if i < 0:
                break
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
            continue
        if text[i] == '"':
            out.append(text[i])
            i += 1
            while i < n and text[i] != '"':
                out.append(text[i])
                if text[i] == "\\":
                    i += 1
                    if i < n:
                        out.append(text[i])
                i += 1
            if i < n:
                out.append(text[i])
                i += 1
            continue
        out.append(text[i])
        i += 1
    return "".join(out)


def skip_preproc(text: str) -> str:
    lines = []
    skipping = False
    for line in text.splitlines(keepends=True):
        stripped = line.lstrip()
        if skipping:
            if not stripped.rstrip().endswith("\\"):
                skipping = False
            continue
        if stripped.startswith("#"):
            skipping = stripped.rstrip().endswith("\\")
            continue
        lines.append(line)
    return "".join(lines)


IDENT = re.compile(r"[A-Za-z_~][A-Za-z0-9_]*")


class Parser:
    def __init__(self, text: str):
        self.text = text
        self.n = len(text)
        self.i = 0
        self.namespaces: list[str] = []
        self.classes: list[str] = []
        self.functions: list[tuple[str, str, str, str, str]] = []
        # (qual, ret, name, params, suffix)

    def peek(self) -> str:
        return self.text[self.i] if self.i < self.n else ""

    def starts(self, token: str) -> bool:
        if not self.text.startswith(token, self.i):
            return False
        after = self.i + len(token)
        if token[0].isalnum() or token[0] == "_":
            if after < self.n and (self.text[after].isalnum() or self.text[after] == "_"):
                return False
        return True

    def skip_ws(self) -> None:
        while self.i < self.n and self.text[self.i].isspace():
            self.i += 1

    def read_ident(self) -> str:
        match = IDENT.match(self.text, self.i)
        if not match:
            return ""
        self.i = match.end()
        return match.group(0)

    def skip_balanced(self, open_ch: str, close_ch: str) -> None:
        if self.peek() != open_ch:
            return
        self.i += 1
        self.skip_inside(open_ch, close_ch)

    def skip_inside(self, open_ch: str, close_ch: str) -> None:
        """i is just after the opening character. Advance to just after the match."""
        depth = 1
        while self.i < self.n and depth:
            ch = self.text[self.i]
            if ch == '"':
                self.i += 1
                while self.i < self.n and self.text[self.i] != '"':
                    if self.text[self.i] == "\\":
                        self.i += 1
                    self.i += 1
                self.i += 1
                continue
            self.i += 1
            if ch == open_ch:
                depth += 1
            elif ch == close_ch:
                depth -= 1

    def take_inside(self, open_ch: str, close_ch: str) -> str:
        """i is just after the opening character. Return the inside and leave i after the close."""
        start = self.i
        self.skip_inside(open_ch, close_ch)
        return self.text[start : self.i - 1]

    def parse(self) -> None:
        while self.i < self.n:
            self.skip_ws()
            if self.i >= self.n:
                return
            if self.starts("namespace"):
                self.parse_namespace()
                continue
            if self.starts("enum"):
                self.skip_enum()
                continue
            if self.starts("typedef") or self.starts("using") or self.starts("friend") or self.starts("static_assert"):
                self.skip_until_semi()
                continue
            if self.starts("class") or self.starts("struct"):
                self.parse_class()
                continue
            if self.classes or (self.namespaces and self.namespaces[-1]):
                before = self.i
                func = self.try_member()
                if func:
                    self.functions.append(func)
                    continue
                if self.i != before:
                    continue
            self.i += 1

    def parse_namespace(self) -> None:
        self.i += len("namespace")
        self.skip_ws()
        name = self.read_ident()
        self.skip_ws()
        if self.peek() == "{":
            self.i += 1
            self.namespaces.append(name)
            depth_classes = len(self.classes)
            body = self.take_inside("{", "}")
            sub = Parser(body)
            sub.namespaces = self.namespaces[:]
            sub.classes = self.classes[:]
            sub.parse()
            self.functions.extend(sub.functions)
            self.namespaces.pop()
            self.classes = self.classes[:depth_classes]
            return
        self.skip_until_semi()

    def skip_enum(self) -> None:
        while self.i < self.n and self.peek() not in "{;":
            self.i += 1
        if self.peek() == "{":
            self.skip_balanced("{", "}")
        self.skip_until_semi()

    def skip_until_semi(self) -> None:
        while self.i < self.n and self.peek() != ";":
            if self.peek() == "{":
                self.skip_balanced("{", "}")
                continue
            self.i += 1
        if self.peek() == ";":
            self.i += 1

    def parse_class(self) -> None:
        keyword_at = self.i
        self.i += 5 if self.starts("class") else 6
        self.skip_ws()
        name = self.read_ident()
        if not name:
            return
        self.skip_ws()
        if self.peek() == ";":
            self.i += 1
            return
        # Alignas / attributes / base list until { or ;
        while self.i < self.n and self.peek() not in "{;":
            if self.peek() == "(":
                self.skip_balanced("(", ")")
                continue
            self.i += 1
        if self.peek() != "{":
            self.skip_until_semi()
            return
        self.i += 1
        self.classes.append(name)
        body = self.take_inside("{", "}")
        sub = Parser(body)
        sub.namespaces = self.namespaces[:]
        sub.classes = self.classes[:]
        sub.parse()
        self.functions.extend(sub.functions)
        self.classes.pop()
        self.skip_ws()
        if self.peek() == ";":
            self.i += 1
        _ = keyword_at

    def try_member(self) -> tuple[str, str, str, str, str] | None:
        start = self.i
        # Read a statement until ; or { at angle/paren depth 0.
        angle = 0
        paren = 0
        bracket = 0
        param_at = -1
        while self.i < self.n:
            ch = self.text[self.i]
            if ch == "<" and paren == 0 and bracket == 0:
                angle += 1
            elif ch == ">" and paren == 0 and bracket == 0 and angle > 0:
                angle -= 1
            elif ch == "(" and angle == 0 and bracket == 0:
                if paren == 0 and param_at < 0:
                    param_at = self.i
                paren += 1
            elif ch == ")" and angle == 0 and bracket == 0 and paren > 0:
                paren -= 1
            elif ch == "[" and angle == 0 and paren == 0:
                bracket += 1
            elif ch == "]" and bracket > 0:
                bracket -= 1
            elif ch == "{" and angle == 0 and paren == 0 and bracket == 0:
                # Inline body or initializer. Not an out-of-line declaration.
                self.i += 1
                self.skip_inside("{", "}")
                self.skip_ws()
                if self.peek() == ";":
                    self.i += 1
                return None
            elif ch == ";" and angle == 0 and paren == 0 and bracket == 0:
                statement = self.text[start:self.i].strip()
                self.i += 1
                if param_at < 0:
                    return None
                return self.classify(statement, param_at - start)
            elif ch == ":" and angle == 0 and paren == 0 and bracket == 0:
                # public: / private: / bitfield. Abort this attempt.
                word = self.text[start:self.i].strip()
                if word in {"public", "private", "protected"}:
                    self.i += 1
                    return None
            self.i += 1
        return None

    def classify(self, statement: str, paren_index: int) -> tuple[str, str, str, str, str] | None:
        if "=" in statement:
            # = 0, = delete, = default after the parameter list, or a variable.
            # Default arguments such as `= 0.0f` sit inside the parameter list and
            # must not be treated as a pure virtual.
            head = statement[:paren_index]
            rest = statement[paren_index:]
            depth = 0
            end = None
            for idx, ch in enumerate(rest):
                if ch == "(":
                    depth += 1
                elif ch == ")":
                    depth -= 1
                    if depth == 0:
                        end = idx
                        break
            suffix = rest[end + 1 :] if end is not None else ""
            if "=" in head or re.search(r"=\s*(0\b|default\b|delete\b)", suffix):
                return None
        head = statement[:paren_index].strip()
        rest = statement[paren_index:]
        if "template" in statement or head.startswith("typedef") or head.startswith("using") or head.startswith("friend"):
            return None
        # Parameter list and trailing suffix.
        depth = 0
        end = 0
        for idx, ch in enumerate(rest):
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    end = idx
                    break
        if end == 0:
            return None
        params = strip_defaults(rest[1:end])
        suffix = rest[end + 1 :].strip()
        suffix = clean_suffix(suffix)
        name, ret = split_name(head)
        if not name or name in {"if", "for", "while", "switch", "return", "catch"}:
            return None
        if name.startswith(("m_", "s_", "g_")):
            return None
        qual = "::".join([*self.namespaces, *self.classes])
        if not qual:
            return None
        return qual, ret, name, params, suffix


def strip_defaults(params: str) -> str:
    out = []
    angle = paren = bracket = 0
    i = 0
    while i < len(params):
        ch = params[i]
        if ch == "<" and paren == 0:
            angle += 1
        elif ch == ">" and paren == 0 and angle > 0:
            angle -= 1
        elif ch == "(":
            paren += 1
        elif ch == ")":
            paren -= 1
        elif ch == "[":
            bracket += 1
        elif ch == "]":
            bracket -= 1
        elif ch == "=" and angle == paren == bracket == 0:
            i += 1
            while i < len(params):
                c2 = params[i]
                if c2 == "," and angle == paren == bracket == 0:
                    break
                if c2 == "<" and paren == 0:
                    angle += 1
                elif c2 == ">" and paren == 0 and angle > 0:
                    angle -= 1
                elif c2 == "(":
                    paren += 1
                elif c2 == ")":
                    paren -= 1
                i += 1
            continue
        out.append(ch)
        i += 1
    return "".join(out).strip()


def clean_suffix(suffix: str) -> str:
    suffix = re.sub(r"\b(override|final|virtual)\b", "", suffix)
    suffix = re.sub(r"\s+", " ", suffix).strip()
    return suffix


def split_name(head: str) -> tuple[str, str]:
    head = re.sub(r"\[\[.*?\]\]", "", head).strip()
    head = re.sub(r"\b(virtual|static|explicit|inline|constexpr|friend)\b", "", head).strip()
    # operator names
    op = re.search(r"operator\s*(\(\)|\[]|[-+*/%&|^~!=<>]=?|<=>|<<|>>|&&|\|\||->\*?|,)", head)
    if op and op.start() >= 0:
        name = head[op.start() : op.end()].replace(" ", "")
        ret = head[: op.start()].strip()
        return name, ret
    match = None
    for item in IDENT.finditer(head):
        match = item
    if not match:
        return "", ""
    name = match.group(0)
    if name == "~":
        return "", ""
    if name.startswith("~"):
        ret = ""
    else:
        # destructor token may be "~Name" which IDENT allows because of ~
        ret = head[: match.start()].strip()
    if name.startswith("~"):
        ret = ""
    return name, ret


def is_reference(ret: str) -> bool:
    compact = ret.replace(" ", "")
    return compact.endswith("&") and not compact.endswith("&&")


def emit_body(ret: str, name: str, params: str, qual: str = "") -> str:
    if name.startswith("~") or ret == "":
        return ""
    special = special_body(name, params, ret, qual)
    if special is not None:
        return special
    if ret == "void" or ret == "":
        return ""
    compact = ret.replace(" ", "")
    if compact in {"constchar*", "constchar*const"}:
        return '\treturn "";'
    if compact.endswith("*") and not compact.endswith("&"):
        return "\treturn nullptr;"
    if is_reference(ret):
        inner = ret.strip()
        if inner.endswith("&"):
            inner = inner[:-1].strip()
        return f"\tstatic {inner} StubValue{{}};\n\treturn StubValue;"
    return f"\treturn {ret}{{}};".replace("  ", " ")


def special_body(name: str, params: str, ret: str, qual: str = "") -> str | None:
    # Called from the shared prediction path even when the offset is zero.
    # Returning a zero vector would pin tees at the origin.
    if qual == "BcInputs" and name == "BestInterpolate":
        return "\t(void)Enable;\n\treturn mix(PrevPos, CurPos, Fraction);"
    if qual == "BcInputs" and name == "BestInterpolationAmount":
        return "\t(void)DeltaLength;\n\t(void)Enable;\n\treturn Fraction;"
    if name not in {"SanitizeText", "SanitizePlayerName", "SanitizeSensitiveCommand", "MaskServerAddress"}:
        return None
    copy = (
        "\tif(pOutput && OutputSize > 0)\n"
        "\t\tstr_copy(pOutput, pInput ? pInput : \"\", OutputSize);"
    )
    if name == "MaskServerAddress":
        copy = (
            "\tif(pOutput && OutputSize > 0)\n"
            "\t\tstr_copy(pOutput, pAddress ? pAddress : \"\", OutputSize);\n"
            "\treturn pOutput;"
        )
        return copy
    if ret == "bool":
        return copy + "\n\treturn false;"
    return copy


def render(functions: list[tuple[str, str, str, str, str]], includes: list[str], guard: str, anchor: str, preamble: str = "") -> str:
    seen = set()
    lines = [
        "// Generated by scripts/gen_layer_stubs.py. Do not edit by hand.",
        '#include "game/client/gameclient.h"',
    ]
    for include in includes:
        lines.append(f'#include "{include}"')
    lines.extend(["", f"#if !({guard})", ""])
    if preamble:
        lines.append(preamble.rstrip())
        lines.append("")
    for qual, ret, name, params, suffix in functions:
        sig = (qual, name, params, suffix)
        if sig in seen:
            continue
        seen.add(sig)
        # Leading return types are looked up outside the class. Nested enums and
        # structs (ERole, SShortcut, ...) only resolve in a trailing return type.
        if ret and not name.startswith("~"):
            declarator = f"auto {qual}::{name}({params})"
            if suffix:
                declarator += " " + suffix
            declarator += f" -> {ret}"
        else:
            declarator = f"{qual}::{name}({params})"
            if suffix:
                declarator += " " + suffix
        body = emit_body(ret, name, params, qual)
        if body:
            lines.append(f"{declarator}\n{{\n{body}\n}}\n")
        else:
            lines.append(f"{declarator}\n{{\n}}\n")
    lines.append("#else")
    lines.append(f"static const int {anchor} = 1;")
    lines.append("#endif")
    lines.append("")
    return "\n".join(lines)


def collect(folder: Path) -> tuple[list[tuple[str, str, str, str, str]], list[str]]:
    functions = []
    includes = []
    src_root = ROOT / "src"
    for path in sorted(folder.rglob("*.h")):
        if any(part in SKIP_DIR_PARTS for part in path.parts):
            continue
        text = skip_preproc(strip_comments(path.read_text(encoding="utf-8")))
        parser = Parser(text)
        parser.parse()
        if parser.functions:
            includes.append(path.relative_to(src_root).as_posix())
            functions.extend(parser.functions)
    return functions, includes


def collect_cpp_methods(folder: Path, class_name: str) -> list[tuple[str, str, str, str, str]]:
    """Out-of-line methods of a shared class whose only definition lives in this layer."""
    found = []
    prefix = re.compile(rf"(?m)^(?P<head>.+)\b{class_name}::(?P<name>~?[A-Za-z_][A-Za-z0-9_]*)\s*\(")
    for path in sorted(folder.rglob("*.cpp")):
        if any(part in SKIP_DIR_PARTS for part in path.parts):
            continue
        if path.name.endswith("_layer_stub.cpp"):
            continue
        text = path.read_text(encoding="utf-8")
        for match in prefix.finditer(text):
            paren = match.end() - 1
            depth = 0
            j = paren
            end = -1
            while j < len(text):
                ch = text[j]
                if ch == "(":
                    depth += 1
                elif ch == ")":
                    depth -= 1
                    if depth == 0:
                        end = j
                        break
                elif ch == ";" and depth == 0:
                    break
                j += 1
            if end < 0:
                continue
            k = end + 1
            suffix = ""
            while True:
                while k < len(text) and text[k].isspace():
                    k += 1
                word_end = k
                while word_end < len(text) and (text[word_end].isalnum() or text[word_end] == "_"):
                    word_end += 1
                word = text[k:word_end]
                if word in {"const", "override", "final", "noexcept", "volatile"} and word_end > k:
                    if word == "const":
                        suffix = "const"
                    k = word_end
                    continue
                break
            if k >= len(text) or text[k] != "{":
                continue
            head = match.group("head").strip()
            head = re.sub(r"\[\[.*?\]\]", "", head)
            head = re.sub(r"\b(virtual|static|explicit|inline|constexpr)\b", "", head).strip()
            params = strip_defaults(text[paren + 1 : end])
            found.append((class_name, head, match.group("name"), params, suffix))
    return found


def main() -> None:
    best, best_includes = collect(CLIENT / "bestclient")
    best.extend(collect_cpp_methods(CLIENT / "bestclient", "CMenus"))
    if "game/client/components/menus.h" not in best_includes:
        best_includes.append("game/client/components/menus.h")
    (CLIENT / "layer_stubs").mkdir(exist_ok=True)
    (CLIENT / "layer_stubs" / "bestclient_layer_stub.cpp").write_text(
        render(
            best,
            best_includes,
            "UCLIENT_HAS_BESTCLIENT",
            "gs_UClientBestClientLayerStubAnchor",
            # Pimpl is only complete in the real .cpp. The stub destructor still
            # destroys the unique_ptr, so the nested type has to be defined here.
            "\n".join(
                [
                    "class CMusicPlayer::CImpl {};",
                    "void (*BCGradient_ApplyEverythingHook)(CTextCursor *pCursor, const char *pText, int Length) = nullptr;",
                    "std::unique_ptr<CRJelly> rJelly;",
                ]
            ),
        ),
        encoding="utf-8",
    )
    tclient, tclient_includes = collect(CLIENT / "tclient")
    tclient.extend(collect_cpp_methods(CLIENT / "tclient", "CMenus"))
    if "game/client/components/menus.h" not in tclient_includes:
        tclient_includes.append("game/client/components/menus.h")
    (CLIENT / "tclient" / "tclient_layer_stub.cpp").write_text(
        render(tclient, tclient_includes, "UCLIENT_HAS_TCLIENT", "gs_UClientTClientLayerStubAnchor"),
        encoding="utf-8",
    )
    print(f"bestclient stubs: {len(best)}")
    print(f"tclient stubs: {len(tclient)}")


if __name__ == "__main__":
    main()
