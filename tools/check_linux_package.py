#!/usr/bin/env python3
"""Check and smoke-test a bundled Debian package without installing it."""

import argparse
from email.parser import Parser
import io
import os
from pathlib import Path
import posixpath
import re
import subprocess
import tarfile
import tempfile


PRIVATE_RUNTIME = "usr/lib/rfdudesmithchartv2"
ALLOWED_FILES = {
    "usr/bin/RFdudeSmithChartV2",
    "usr/share/applications/rfdude-smith-chart-v2.desktop",
    "usr/share/icons/hicolor/scalable/apps/rfdude-smith-chart-v2.svg",
}
ALLOWED_TREES = (
    PRIVATE_RUNTIME,
    "usr/share/doc/RFdudeSmithChartV2",
    "usr/share/RFdudeSmithChartV2/examples",
)


def check_metadata(metadata: str) -> None:
    fields = Parser().parsestr(metadata)
    if fields.get("Package") != "rfdudesmithchartv2" or fields.get("Architecture") != "amd64":
        raise RuntimeError("Unexpected Debian package identity or architecture")
    for name in ("Conflicts", "Breaks", "Replaces", "Provides", "Pre-Depends"):
        if fields.get(name):
            raise RuntimeError(f"Package must not affect other packages through {name}")
    if fields.get("Essential", "no").lower() != "no":
        raise RuntimeError("Application package must not be Essential")


def check_payload_member(member: tarfile.TarInfo) -> None:
    name = member.name.removeprefix("./").rstrip("/")
    if member.isdir() and name in ("", "."):
        return
    if name.startswith("/") or posixpath.normpath(name) != name or name.startswith("../"):
        raise RuntimeError(f"Unsafe archive path: {member.name}")
    allowed = name in ALLOWED_FILES or any(
        name == tree or name.startswith(tree + "/") for tree in ALLOWED_TREES
    )
    ancestor = any(path.startswith(name + "/") for path in (*ALLOWED_FILES, *ALLOWED_TREES))
    if not allowed and not (member.isdir() and ancestor):
        raise RuntimeError(f"Unexpected system-wide package entry: {name}")
    if not (member.isfile() or member.isdir() or member.issym()):
        raise RuntimeError(f"Unsupported archive entry type: {name}")
    if member.uid != 0 or member.gid != 0 or (
        not member.issym() and member.mode & 0o6022
    ):
        raise RuntimeError(f"Unsafe package ownership or permissions: {name}")
    if member.issym():
        target = posixpath.normpath(posixpath.join(posixpath.dirname(name), member.linkname))
        if not name.startswith(PRIVATE_RUNTIME + "/") or not target.startswith(PRIVATE_RUNTIME + "/"):
            raise RuntimeError(f"Symlink escapes the private runtime: {name}")


def check_payload(package: Path) -> None:
    names = set()
    symlinks = set()
    with subprocess.Popen(
        ["dpkg-deb", "--fsys-tarfile", str(package)], stdout=subprocess.PIPE,
    ) as process:
        try:
            with tarfile.open(fileobj=process.stdout, mode="r|") as archive:
                for member in archive:
                    check_payload_member(member)
                    name = member.name.removeprefix("./").rstrip("/")
                    if name in names:
                        raise RuntimeError(f"Duplicate archive path: {name}")
                    names.add(name)
                    if member.issym():
                        symlinks.add(name)
        finally:
            process.stdout.close()
        if process.wait():
            raise RuntimeError(f"Could not inspect package payload: {package}")
    for name in names:
        if any(name.startswith(link + "/") for link in symlinks):
            raise RuntimeError(f"Archive entry traverses a symlink: {name}")


def check_rpaths(binary: Path, private: Path) -> None:
    dynamic = subprocess.run(
        ["readelf", "--dynamic", str(binary)], check=True, text=True, capture_output=True,
    ).stdout
    for value in re.findall(r"\((?:RPATH|RUNPATH)\).*?\[(.*?)\]", dynamic):
        for entry in value.split(":"):
            if not entry.startswith(("$ORIGIN/", "${ORIGIN}/")):
                raise RuntimeError(f"Non-relative runtime search path in {binary}: {entry}")
            target = (binary.parent / entry.replace("${ORIGIN}/", "").replace("$ORIGIN/", "")).resolve()
            if not target.is_relative_to(private):
                raise RuntimeError(f"Runtime search path escapes private runtime in {binary}: {entry}")


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


def check_loaded_qt_libraries(loader_output: str, private: Path) -> None:
    loaded = [
        Path(match.group(1)).resolve()
        for match in re.finditer(r"calling init:\s+(\S+)", loader_output)
        if Path(match.group(1)).name.startswith("libQt6")
    ]
    if not loaded or any(path.parent != private / "lib" for path in loaded):
        raise RuntimeError(f"Loaded Qt libraries are not isolated:\n{loader_output}")


def check_package_structure(package: Path) -> None:
    check_control_files(package)
    check_metadata(subprocess.run(
        ["dpkg-deb", "--field", str(package)], check=True, text=True, capture_output=True,
    ).stdout)
    check_payload(package)


def check_package(package: Path) -> None:
    check_package_structure(package)
    with tempfile.TemporaryDirectory(prefix="rfdude-package-") as directory:
        root = Path(directory)
        subprocess.run(["dpkg-deb", "--extract", str(package), str(root)], check=True)
        private = root / "usr/lib/rfdudesmithchartv2"
        for path in root.rglob("*"):
            if path.is_dir() and not path.is_symlink():
                continue
            relative = path.relative_to(root).as_posix()
            if relative not in ALLOWED_FILES and not any(
                relative.startswith(tree + "/") for tree in ALLOWED_TREES
            ):
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
        check_rpaths(executable, private)
        for tree in (private / "lib", private / "plugins"):
            for binary in tree.rglob("*.so*"):
                if binary.is_symlink() or not binary.is_file():
                    continue
                with binary.open("rb") as stream:
                    is_elf = stream.read(4) == b"\x7fELF"
                if is_elf:
                    check_rpaths(binary, private)
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
        check_loaded_qt_libraries(loader_output, private)
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
