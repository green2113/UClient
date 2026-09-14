#!/usr/bin/env python3
"""Merge distribution latest.json and versions.json for partial releases."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


def load_json(path: Path | None, default: Any) -> Any:
    if path is None or not path.is_file():
        return default
    text = path.read_text(encoding="utf-8-sig")
    return json.loads(text)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--existing-latest", type=Path)
    parser.add_argument("--existing-versions", type=Path)
    parser.add_argument("--client-version", required=True)
    parser.add_argument("--launcher-version", required=True)
    parser.add_argument("--released-at", required=True)
    parser.add_argument("--publish-full", action="store_true")
    parser.add_argument("--full-latest", type=Path, help="New full distribution latest.json payload")
    parser.add_argument("--out-latest", type=Path, required=True)
    parser.add_argument("--out-versions", type=Path, required=True)
    args = parser.parse_args()

    existing = load_json(args.existing_latest, {})
    if not isinstance(existing, dict):
        existing = {}

    merged: dict[str, Any] = dict(existing)
    merged["version"] = args.client_version
    merged["launcherVersion"] = args.launcher_version
    merged["releasedAt"] = args.released_at

    if args.publish_full:
        if args.full_latest is None or not args.full_latest.is_file():
            raise SystemExit("publish_full requires --full-latest")
        full_payload = json.loads(args.full_latest.read_text(encoding="utf-8-sig"))
        if isinstance(full_payload, dict):
            merged["version"] = full_payload.get("version", args.client_version)
            merged["launcherVersion"] = full_payload.get("launcherVersion", args.launcher_version)
            merged["releasedAt"] = full_payload.get("releasedAt", args.released_at)
            if "platforms" in full_payload:
                merged["platforms"] = full_payload["platforms"]
    elif "platforms" not in merged:
        raise SystemExit(
            "No existing platforms in uclient/latest.json; run a full publish first or enable publish_full",
        )

    args.out_latest.parent.mkdir(parents=True, exist_ok=True)
    args.out_latest.write_text(json.dumps(merged, separators=(",", ":")) + "\n", encoding="utf-8")

    versions = load_json(args.existing_versions, [])
    if not isinstance(versions, list):
        versions = []

    entry = {
        "version": args.client_version,
        "launcherVersion": args.launcher_version,
        "releasedAt": args.released_at,
    }
    filtered = [
        item
        for item in versions
        if not (
            isinstance(item, dict)
            and item.get("version") == args.client_version
            and item.get("launcherVersion") == args.launcher_version
        )
    ]
    filtered.insert(0, entry)
    args.out_versions.write_text(json.dumps(filtered, separators=(",", ":")) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
