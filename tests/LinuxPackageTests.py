import importlib.util
import io
from pathlib import Path
import subprocess
import tarfile
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location(
    "check_linux_package", Path(__file__).resolve().parents[1] / "tools/check_linux_package.py",
)
package_check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package_check)


class LinuxPackageTests(unittest.TestCase):
    def control_archive(self, name, kind=tarfile.REGTYPE):
        data = io.BytesIO()
        with tarfile.open(fileobj=data, mode="w") as archive:
            member = tarfile.TarInfo(name)
            member.type = kind
            archive.addfile(member)
        return data.getvalue()

    def test_control_files(self):
        for name in ("./control", "./md5sums"):
            with self.subTest(name=name), patch.object(
                package_check.subprocess, "run",
                return_value=subprocess.CompletedProcess([], 0, self.control_archive(name)),
            ):
                package_check.check_control_files(Path("candidate.deb"))

    def test_rejects_control_scripts_and_links(self):
        for name, kind in (
            ("./preinst", tarfile.REGTYPE),
            ("./postinst", tarfile.REGTYPE),
            ("./prerm", tarfile.REGTYPE),
            ("./postrm", tarfile.REGTYPE),
            ("./triggers", tarfile.REGTYPE),
            ("../control", tarfile.REGTYPE),
            ("./control", tarfile.SYMTYPE),
        ):
            with self.subTest(name=name, kind=kind), patch.object(
                package_check.subprocess, "run",
                return_value=subprocess.CompletedProcess([], 0, self.control_archive(name, kind)),
            ):
                with self.assertRaisesRegex(RuntimeError, "Unexpected package control entry"):
                    package_check.check_control_files(Path("candidate.deb"))

    def test_dependency_isolation(self):
        private = Path("/extracted/usr/lib/rfdudesmithchartv2")
        for output, passes in (
            (f"libQt6Gui.so.6 => {private}/lib/libQt6Gui.so.6 (0x123)\n", True),
            ("libQt6DBus.so.6 => /usr/lib/libQt6DBus.so.6 (0x123)\n", False),
            ("libQt6Gui.so.6 => not found\n", False),
            ("libc.so.6 => /usr/lib/libc.so.6 (0x123)\n", False),
        ):
            with self.subTest(output=output), patch.object(
                package_check.subprocess, "run",
                return_value=subprocess.CompletedProcess([], 0, output),
            ):
                if passes:
                    package_check.check_qt_dependencies(private / "bin/app", private, {})
                else:
                    with self.assertRaises(RuntimeError):
                        package_check.check_qt_dependencies(private / "bin/app", private, {})

    def test_leaf_library_without_qt_dependencies(self):
        with patch.object(
            package_check.subprocess, "run",
            return_value=subprocess.CompletedProcess([], 0, "libc.so.6 => /usr/lib/libc.so.6 (0x123)\n"),
        ):
            private = Path("/extracted/usr/lib/rfdudesmithchartv2")
            package_check.check_qt_dependencies(
                private / "lib/libQt6Core.so.6", private, {}, require_qt=False,
            )


if __name__ == "__main__":
    unittest.main()
