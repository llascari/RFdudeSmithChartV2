#!/usr/bin/env python3
"""Exercise a DEB lifecycle in a disposable CI container with distro KDE/Qt."""

import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time

from check_linux_package import check_package, check_package_structure


def run(*args: str, **kwargs) -> str:
    result = subprocess.run(args, text=True, capture_output=True, **kwargs)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {args}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def system_runtime_snapshot() -> dict[str, str]:
    packages = run("dpkg-query", "-W", "-f=${binary:Package}\n").splitlines()
    protected = [name for name in packages if re.match(r"(libqt[56]|qt[56]-|libkf[56]|kdialog)", name)]
    paths = set(run("dpkg-query", "-L", *protected).splitlines())
    paths.update(("/etc/ld.so.conf", "/usr/bin/qt.conf"))
    paths.update(str(path) for path in Path("/etc/ld.so.conf.d").glob("*"))
    snapshot = {}
    for name in sorted(paths):
        path = Path(name)
        if path.is_symlink():
            snapshot[name] = "link:" + os.readlink(path)
        elif path.is_file():
            snapshot[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    snapshot["Qt loader cache"] = "\n".join(
        line for line in run("ldconfig", "-p").splitlines() if "libQt" in line
    )
    return snapshot


def kde_canary() -> None:
    result = subprocess.run(
        ["kdialog", "--help"], check=True, text=True, capture_output=True,
        env={**os.environ, "LD_DEBUG": "libs", "QT_QPA_PLATFORM": "xcb"},
    )
    loaded = [
        match.group(1) for match in re.finditer(r"calling init:\s+(\S+)", result.stderr)
        if Path(match.group(1)).name.startswith("libQt")
    ]
    if not loaded or any("rfdudesmithchartv2" in path for path in loaded):
        raise RuntimeError(f"KDE did not load exclusively distro Qt:\n{result.stderr}")


def assert_unchanged(before: dict[str, str]) -> None:
    after = system_runtime_snapshot()
    changed = [name for name in before.keys() | after.keys() if before.get(name) != after.get(name)]
    if changed:
        raise RuntimeError(f"System Qt/KDE or loader configuration changed: {changed}")
    kde_canary()


def check_visible_application(example: Path) -> None:
    with tempfile.TemporaryFile(mode="w+") as log:
        process = subprocess.Popen(
            ["/usr/bin/RFdudeSmithChartV2", str(example)], stdout=log, stderr=log,
            env={**os.environ, "QT_QPA_PLATFORM": "xcb"},
        )
        try:
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline and process.poll() is None:
                windows = subprocess.run(
                    ["xdotool", "search", "--onlyvisible", "--pid", str(process.pid)],
                    text=True, capture_output=True,
                )
                if windows.returncode == 0 and windows.stdout.strip():
                    maps = Path(f"/proc/{process.pid}/maps").read_text()
                    qt = [line for line in maps.splitlines() if "/libQt6" in line]
                    if not qt or any("/usr/lib/rfdudesmithchartv2/lib/" not in line for line in qt):
                        raise RuntimeError(f"Installed app loaded non-private Qt:\n{maps}")
                    if "/usr/lib/rfdudesmithchartv2/plugins/platforms/libqxcb.so" not in maps:
                        raise RuntimeError("Installed app did not load its private XCB plugin")
                    return
                time.sleep(0.25)
            log.seek(0)
            raise RuntimeError(f"Installed app did not create a visible window:\n{log.read()}")
        finally:
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=15)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    args = parser.parse_args()
    if os.geteuid() != 0 or not Path("/.dockerenv").is_file() or not os.environ.get("DISPLAY"):
        parser.error("Run only as root inside a disposable Docker container under Xvfb.")
    package = args.package.resolve()
    check_package_structure(package)
    kde_canary()
    before = system_runtime_snapshot()
    versions = run("dpkg-query", "-W", "-f=${binary:Package} ${Version}\n").splitlines()
    run("apt-get", "install", "-y", "--no-remove", "--no-upgrade", str(package))
    assert_unchanged(before)
    current = set(run("dpkg-query", "-W", "-f=${binary:Package} ${Version}\n").splitlines())
    if not set(versions).issubset(current):
        raise RuntimeError("Installation removed or changed an existing distro package")
    if run("dpkg", "--audit").strip():
        raise RuntimeError("dpkg reports an inconsistent installation")
    check_package(package)
    example = Path("/usr/share/RFdudeSmithChartV2/examples/network.rfs_45Mfilter.json")
    check_visible_application(example)
    # Reinstall the same revision to exercise replacement of an existing installation.
    run("apt-get", "install", "-y", "--reinstall", "--no-remove", "--no-upgrade", str(package))
    assert_unchanged(before)
    check_visible_application(example)
    run("apt-get", "purge", "-y", "rfdudesmithchartv2")
    assert_unchanged(before)
    for name in ("/usr/bin/RFdudeSmithChartV2", "/usr/lib/rfdudesmithchartv2",
                 "/usr/share/applications/rfdude-smith-chart-v2.desktop"):
        if Path(name).exists():
            raise RuntimeError(f"Uninstall left application payload behind: {name}")
    if run("dpkg", "--audit").strip():
        raise RuntimeError("dpkg reports an inconsistent uninstall")
    print("PASS: install, visible packaged GUI, reinstall, purge; distro KDE/Qt unchanged")


if __name__ == "__main__":
    main()
