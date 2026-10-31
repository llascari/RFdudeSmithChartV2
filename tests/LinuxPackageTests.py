import importlib.util
import io
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location(
    "check_linux_package", Path(__file__).resolve().parents[1] / "tools/check_linux_package.py",
)
package_check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package_check)


class LinuxPackageTests(unittest.TestCase):
    def test_metadata_cannot_replace_desktop_packages(self):
        metadata = "Package: rfdudesmithchartv2\nArchitecture: amd64\n"
        package_check.check_metadata(metadata)
        for extra in (
            "Replaces: libqt6core6\n", "Breaks: plasma-workspace\n",
            "Conflicts: kdialog\n", "Provides: libqt6gui6\n", "Essential: yes\n",
            "Pre-Depends: libc6\n",
        ):
            with self.subTest(extra=extra), self.assertRaises(RuntimeError):
                package_check.check_metadata(metadata + extra)
        for invalid in ("Package: plasma-workspace\nArchitecture: amd64\n", metadata.replace("amd64", "arm64")):
            with self.assertRaises(RuntimeError):
                package_check.check_metadata(invalid)

    def test_payload_paths_types_and_permissions(self):
        for name, kind, mode, link, passes in (
            ("./usr/", tarfile.DIRTYPE, 0o755, "", True),
            ("./usr/bin/RFdudeSmithChartV2", tarfile.REGTYPE, 0o755, "", True),
            ("./usr/lib/rfdudesmithchartv2/lib/libQt6Core.so.6", tarfile.SYMTYPE, 0o777, "libQt6Core.so.6.8.3", True),
            ("./usr/lib/libQt6Core.so.6", tarfile.REGTYPE, 0o644, "", False),
            ("./usr/bin/qt.conf", tarfile.REGTYPE, 0o644, "", False),
            ("./etc/ld.so.conf.d/", tarfile.DIRTYPE, 0o755, "", False),
            ("./usr/../etc/test", tarfile.REGTYPE, 0o644, "", False),
            ("/usr/bin/RFdudeSmithChartV2", tarfile.REGTYPE, 0o755, "", False),
            ("./usr/bin/RFdudeSmithChartV2", tarfile.REGTYPE, 0o4755, "", False),
            ("./usr/bin/RFdudeSmithChartV2", tarfile.REGTYPE, 0o777, "", False),
            ("./usr/lib/rfdudesmithchartv2/lib/link", tarfile.SYMTYPE, 0o777, "../../../libQt6Core.so.6", False),
            ("./usr/lib/rfdudesmithchartv2/lib/link", tarfile.SYMTYPE, 0o777, "/usr/lib/libQt6Core.so.6", False),
            ("./usr/lib/rfdudesmithchartv2/lib/device", tarfile.CHRTYPE, 0o644, "", False),
            ("./usr/lib/rfdudesmithchartv2/lib/hardlink", tarfile.LNKTYPE, 0o644, "etc/passwd", False),
        ):
            with self.subTest(name=name, kind=kind, mode=mode):
                member = tarfile.TarInfo(name)
                member.type, member.mode, member.linkname = kind, mode, link
                if passes:
                    package_check.check_payload_member(member)
                else:
                    with self.assertRaises(RuntimeError):
                        package_check.check_payload_member(member)
        member = tarfile.TarInfo("./usr/bin/RFdudeSmithChartV2")
        member.uid = 1000
        with self.assertRaises(RuntimeError):
            package_check.check_payload_member(member)

    def test_rpaths_are_private_and_relocatable(self):
        private = Path("/extracted/usr/lib/rfdudesmithchartv2")
        for entry, passes in (
            ("$ORIGIN/../lib", True), ("${ORIGIN}/../lib", True),
            ("/opt/Qt/lib", False), ("$ORIGIN/../../../lib", False),
            ("$ORIGIN/../lib:", False),
        ):
            with self.subTest(entry=entry), patch.object(
                package_check.subprocess, "run",
                return_value=subprocess.CompletedProcess([], 0, f"(RUNPATH) Library runpath: [{entry}]\n"),
            ):
                if passes:
                    package_check.check_rpaths(private / "bin/app", private)
                else:
                    with self.assertRaises(RuntimeError):
                        package_check.check_rpaths(private / "bin/app", private)

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

    def test_loaded_qt_library_isolation(self):
        private = Path("/extracted/usr/lib/rfdudesmithchartv2")
        for output, passes in (
            (f"123: calling init: {private}/lib/libQt6Core.so.6\n", True),
            (
                f"123: calling init: {private}/lib/libQt6Core.so.6\n"
                "123: calling init: /usr/lib/libQt6DBus.so.6\n",
                False,
            ),
            ("123: calling init: /usr/lib/libc.so.6\n", False),
        ):
            with self.subTest(output=output):
                if passes:
                    package_check.check_loaded_qt_libraries(output, private)
                else:
                    with self.assertRaisesRegex(RuntimeError, "Loaded Qt libraries are not isolated"):
                        package_check.check_loaded_qt_libraries(output, private)


