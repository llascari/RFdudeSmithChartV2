#!/usr/bin/env python3
"""Check and smoke-test a bundled Debian package without installing it."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def check_package(package: Path) -> None:
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
        linked = subprocess.run(
            ["ldd", str(executable)], check=True, text=True, capture_output=True,
            env=environment,
        ).stdout
        if "not found" in linked:
            raise RuntimeError(f"Missing runtime dependencies:\n{linked}")
        qt_libraries = [line for line in linked.splitlines() if "libQt6" in line]
        qt_paths = [
            Path(line.split("=>", 1)[1].split(" (", 1)[0].strip()).resolve()
            for line in qt_libraries if "=>" in line
        ]
        if not qt_paths or len(qt_paths) != len(qt_libraries) or any(
            path.parent != private / "lib" for path in qt_paths
        ):
            raise RuntimeError(f"Qt libraries are not isolated:\n{linked}")

        environment["QT_QPA_PLATFORM"] = "xcb"
        environment["QT_DEBUG_PLUGINS"] = "1"
        environment["QT_LOGGING_RULES"] = "qt.core.plugin.loader.debug=true"
        environment["QT_MESSAGE_PATTERN"] = "%{message}"
        result = subprocess.run(
            ["xvfb-run", "-a", str(root / "usr/bin/RFdudeSmithChartV2"), "--help"],
            env=environment, capture_output=True, text=True, timeout=60,
        )
        if result.returncode or "Cross-platform RF Smith chart" not in result.stdout:
            raise RuntimeError(
                f"Launcher failed (exit {result.returncode}):\n{result.stdout}\n{result.stderr}"
            )
        platform_plugin = str(private / "plugins/platforms/libqxcb.so")
        if not any(
            platform_plugin in line and "loaded library" in line
            for line in result.stderr.splitlines()
        ):
            raise RuntimeError(f"Private Qt platform plugin was not loaded:\n{result.stderr}")
        print(f"PASS: isolated runtime and extracted-package launch: {package.name}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("packages", nargs="+", type=Path)
    arguments = parser.parse_args()
    for package in arguments.packages:
        check_package(package.resolve())
