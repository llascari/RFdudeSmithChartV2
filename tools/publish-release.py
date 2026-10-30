#!/usr/bin/env python3
"""Publish a complete, verified CI installer set as an immutable pre-release."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def publish(assets: Path, version: str, commit: str, tag: str, run_url: str, repository: str) -> None:
    expected = {
        f"RFdudeSmithChartV2-{version}-windows-x64-setup.exe",
        f"RFdudeSmithChartV2-{version}-macos-arm64.dmg",
        f"RFdudeSmithChartV2-{version}-macos-x64.dmg",
        f"rfdudesmithchartv2_{version}-1_amd64.deb",
    }
    files = list(assets.iterdir())
    if {path.name for path in files} != expected or any(
        not path.is_file() or path.is_symlink() or path.stat().st_size == 0 for path in files
    ):
        raise RuntimeError(f"Release requires exactly these nonempty installers: {sorted(expected)}")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise RuntimeError("Release requires a full source commit SHA.")
    if not re.fullmatch(r"build-[0-9]+-[0-9]+", tag) and tag != f"v{version}":
        raise RuntimeError("Release tag must identify this CI run or match the CMake version.")

    checksums = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in files}
    (assets / "SHA256SUMS.txt").write_text(
        "".join(f"{digest}  {name}\n" for name, digest in sorted(checksums.items()))
    )
    (assets / "build-info.json").write_text(json.dumps({
        "version": version,
        "commit": commit,
        "tag": tag,
        "workflow_run": run_url,
        "sha256": checksums,
        "validation": "All four platform jobs passed, including tests, Linux private Qt checks, and explicit macOS architecture checks.",
        "limitations": "Unsigned, unnotarized, unsupported pre-release; not exhaustive clean-machine certification.",
    }, indent=2) + "\n")
    notes = assets / "release-notes.txt"
    notes.write_text(
        f"Automated RFdude Smith Chart V2 {version} pre-release.\n\n"
        f"Source commit: `{commit}`\n\n"
        f"[Passing build and test run]({run_url}). Linux x64, Windows x64, "
        "macOS Intel, and macOS Apple Silicon jobs passed. Linux packaging checks "
        "private Qt paths, dependency resolution, and extracted launcher startup "
        "under Xvfb; macOS builds verify the executable architecture before publication.\n\n"
        "Installers are unsigned and unsupported; macOS DMGs are not notarized. "
        "CI checks do not replace clean-machine installation/uninstallation testing.\n\n"
        "Linux Qt stays private under `/usr/lib/rfdudesmithchartv2`; only a launcher "
        "is installed in `/usr/bin`. Do not reuse the original unsafe v0.1.0 Linux "
        f"installer. See the [KDE recovery guidance](https://github.com/{repository}"
        "/releases/tag/v0.1.0) if you previously installed it.\n\n"
        "SHA-256 checksums and source/build metadata are attached. A new build has "
        "a unique release tag; previous release assets are never overwritten.\n"
    )
    command = [
        "gh", "release", "create", tag, "--repo", repository, "--target", commit,
        "--title", f"RFdude Smith Chart V2 {version} ({tag})",
        "--draft", "--prerelease", "--latest=false", "--notes-file", str(notes),
        *(str(path) for path in sorted(assets.iterdir()) if path != notes),
    ]
    if tag.startswith("v"):
        command.append("--verify-tag")
    # Upload the complete set while hidden; a failed upload leaves only a draft.
    subprocess.run(command, check=True)
    subprocess.run([
        "gh", "release", "edit", tag, "--repo", repository,
        "--draft=false", "--prerelease", "--latest=false",
    ], check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("assets", type=Path)
    args = parser.parse_args()
    cmake = (Path(__file__).resolve().parents[1] / "CMakeLists.txt").read_text()
    version_match = re.search(r"^\s*VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)\s*$", cmake, re.MULTILINE)
    if not version_match:
        raise RuntimeError("Could not read the CMake project version.")
    ref = os.environ["GITHUB_REF"]
    if ref.startswith("refs/tags/v"):
        tag = ref.removeprefix("refs/tags/")
    elif ref == "refs/heads/main":
        tag = f"build-{os.environ['GITHUB_RUN_ID']}-{os.environ['GITHUB_RUN_ATTEMPT']}"
    else:
        raise RuntimeError("Only main and version tags may publish releases.")
    repository = os.environ["GITHUB_REPOSITORY"]
    run_url = f"{os.environ['GITHUB_SERVER_URL']}/{repository}/actions/runs/{os.environ['GITHUB_RUN_ID']}"
    publish(args.assets, version_match.group(1), os.environ["GITHUB_SHA"], tag, run_url, repository)
