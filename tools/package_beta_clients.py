#!/usr/bin/env python3
"""Build deterministic source bundles for the V3 beta download site."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import zipfile
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PACKAGES = {
    "python": ("client/python_client", "twclone-python-v3", "Linux x86_64 · Python 3.9+"),
    "godot": ("client/godot_client/godot", "twclone-godot-project-v3", "Linux x86_64 · Godot 4.5.2"),
}


def git(*args: str) -> bytes:
    return subprocess.check_output(["git", *args], cwd=ROOT)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--commit", default="HEAD")
    parser.add_argument("--output", type=Path, default=ROOT / "website-beta" / "downloads")
    args = parser.parse_args()
    commit = git("rev-parse", args.commit).decode().strip()
    short = commit[:12]
    epoch = int(git("show", "-s", "--format=%ct", commit).decode().strip())
    stamp = datetime.fromtimestamp(max(epoch, 315532800), timezone.utc)
    zip_time = (stamp.year, stamp.month, stamp.day, stamp.hour, stamp.minute, stamp.second // 2 * 2)
    args.output.mkdir(parents=True, exist_ok=True)
    records = []

    for kind, (source_root, prefix, platform) in PACKAGES.items():
        name = f"{prefix}-{short}.zip"
        output = args.output / name
        paths = git("ls-tree", "-r", "--name-only", commit, "--", source_root).decode().splitlines()
        with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for path in sorted(paths):
                relative = path[len(source_root) + 1:]
                if not relative or ".godot/" in relative or relative.startswith(".godot/"):
                    continue
                info = zipfile.ZipInfo(f"{prefix}-{short}/{relative}", zip_time)
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o100644 << 16
                archive.writestr(info, git("show", f"{commit}:{path}"), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
            install = (
                "TWClone V3 beta source bundle\n"
                f"Source commit: {commit}\n"
                f"Client: {kind}\n"
                "See the download page at https://twclone.impara.co.uk/ for setup instructions.\n"
            )
            info = zipfile.ZipInfo(f"{prefix}-{short}/BETA-BUILD.txt", zip_time)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, install)
        digest = hashlib.sha256(output.read_bytes()).hexdigest()
        records.append({
            "client": kind, "file": name, "platform": platform,
            "sha256": digest, "commit": commit, "bytes": output.stat().st_size,
        })

    (args.output / "SHA256SUMS").write_text("".join(f"{item['sha256']}  {item['file']}\n" for item in records), encoding="ascii")
    aliases = {
        "python": "twclone-python-v3.zip",
        "godot": "twclone-godot-project-v3.zip",
    }
    for item in records:
        alias = args.output / aliases[item["client"]]
        alias.write_bytes((args.output / item["file"]).read_bytes())
    (args.output / "release.json").write_text(json.dumps({
        "version": f"v3-beta-{short}",
        "commit": commit,
        "release_date": stamp.strftime("%Y-%m-%d"),
        "artifacts": records,
    }, indent=2) + "\n", encoding="utf-8")
    for item in records:
        print(f"{item['sha256']}  {item['file']}  {item['bytes']} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
