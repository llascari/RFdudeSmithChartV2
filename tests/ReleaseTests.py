import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location(
    "publish_release", Path(__file__).resolve().parents[1] / "tools/publish-release.py",
)
publisher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher)


class ReleaseTests(unittest.TestCase):
    def installers(self, root):
        windows = root / "RFdudeSmithChartV2-0.1.0-windows-x64-setup.exe"
        mac_arm64 = root / "RFdudeSmithChartV2-0.1.0-macos-arm64.dmg"
        mac_x64 = root / "RFdudeSmithChartV2-0.1.0-macos-x64.dmg"
        linux = root / "rfdudesmithchartv2_0.1.0-1_amd64.deb"
        windows.write_bytes(b"windows test fixture")
        mac_arm64.write_bytes(b"macOS Apple Silicon test fixture")
        mac_x64.write_bytes(b"macOS Intel test fixture")
        linux.write_bytes(b"linux test fixture")
        return windows, mac_arm64, mac_x64, linux

    def publish(self, root, tag="build-123-1"):
        publisher.publish(
            root, "0.1.0", "a" * 40, tag,
            "https://github.com/example/project/actions/runs/123", "example/project",
        )

    def test_complete_set_is_uploaded_before_publication(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(
            publisher.subprocess, "run",
        ) as run:
            root = Path(directory)
            installers = self.installers(root)
            self.publish(root)
            self.assertEqual(run.call_count, 2)
            create, edit = [call.args[0] for call in run.call_args_list]
            self.assertEqual(create[:4], ["gh", "release", "create", "build-123-1"])
            self.assertIn("--draft", create)
            self.assertIn("--prerelease", create)
            self.assertNotIn("--clobber", create)
            for path in installers:
                self.assertIn(str(path), create)
            self.assertIn(str(root / "SHA256SUMS.txt"), create)
            self.assertIn(str(root / "build-info.json"), create)
            self.assertIn("--draft=false", edit)
            metadata = json.loads((root / "build-info.json").read_text())
            self.assertEqual(metadata["commit"], "a" * 40)
            for path in installers:
                self.assertEqual(metadata["sha256"][path.name], hashlib.sha256(path.read_bytes()).hexdigest())
            self.assertTrue(all(call.kwargs["check"] for call in run.call_args_list))

    def test_upload_failure_never_publishes(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(
            publisher.subprocess, "run", side_effect=subprocess.CalledProcessError(1, ["gh"]),
        ) as run:
            root = Path(directory)
            self.installers(root)
            with self.assertRaises(subprocess.CalledProcessError):
                self.publish(root)
            self.assertEqual(run.call_count, 1)

    def test_missing_extra_empty_or_failed_test_assets_are_rejected(self):
        for variant in ("missing", "extra", "empty", "failed-tests"):
            with self.subTest(variant=variant), tempfile.TemporaryDirectory() as directory, patch.object(
                publisher.subprocess, "run",
            ) as run:
                root = Path(directory)
                windows, mac_arm64, mac_x64, linux = self.installers(root)
                if variant == "missing":
                    mac_arm64.unlink()
                elif variant == "extra":
                    (root / "unexpected.exe").write_bytes(b"unexpected")
                elif variant == "empty":
                    mac_x64.write_bytes(b"")
                else:
                    windows.rename(root / "windows-tests-failed.exe")
                with self.assertRaisesRegex(RuntimeError, "exactly these nonempty installers"):
                    self.publish(root)
                run.assert_not_called()

    def test_tag_matches_source_version(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(
            publisher.subprocess, "run",
        ) as run:
            root = Path(directory)
            self.installers(root)
            with self.assertRaisesRegex(RuntimeError, "tag must identify"):
                self.publish(root, "v9.9.9")
            run.assert_not_called()
            self.publish(root, "v0.1.0")
            self.assertIn("--verify-tag", run.call_args_list[0].args[0])


if __name__ == "__main__":
    unittest.main()
