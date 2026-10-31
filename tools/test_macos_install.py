#!/usr/bin/env python3
"""Mount the final DMG and check a relocated app on a runner without a Qt SDK."""

import argparse
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tempfile
import time


def run(*args: str, **kwargs) -> str:
    result = subprocess.run(args, text=True, capture_output=True, **kwargs)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {args}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def check_macho(binary: Path, architecture: str) -> None:
    if architecture not in run("lipo", "-archs", str(binary)).split():
        raise RuntimeError(f"Missing {architecture} slice in {binary}")
    for line in run("otool", "-arch", architecture, "-L", str(binary)).splitlines()[1:]:
        dependency = line.strip().split(" (", 1)[0]
        if not dependency.startswith(("@rpath/", "@loader_path/", "@executable_path/", "/System/Library/", "/usr/lib/")):
            raise RuntimeError(f"Non-portable Mach-O dependency in {binary}: {dependency}")
    commands = run("otool", "-arch", architecture, "-l", str(binary))
    bundle = next(parent for parent in binary.parents if parent.suffix == ".app")
    for entry in re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset", commands):
        prefix, _, suffix = entry.partition("/")
        if prefix not in ("@loader_path", "@executable_path"):
            raise RuntimeError(f"Non-relative Mach-O RPATH in {binary}: {entry}")
        base = binary.parent if prefix == "@loader_path" else bundle / "Contents/MacOS"
        if not (base / suffix).resolve().is_relative_to(bundle.resolve()):
            raise RuntimeError(f"Mach-O RPATH escapes the bundle in {binary}: {entry}")


def has_visible_window(pid: int) -> bool:
    script = (
        "ObjC.import('CoreGraphics'); "
        "var windows = ObjC.deepUnwrap($.CGWindowListCopyWindowInfo(1, 0)); "
        f"windows.some(function(w) {{ return w.kCGWindowOwnerPID === {pid} && "
        "w.kCGWindowBounds.Width > 100 && w.kCGWindowBounds.Height > 100; });"
    )
    return run("osascript", "-l", "JavaScript", "-e", script).strip() == "true"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dmg", type=Path)
    parser.add_argument("architecture", choices=("arm64", "x86_64"))
    args = parser.parse_args()
    logs = Path("installer-logs")
    logs.mkdir(exist_ok=True)
    environment = {
        name: value for name, value in os.environ.items()
        if not name.startswith(("QT_", "DYLD_")) and name not in ("QTDIR", "LD_LIBRARY_PATH")
    }
    environment["DYLD_PRINT_LIBRARIES"] = "1"
    environment["QT_DEBUG_PLUGINS"] = "1"
    environment["QT_QPA_PLATFORM"] = "cocoa"
    with tempfile.TemporaryDirectory(prefix="RFdude relocated app ") as directory:
        root = Path(directory).resolve()
        mounted = subprocess.run(
            ["hdiutil", "attach", "-readonly", "-nobrowse", "-plist", str(args.dmg.resolve())],
            capture_output=True,
        )
        if mounted.returncode:
            raise RuntimeError(f"Could not mount final DMG:\n{mounted.stderr.decode(errors='replace')}")
        entities = plistlib.loads(mounted.stdout)["system-entities"]
        mounts = [Path(entity["mount-point"]) for entity in entities if "mount-point" in entity]
        device = next(entity["dev-entry"] for entity in entities if "dev-entry" in entity)
        try:
            apps = [app for mount in mounts for app in mount.glob("*.app")]
            if len(apps) != 1:
                raise RuntimeError(f"Expected one app in DMG, found {apps}")
            app = root / apps[0].name
            run("ditto", str(apps[0]), str(app))
        finally:
            run("hdiutil", "detach", device)
        for path in app.rglob("*"):
            if path.is_symlink() and not path.resolve().is_relative_to(app.resolve()):
                raise RuntimeError(f"Bundle symlink escapes app: {path}")
            if path.is_file() and not path.is_symlink():
                with path.open("rb") as stream:
                    magic = stream.read(4)
                if magic in (b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xca\xfe\xba\xbe", b"\xca\xfe\xba\xbf"):
                    check_macho(path, args.architecture)
        executable = app / "Contents/MacOS/RFdudeSmithChartV2"
        if run("lipo", "-archs", str(executable)).strip() != args.architecture:
            raise RuntimeError("Packaged executable architecture differs from requested architecture")
        result = subprocess.run(
            [str(executable), "--help"], env=environment, text=True,
            capture_output=True, timeout=60,
        )
        (logs / "macos-loader.txt").write_text(result.stdout + result.stderr)
        if result.returncode or "Cross-platform RF Smith chart" not in result.stdout:
            raise RuntimeError(f"Relocated packaged app failed:\n{result.stdout}\n{result.stderr}")
        loaded = [line for line in result.stderr.splitlines() if "dyld[" in line]
        qt = [line for line in loaded if re.search(r"/Qt[^/]*\.framework/", line)]
        if not qt or any(str(app) + "/" not in line for line in qt):
            raise RuntimeError("Packaged app did not load exclusively bundle-private Qt")
        if not any(str(app) + "/" in line and "libqcocoa.dylib" in line for line in loaded):
            raise RuntimeError("Packaged app did not load its Cocoa platform plugin")
        example = Path("examples/network.rfs_45Mfilter.json").resolve()
        with (logs / "macos-gui.txt").open("w") as log:
            process = subprocess.Popen([str(executable), str(example)], env=environment, stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 30
                while process.poll() is None and time.monotonic() < deadline:
                    if has_visible_window(process.pid):
                        break
                    time.sleep(0.25)
                else:
                    raise RuntimeError(f"Packaged GUI did not create a visible native window (exit {process.poll()})")
                run("screencapture", "-x", str(logs / "macos-desktop.png"))
            finally:
                if process.poll() is None:
                    process.terminate()
                process.wait(timeout=15)
        shutil.rmtree(app)
        if app.exists():
            raise RuntimeError("Could not remove copied application")
    print("PASS: final DMG, relocation, Mach-O dependencies, private Qt/Cocoa, GUI startup, removal")


if __name__ == "__main__":
    main()
