#!/usr/bin/env python3
import re
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
header = root / "src/tools/uclient_launcher_ui.h"
text = header.read_text(encoding="utf-8")
match = re.search(r"<script>(.*?)</script>", text, re.DOTALL)
if not match:
    print("ERROR: no <script> block found")
    sys.exit(1)

js = match.group(1)
out = root / "build/_launcher_ui_check.js"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(js, encoding="utf-8")
print(f"extracted {len(js)} chars -> {out}")

proc = subprocess.run(["node", "--check", str(out)], capture_output=True, text=True)
if proc.returncode != 0:
    print("JS syntax error:")
    print(proc.stderr or proc.stdout)
    sys.exit(proc.returncode)

print("JS syntax OK")
