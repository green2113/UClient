#!/usr/bin/env python3
"""Derive client-only and launcher-only Windows packages from a full distribution zip."""

from __future__ import annotations

import argparse
import json
import shutil
import sys
import tempfile
import zipfile
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    raise SystemExit(message)


def find_content_root(stage: Path) -> Path:
    top = [p for p in stage.iterdir()]
    if len(top) == 1 and top[0].is_dir():
        return top[0]
    return stage


def locate_file(root: Path, name: str, *, case_insensitive: bool = False) -> Path | None:
    target = name.lower() if case_insensitive else name
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        candidate = path.name.lower() if case_insensitive else path.name
        if candidate == target:
            return path
    return None


def write_launcher_manifest(
    path: Path,
    *,
    client_version: str,
    launcher_version: str,
    git_sha: str,
) -> None:
    payload = {
        "component": "launcher",
        "clientVersion": client_version,
        "launcherVersion": launcher_version,
        "platform": "windows",
        "architecture": "x86_64",
        "gitSha": git_sha,
    }
    path.write_text(json.dumps(payload, separators=(",", ":")) + "\n", encoding="utf-8")


def load_client_manifest(source: Path, client_version: str, git_sha: str) -> dict:
    data = json.loads(source.read_text(encoding="utf-8-sig"))
    data["component"] = "client"
    data["clientVersion"] = client_version
    data["gitSha"] = git_sha
    if "platform" not in data:
        data["platform"] = "windows"
    if "architecture" not in data:
        data["architecture"] = "x86_64"
    return data


def zip_directory(source_root: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_file():
        destination.unlink()
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(source_root.rglob("*")):
            if path.is_dir():
                continue
            rel = path.relative_to(source_root).as_posix()
            archive.write(path, rel)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--full", type=Path, required=True)
    parser.add_argument("--client-manifest", type=Path, required=True)
    parser.add_argument("--client-version", required=True)
    parser.add_argument("--launcher-version", required=True)
    parser.add_argument("--git-sha", required=True)
    parser.add_argument("--client-out", type=Path, required=True)
    parser.add_argument("--launcher-out", type=Path, required=True)
    args = parser.parse_args()

    if not args.full.is_file():
        fail(f"full distribution zip not found: {args.full}")
    if not args.client_manifest.is_file():
        fail(f"client manifest not found: {args.client_manifest}")

    with tempfile.TemporaryDirectory(prefix="uclient-dist-") as tmp:
        stage = Path(tmp)
        extract_dir = stage / "full"
        extract_dir.mkdir()
        with zipfile.ZipFile(args.full) as archive:
            archive.extractall(extract_dir)

        content_root = find_content_root(extract_dir)
        launcher_exe = locate_file(content_root, "UClient.exe", case_insensitive=True)
        if launcher_exe is None:
            fail("full distribution zip does not contain UClient.exe")

        client_root = stage / "client"
        shutil.copytree(content_root, client_root)
        for path in list(client_root.rglob("*")):
            if path.is_file() and path.name.lower() == "uclient.exe":
                path.unlink()

        manifest = load_client_manifest(args.client_manifest, args.client_version, args.git_sha)
        (client_root / "uclient_build_manifest.json").write_text(
            json.dumps(manifest, separators=(",", ":")) + "\n",
            encoding="utf-8",
        )
        zip_directory(client_root, args.client_out)

        launcher_root = stage / "launcher"
        launcher_root.mkdir()
        shutil.copy2(launcher_exe, launcher_root / "UClient.exe")
        write_launcher_manifest(
            launcher_root / "uclient_build_manifest.json",
            client_version=args.client_version,
            launcher_version=args.launcher_version,
            git_sha=args.git_sha,
        )
        zip_directory(launcher_root, args.launcher_out)

    with zipfile.ZipFile(args.client_out) as client_zip:
        names = {PurePosixPath(name).name.lower() for name in client_zip.namelist()}
    if "uclient.exe" in names:
        fail("client package still contains UClient.exe")
    if "uclient_build_manifest.json" not in names:
        fail("client package is missing uclient_build_manifest.json")

    with zipfile.ZipFile(args.launcher_out) as launcher_zip:
        names = sorted(name for name in launcher_zip.namelist() if not name.endswith("/"))
    if names != ["UClient.exe", "uclient_build_manifest.json"]:
        fail(f"unexpected launcher package contents: {names}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