class CPackGateTests(unittest.TestCase):
    def run_gate(self, directory, *, bundled="ON", packages=(), validator_exit=0, source=False):
        root = Path(directory)
        tools = root / "tools"
        tools.mkdir()
        (tools / "check_linux_package.py").write_text(
            "import pathlib, sys\n"
            "pathlib.Path(sys.argv[1] + '.checked').touch()\n"
            f"sys.exit({validator_exit})\n"
        )
        template = (
            Path(__file__).resolve().parents[1] / "packaging/linux/validate_package.cmake.in"
        ).read_text()
        script = root / "gate.cmake"
        script.write_text(
            template.replace("@RFDUDE_BUNDLE_QT@", bundled)
            .replace("@CMAKE_CURRENT_SOURCE_DIR@", root.as_posix())
        )
        return subprocess.run(
            [
                "cmake",
                *([
                    f"-DCPACK_SOURCE_INSTALLED_DIRECTORIES={root};/",
                    f"-DCPACK_INSTALLED_DIRECTORIES={root};/",
                ] if source else []),
                f"-DPython3_EXECUTABLE={sys.executable}",
                *(f"-DRFDUDE_{tool}={sys.executable}" for tool in (
                    "DPKG_DEB", "LDD", "XVFB_RUN", "XAUTH", "READELF",
                )),
                "-DCPACK_PACKAGE_FILES=" + ";".join(str(root / name) for name in packages),
                "-P", str(script),
            ],
            capture_output=True, text=True,
        )

    def test_validates_each_generated_package(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_gate(directory, packages=("one.deb", "two.deb"))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for name in ("one.deb.checked", "two.deb.checked"):
                self.assertTrue((Path(directory) / name).is_file())

    def test_rejects_failed_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_gate(directory, packages=("bad.deb",), validator_exit=1)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Linux installer validation failed", result.stderr)

    def test_source_archives_remain_supported(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_gate(directory, bundled="OFF", source=True, packages=("source.tar.gz",))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Source archive", result.stdout)
            self.assertFalse((Path(directory) / "source.tar.gz.checked").exists())

    def test_rejects_unvalidated_packaging_modes(self):
        for options, message in (
            ({"bundled": "OFF", "packages": ("app.deb",)}, "require RFDUDE_BUNDLE_QT=ON"),
            ({}, "No generated Linux packages"),
            ({"packages": ("app.tar.gz",)}, "supports Debian installers only"),
        ):
            with self.subTest(options=options), tempfile.TemporaryDirectory() as directory:
                result = self.run_gate(directory, **options)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)

    def test_cpack_does_not_publish_failed_package(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.run_gate(directory, packages=("bad.deb",), validator_exit=1)
            payload = root / "payload"
            payload.mkdir()
            (payload / "test.txt").write_text("CPack gate regression fixture\n")
            config = root / "CPackConfig.cmake"
            config.write_text(
                'set(CPACK_GENERATOR "DEB")\n'
                'set(CPACK_PACKAGE_NAME "safety-test")\n'
                'set(CPACK_PACKAGE_VERSION "1.0.0")\n'
                'set(CPACK_PACKAGE_FILE_NAME "safety-test-1.0.0")\n'
                'set(CPACK_PACKAGE_DESCRIPTION "Packaging safety regression fixture")\n'
                'set(CPACK_PACKAGE_CONTACT "packaging-test")\n'
                f'set(CPACK_PACKAGE_DIRECTORY "{root.as_posix()}/output")\n'
                'set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")\n'
                f'set(CPACK_INSTALLED_DIRECTORIES "{payload.as_posix()};/")\n'
                f'set(CPACK_POST_BUILD_SCRIPTS "{root.as_posix()}/gate.cmake")\n'
            )
            result = subprocess.run(
                [
                    "cpack", "--config", str(config),
                    f"-DPython3_EXECUTABLE={sys.executable}",
                    *(f"-DRFDUDE_{tool}={sys.executable}" for tool in (
                        "DPKG_DEB", "LDD", "XVFB_RUN", "XAUTH",
                    )),
                ],
                capture_output=True, text=True,
            )
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Linux installer validation failed", result.stderr)
            self.assertFalse(list((root / "output").glob("*.deb")))


if __name__ == "__main__":
    unittest.main()
