import json
import tempfile
import unittest
import zipfile
from pathlib import Path

from scripts.verify_uclient_release import source_version, validate_package

ROOT = Path(__file__).resolve().parents[2]


class ReleaseVerificationTests(unittest.TestCase):
    def manifest(self, component: str, version: str) -> bytes:
        return json.dumps(
            {
                "component": component,
                "clientVersion": version if component == "client" else "2.0.0",
                "launcherVersion": version if component == "launcher" else "1.0.0",
                "gitSha": "abc123",
                "platform": "windows",
                "architecture": "x86_64",
            }
        ).encode()

    def package(self, files: dict[str, bytes]) -> Path:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = Path(directory.name) / "package.zip"
        with zipfile.ZipFile(path, "w") as archive:
            for name, contents in files.items():
                archive.writestr(name, contents)
        return path

    def test_source_versions_are_independent(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "version.h"
            path.write_text(
                '#define UCLIENT_VERSION "2.3.0"\n'
                '#define UCLIENT_LAUNCHER_VERSION "1.4.0"\n',
                encoding="utf-8",
            )
            self.assertEqual(source_version(path, "client"), "2.3.0")
            self.assertEqual(source_version(path, "launcher"), "1.4.0")

    def test_client_package_rejects_launcher(self):
        path = self.package(
            {
                "DDNet.exe": b"client",
                "UClient.exe": b"launcher",
                "uclient_build_manifest.json": self.manifest("client", "2.3.0"),
            }
        )
        with self.assertRaisesRegex(ValueError, "launcher executable"):
            validate_package(path, "client", "2.3.0", "abc123")

    def test_launcher_package_rejects_extra_files(self):
        path = self.package(
            {
                "UClient.exe": b"launcher",
                "data/data.txt": b"unexpected",
                "uclient_build_manifest.json": self.manifest("launcher", "1.4.0"),
            }
        )
        with self.assertRaisesRegex(ValueError, "only UClient.exe"):
            validate_package(path, "launcher", "1.4.0", "abc123")

    def test_package_rejects_wrong_built_version(self):
        path = self.package(
            {
                "DDNet.exe": b"client",
                "uclient_build_manifest.json": self.manifest("client", "2.2.0"),
            }
        )
        with self.assertRaisesRegex(ValueError, "Version mismatch"):
            validate_package(path, "client", "2.3.0", "abc123")

    def test_package_rejects_path_traversal(self):
        path = self.package(
            {
                "../DDNet.exe": b"client",
                "uclient_build_manifest.json": self.manifest("client", "2.3.0"),
            }
        )
        with self.assertRaisesRegex(ValueError, "unsafe archive path"):
            validate_package(path, "client", "2.3.0", "abc123")

    def test_package_rejects_artifact_from_another_commit(self):
        path = self.package(
            {
                "UClient.exe": b"launcher",
                "uclient_build_manifest.json": self.manifest("launcher", "1.4.0"),
            }
        )
        with self.assertRaisesRegex(ValueError, "git SHA mismatch"):
            validate_package(path, "launcher", "1.4.0", "different")

    def test_valid_component_packages(self):
        client = self.package(
            {
                "DDNet.exe": b"client",
                "uclient_build_manifest.json": self.manifest("client", "2.3.0"),
            }
        )
        launcher = self.package(
            {
                "UClient.exe": b"launcher",
                "uclient_build_manifest.json": self.manifest("launcher", "1.4.0"),
            }
        )
        validate_package(client, "client", "2.3.0", "abc123")
        validate_package(launcher, "launcher", "1.4.0", "abc123")

    def test_bridge_and_component_update_contracts_are_wired(self):
        launcher = (ROOT / "src/tools/uclient_launcher.cpp").read_text(encoding="utf-8")
        legacy_workflow = (ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
        self.assertIn('kLegacyPendingVersionFile = L"uclient_pending_version.txt"', launcher)
        self.assertIn('kClientPendingVersionFile = L"uclient_client_pending_version.txt"', launcher)
        self.assertIn("UCLIENT_UPDATE_LATEST_URL, \"client\", true", launcher)
        self.assertIn("CompareVersions(Legacy.Version, UCLIENT_CLIENT_VERSION) <= 0", launcher)
        self.assertIn("\n  release:\n", legacy_workflow)
        self.assertIn("startsWith(github.event.release.tag_name, 'v')", legacy_workflow)


if __name__ == "__main__":
    unittest.main()
