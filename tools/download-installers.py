#!/usr/bin/env python3
"""Download successful CI installers for all supported platforms into a dated build folder."""

import argparse
import datetime
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run_id", type=int, help="Successful Build and test Actions run ID")
    parser.add_argument("--destination", type=Path,
                        default=Path.home() / "Downloads/RFdudeSmithChartV2")
    args = parser.parse_args()
    repo = "llascari/RFdudeSmithChartV2"
    run = json.loads(subprocess.check_output(
        ["gh", "run", "view", str(args.run_id), "--repo", repo,
         "--json", "conclusion,headSha,url,workflowName,jobs"], text=True))
    if run["conclusion"] != "success" or run["workflowName"] != "Build and test":
        parser.error("Choose a successful Build and test workflow run.")
    platforms = ("Linux x64", "Windows x64", "macOS Apple Silicon", "macOS Intel")
    for platform in platforms:
        if not any(job["name"] == platform and job["conclusion"] == "success"
                   for job in run["jobs"]):
            parser.error(f"Run is missing a successful {platform} job.")
    destination = args.destination.expanduser()
    destination.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".download-", dir=destination) as temporary:
        subprocess.run(
            ["gh", "run", "download", str(args.run_id), "--repo", repo,
             "--name", "RFdudeSmithChartV2-linux-x64",
             "--name", "RFdudeSmithChartV2-windows-x64",
             "--name", "RFdudeSmithChartV2-macos-arm64",
             "--name", "RFdudeSmithChartV2-macos-x64", "--dir", temporary], check=True)
        installers = sorted(path for path in Path(temporary).rglob("*")
                            if path.is_file() and (path.suffix == ".deb"
                                                  or path.suffix == ".dmg"
                                                  or path.name.endswith("-setup.exe")))
        if (len(installers) != 4
                or sum(path.suffix == ".deb" for path in installers) != 1
                or sum(path.suffix == ".dmg" for path in installers) != 2
                or sum(path.name.endswith("-setup.exe") for path in installers) != 1):
            parser.error("Expected one Linux .deb, one Windows setup .exe, and two macOS DMGs.")
        today = datetime.date.today().isoformat()
        sequences = [int(path.name[len(today) + 1:]) for path in destination.iterdir()
                     if path.name.startswith(today + "-")
                     and path.name[len(today) + 1:].isdigit()]
        sequence = max(sequences, default=0) + 1
        if sequence > 99:
            parser.error("All two-digit build numbers for today have been used.")
        output = destination / f"{today}-{sequence:02d}"
        output.mkdir()
        checksums = {}
        for installer in installers:
            target = output / installer.name
            shutil.copyfile(installer, target)
            checksums[target.name] = hashlib.sha256(target.read_bytes()).hexdigest()
        manifest = {"run_id": args.run_id, "url": run["url"], "commit": run["headSha"],
                    "downloaded_at": datetime.datetime.now().astimezone().isoformat(),
                    "sha256": checksums}
        (output / "build-info.json").write_text(json.dumps(manifest, indent=2) + "\n")
        print(output)
        for name, checksum in checksums.items():
            print(f"{checksum}  {name}")


if __name__ == "__main__":
    main()
