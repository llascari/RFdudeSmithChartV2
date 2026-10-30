#!/usr/bin/env python3
"""Check and smoke-test a bundled Debian package without installing it."""

import argparse
import io
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile


def check_control_files(package: Path) -> None:
    control = subprocess.run(
        ["dpkg-deb", "--ctrl-tarfile", str(package)],
        check=True, capture_output=True,
    ).stdout
    with tarfile.open(fileobj=io.BytesIO(control)) as archive:
        for member in archive:
            if member.isdir() and member.name in (".", "./"):
                continue
            name = member.name.removeprefix("./")
            if name not in ("control", "md5sums") or not member.isfile():
                raise RuntimeError(f"Unexpected package control entry: {member.name}")


def check_qt_dependencies(
    binary: Path, private: Path, environment: dict[str, str], *, require_qt: bool = True,
) -> None:
    linked = subprocess.run(
        ["ldd", str(binary)], check=True, text=True, capture_output=True,
        env=environment,
    ).stdout
    if "not found" in linked:
        raise RuntimeError(f"Missing runtime dependencies in {binary}:\n{linked}")
    qt_libraries = [line for line in linked.splitlines() if "libQt6" in line]
    qt_paths = [
        Path(line.split("=>", 1)[1].split(" (", 1)[0].strip()).resolve()
        for line in qt_libraries if "=>" in line
    ]
    if (require_qt and not qt_paths) or len(qt_paths) != len(qt_libraries) or any(
        path.parent != private / "lib" for path in qt_paths
    ):
        raise RuntimeError(f"Qt libraries are not isolated in {binary}:\n{linked}")


def check_package(package: Path) -> None:
    check_control_files(package)
    with tempfile.TemporaryDirectory(prefix="rfdude-package-") as directory:
        root = Path(directory)
        subprocess.run(["dpkg-deb", "--extract", str(package), str(root)], check=True)
        private = root / "usr/lib/rfdudesmithchartv2"
        allowed_files = {
            "usr/bin/RFdudeSmithChartV2",
            "usr/share/applications/rfdude-smith-chart-v2.desktop",
            "usr/share/icons/hicolor/scalable/apps/rfdude-smith-chart-v2.svg",
        }
        allowed_trees = (
            "usr/lib/rfdudesmithchartv2/",
            "usr/share/doc/RFdudeSmithChartV2/",
            "usr/share/RFdudeSmithChartV2/examples/",
        )
        for path in root.rglob("*"):
            if path.is_dir() and not path.is_symlink():
                continue
            relative = path.relative_to(root).as_posix()
            if relative not in allowed_files and not relative.startswith(allowed_trees):
                raise RuntimeError(f"Unexpected system-wide package file: {relative}")
            if path.is_symlink() and not path.resolve().is_relative_to(private):
                raise RuntimeError(f"Symlink escapes the private runtime: {relative}")

        executable = private / "bin/RFdudeSmithChartV2"
        for required in (
            root / "usr/bin/RFdudeSmithChartV2",
            executable,
            private / "bin/qt.conf",
            private / "lib/libQt6Core.so.6",
            private / "lib/libQt6Gui.so.6",
            private / "lib/libQt6Widgets.so.6",
            private / "plugins/platforms/libqxcb.so",
        ):
            if not required.is_file():
                raise RuntimeError(f"Missing package file: {required.relative_to(root)}")

        environment = os.environ.copy()
        for name in (
            "LD_LIBRARY_PATH", "LD_PRELOAD", "QT_PLUGIN_PATH",
            "QT_QPA_PLATFORM_PLUGIN_PATH", "QT_QPA_PLATFORMTHEME", "QT_QPA_PLATFORM",
        ):
            environment.pop(name, None)
        check_qt_dependencies(executable, private, environment)
        for tree in (private / "lib", private / "plugins"):
            for binary in tree.rglob("*.so*"):
                if binary.is_symlink() or not binary.is_file():
                    continue
                with binary.open("rb") as stream:
                    is_elf = stream.read(4) == b"\x7fELF"
                if is_elf:
                    check_qt_dependencies(binary, private, environment, require_qt=False)

        environment["QT_QPA_PLATFORM"] = "xcb"
        environment["LD_DEBUG"] = "libs"
        environment.pop("LD_DEBUG_OUTPUT", None)
        result = subprocess.run(
            ["xvfb-run", "-a", str(root / "usr/bin/RFdudeSmithChartV2"), "--help"],
            env=environment, capture_output=True, text=True, timeout=60,
        )
        if result.returncode or "Cross-platform RF Smith chart" not in result.stdout:
            raise RuntimeError(
                f"Launcher failed (exit {result.returncode}):\n{result.stdout}\n{result.stderr}"
            )
        platform_plugin = str(private / "plugins/platforms/libqxcb.so")
        loader_output = result.stdout + result.stderr
        if not any(
            platform_plugin in line and "calling init:" in line
            for line in loader_output.splitlines()
        ):
            raise RuntimeError(f"Private Qt platform plugin was not loaded:\n{loader_output}")
        print(f"PASS: isolated runtime and extracted-package launch: {package.name}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("packages", nargs="+", type=Path)
    arguments = parser.parse_args()
    for package in arguments.packages:
        check_package(package.resolve())
