#!/usr/bin/env python3
"""Fail-closed checks shared by UClient component release workflows."""

from __future__ import annotations

import argparse
import json
import re
import sys
import tarfile
import zipfile
from pathlib import Path, PurePosixPath


MACROS = {
    "client": "UCLIENT_VERSION",
    "launcher": "UCLIENT_LAUNCHER_VERSION",
}


def fail(message: str) -> None:
    raise ValueError(message)


def source_version(version_file: Path, component: str) -> str:
    pattern = re.compile(
        rf'^\s*#define\s+{re.escape(MACROS[component])}\s+"([^"]+)"\s*$',
        re.MULTILINE,
    )
    match = pattern.search(version_file.read_text(encoding="utf-8"))
    if not match:
        fail(f"{MACROS[component]} is missing from {version_file}")
    return match.group(1)


def safe_member(name: str) -> bool:
    normalized = name.replace("\\", "/")
    path = PurePosixPath(normalized)
    return bool(normalized) and not path.is_absolute() and ":" not in normalized and ".." not in path.parts


def archive_members(archive: Path) -> tuple[list[str], dict[str, bytes]]:
    names: list[str] = []
    payloads: dict[str, bytes] = {}
    if zipfile.is_zipfile(archive):
        with zipfile.ZipFile(archive) as package:
            for info in package.infolist():
                names.append(info.filename)
                if not info.is_dir():
                    payloads[info.filename] = package.read(info)
        return names, payloads
    if tarfile.is_tarfile(archive):
        with tarfile.open(archive, "r:*") as package:
            for member in package.getmembers():
                names.append(member.name)
                if member.issym() or member.islnk():
                    fail(f"archive link is forbidden: {member.name}")
                if member.isfile():
                    extracted = package.extractfile(member)
                    if extracted is None:
                        fail(f"could not read archive member: {member.name}")
                    payloads[member.name] = extracted.read()
        return names, payloads
    fail(f"unsupported archive: {archive}")


def validate_manifest(data: bytes, component: str, version: str, git_sha: str | None) -> dict:
    try:
        manifest = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        fail(f"invalid uclient_build_manifest.json: {error}")
    key = "clientVersion" if component == "client" else "launcherVersion"
    actual = manifest.get(key)
    if actual != version:
        fail(f"Version mismatch: expected {version}, built {component} {actual!r}")
    if manifest.get("component") not in (None, component):
        fail(f"component mismatch: expected {component}, got {manifest.get('component')!r}")
    if git_sha and manifest.get("gitSha") != git_sha:
        fail(f"git SHA mismatch: expected {git_sha}, got {manifest.get('gitSha')!r}")
    if not manifest.get("platform") or not manifest.get("architecture"):
        fail("build manifest must include platform and architecture")
    return manifest


def validate_package(archive: Path, component: str, version: str, git_sha: str | None) -> None:
    names, payloads = archive_members(archive)
    unsafe = [name for name in names if not safe_member(name)]
    if unsafe:
        fail(f"unsafe archive path: {unsafe[0]}")
    files = [name for name in payloads]
    manifests = [name for name in files if PurePosixPath(name.replace("\\", "/")).name == "uclient_build_manifest.json"]
    if len(manifests) != 1:
        fail(f"expected exactly one build manifest, found {len(manifests)}")
    launcher_files = [
        name for name in files if PurePosixPath(name.replace("\\", "/")).name.lower() == "uclient.exe"
    ]
    if component == "client" and launcher_files:
        fail(f"client package contains launcher executable: {launcher_files[0]}")
    if component == "launcher":
        allowed = {"uclient.exe", "uclient_build_manifest.json"}
        unexpected = [
            name for name in files
            if PurePosixPath(name.replace("\\", "/")).name.lower() not in allowed
        ]
        if len(launcher_files) != 1 or unexpected:
            fail("launcher package must contain only UClient.exe and uclient_build_manifest.json")
    validate_manifest(payloads[manifests[0]], component, version, git_sha)


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    source = subparsers.add_parser("source")
    source.add_argument("--component", choices=MACROS, required=True)
    source.add_argument("--version", required=True)
    source.add_argument("--file", type=Path, default=Path("src/game/version.h"))

    package = subparsers.add_parser("package")
    package.add_argument("--component", choices=MACROS, required=True)
    package.add_argument("--version", required=True)
    package.add_argument("--archive", type=Path, required=True)
    package.add_argument("--git-sha")

    manifest = subparsers.add_parser("manifest")
    manifest.add_argument("--component", choices=MACROS, required=True)
    manifest.add_argument("--version", required=True)
    manifest.add_argument("--path", type=Path, required=True)
    manifest.add_argument("--git-sha")

    args = parser.parse_args()
    try:
        if args.command == "source":
            actual = source_version(args.file, args.component)
            if actual != args.version:
                fail(f"Version mismatch: expected {args.version}, source {actual}")
        elif args.command == "package":
            validate_package(args.archive, args.component, args.version, args.git_sha)
        else:
            validate_manifest(args.path.read_bytes(), args.component, args.version, args.git_sha)
    except (OSError, ValueError) as error:
        print(f"release verification failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
