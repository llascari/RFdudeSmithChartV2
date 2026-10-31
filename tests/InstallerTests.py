import ctypes
import importlib.util
from pathlib import Path
import plistlib
import subprocess
import unittest
from unittest.mock import MagicMock, call, patch


spec = importlib.util.spec_from_file_location(
    "test_macos_install", Path(__file__).resolve().parents[1] / "tools/test_macos_install.py",
)
macos = importlib.util.module_from_spec(spec)
spec.loader.exec_module(macos)


class InstallerTests(unittest.TestCase):
    def test_macho_accepts_private_and_system_dependencies(self):
        with patch.object(macos, "run", side_effect=(
            "x86_64 arm64\n",
            "app:\n\t@rpath/QtCore.framework/Versions/A/QtCore (compatibility version 6.0.0)\n"
            "\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0)\n",
            "cmd LC_RPATH\ncmdsize 48\npath @executable_path/../Frameworks (offset 12)\n",
        )) as run:
            macos.check_macho(Path("/test.app/Contents/MacOS/binary"), "arm64")
            self.assertEqual(run.call_args_list[1].args[:4], ("otool", "-arch", "arm64", "-L"))

    def test_macho_rejects_missing_architecture(self):
        with patch.object(macos, "run", return_value="x86_64\n"):
            with self.assertRaisesRegex(RuntimeError, "Missing arm64"):
                macos.check_macho(Path("/test.app/Contents/MacOS/binary"), "arm64")

    def test_macho_rejects_build_host_dependencies(self):
        with patch.object(macos, "run", side_effect=(
            "arm64\n", "app:\n\t/Users/runner/Qt/lib/QtCore (compatibility version 6.0.0)\n",
        )):
            with self.assertRaisesRegex(RuntimeError, "Non-portable"):
                macos.check_macho(Path("/test.app/Contents/MacOS/binary"), "arm64")

    def test_macho_rejects_absolute_rpaths(self):
        for entry in ("/Users/runner/Qt/lib", "/usr/local/lib", "/Users/runner/build path/Qt/lib", "", "@loader_pathexternal/lib"):
            with self.subTest(entry=entry), patch.object(macos, "run", side_effect=(
                "arm64\n", "app:\n",
                f"cmd LC_RPATH\ncmdsize 48\npath {entry} (offset 12)\n",
            )):
                with self.assertRaisesRegex(RuntimeError, "Non-relative"):
                    macos.check_macho(Path("/test.app/Contents/MacOS/binary"), "arm64")

    def test_macho_rejects_escaping_relative_rpaths(self):
        with patch.object(macos, "run", side_effect=(
            "arm64\n", "app:\n",
            "cmd LC_RPATH\ncmdsize 48\npath @executable_path/../../../Qt (offset 12)\n",
        )):
            with self.assertRaisesRegex(RuntimeError, "escapes the bundle"):
                macos.check_macho(Path("/test.app/Contents/MacOS/binary"), "arm64")

    def test_native_window_probe_uses_process_and_size(self):
        for pid, width, expected in ((1234, 800, True), (9999, 800, False), (1234, 50, False)):
            with self.subTest(pid=pid, width=width):
                payload = plistlib.dumps([{
                    "kCGWindowOwnerPID": pid,
                    "kCGWindowBounds": {"Width": width, "Height": 600},
                }])
                buffer = ctypes.create_string_buffer(payload)
                graphics, foundation = MagicMock(), MagicMock()
                graphics.CGWindowListCopyWindowInfo.return_value = 1
                foundation.CFPropertyListCreateData.return_value = 2
                foundation.CFDataGetLength.return_value = len(payload)
                foundation.CFDataGetBytePtr.return_value = ctypes.addressof(buffer)
                with patch.object(macos.ctypes, "CDLL", side_effect=(graphics, foundation)):
                    self.assertEqual(macos.has_visible_window(1234), expected)
                graphics.CGWindowListCopyWindowInfo.assert_called_once_with(1, 0)
                foundation.CFRelease.assert_has_calls([call(2), call(1)])

    def test_native_window_probe_reports_api_failure(self):
        graphics, foundation = MagicMock(), MagicMock()
        graphics.CGWindowListCopyWindowInfo.return_value = None
        with patch.object(macos.ctypes, "CDLL", side_effect=(graphics, foundation)):
            with self.assertRaisesRegex(RuntimeError, "could not enumerate"):
                macos.has_visible_window(1234)

    def test_native_window_probe_releases_list_on_serialization_failure(self):
        graphics, foundation = MagicMock(), MagicMock()
        graphics.CGWindowListCopyWindowInfo.return_value = 1
        foundation.CFPropertyListCreateData.return_value = None
        with patch.object(macos.ctypes, "CDLL", side_effect=(graphics, foundation)):
            with self.assertRaisesRegex(RuntimeError, "could not serialize"):
                macos.has_visible_window(1234)
        foundation.CFRelease.assert_called_once_with(1)

    def test_command_failures_include_output(self):
        with patch.object(
            macos.subprocess, "run",
            return_value=subprocess.CompletedProcess(["otool"], 1, "stdout detail", "stderr detail"),
        ):
            with self.assertRaisesRegex(RuntimeError, "stdout detail\nstderr detail"):
                macos.run("otool")


if __name__ == "__main__":
    unittest.main()
