#!/usr/bin/env python3
"""Validate catalogue references, paths, IDs, and client usage."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
CATALOG_PATH = ROOT / "assets" / "catalog.json"
ASSET_ID_RE = re.compile(r"^[a-z][a-z0-9]*(?:[.-][a-z0-9]+)*$")
ASSET_LITERAL_RE = re.compile(
    r"(?:texture_for_id\s*\(\s*|asset_id['\"]?\s*:\s*|asset_id\s*:?=\s*)['\"]([^'\"]+)['\"]"
)
SOURCE_SUFFIXES = {".gd", ".ts", ".tsx", ".js", ".jsx", ".py", ".kt", ".swift", ".java", ".dart"}
IMAGE_SUFFIXES = {".webp", ".svg", ".png", ".jpg", ".jpeg", ".avif"}
IGNORED_DIRS = {".godot", "node_modules", "dist", "build", "coverage", "__pycache__"}
NAMESPACE_CATEGORY = {
    "location.": "locations",
    "planet.class.": "locations",
    "ship.": "ships",
    "commodity.": "commodities",
    "hardware.": "hardware",
    "ui.": "UI",
}


class Validation:
    def __init__(self) -> None:
        self.errors: list[str] = []
        self.warnings: list[str] = []

    def error(self, message: str) -> None:
        self.errors.append(message)

    def warning(self, message: str) -> None:
        self.warnings.append(message)


def namespace_category(asset_id: str) -> str | None:
    for prefix, category in NAMESPACE_CATEGORY.items():
        if asset_id.startswith(prefix):
            return category
    return None


def walk_mapping_ids(value: Any, location: str, result: list[tuple[str, str]]) -> None:
    if isinstance(value, dict):
        for key, child in value.items():
            walk_mapping_ids(child, f"{location}.{key}", result)
    elif isinstance(value, list):
        for index, child in enumerate(value):
            walk_mapping_ids(child, f"{location}[{index}]", result)
    elif isinstance(value, str) and namespace_category(value) is not None:
        result.append((value, location))


def client_source_files(source_roots: list[Path]) -> list[Path]:
    files: list[Path] = []
    for source_root in source_roots:
        if not source_root.exists():
            continue
        for path in source_root.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
                continue
            if any(part in IGNORED_DIRS for part in path.parts):
                continue
            if "assets" in path.relative_to(source_root).parts:
                continue
            files.append(path)
    return files


def validate(catalog_path: Path, source_roots: list[Path]) -> Validation:
    result = Validation()
    try:
        catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        result.error(f"cannot read catalogue {catalog_path}: {exc}")
        return result

    if not isinstance(catalog, dict) or not isinstance(catalog.get("assets"), list):
        result.error("catalogue must be an object with an assets array")
        return result

    entries: dict[str, dict[str, Any]] = {}
    catalogued_paths: set[Path] = set()
    for index, entry in enumerate(catalog["assets"]):
        location = f"assets[{index}]"
        if not isinstance(entry, dict):
            result.error(f"{location} must be an object")
            continue
        asset_id = entry.get("asset_id")
        if not isinstance(asset_id, str) or not ASSET_ID_RE.fullmatch(asset_id):
            result.error(f"{location} has invalid asset_id {asset_id!r}")
            continue
        if asset_id in entries:
            result.error(f"duplicate asset_id {asset_id!r}")
            continue
        entries[asset_id] = entry
        expected_category = namespace_category(asset_id)
        actual_category = entry.get("category")
        if expected_category is None:
            result.error(f"{asset_id}: ID must use a supported namespace")
        elif actual_category != expected_category:
            result.error(
                f"{asset_id}: category {actual_category!r} should be {expected_category!r}"
            )
        if not str(entry.get("display_name", "")).strip():
            result.error(f"{asset_id}: display_name is required")

        supported = entry.get("supported_sizes")
        paths = entry.get("paths")
        if not isinstance(supported, list) or not isinstance(paths, dict):
            result.error(f"{asset_id}: paths and supported_sizes must be defined")
            continue
        for size in supported:
            if size not in paths:
                result.error(f"{asset_id}: missing path for supported size {size!r}")
        for size, relative_path in paths.items():
            if size not in supported:
                result.error(f"{asset_id}: path size {size!r} is not in supported_sizes")
            if not isinstance(relative_path, str) or not relative_path.startswith("assets/"):
                result.error(f"{asset_id}: path for {size!r} must be assets/-relative")
                continue
            disk_path = (ROOT / relative_path).resolve()
            if not disk_path.is_relative_to(ROOT.resolve()):
                result.error(f"{asset_id}: path escapes repository root: {relative_path!r}")
                continue
            catalogued_paths.add(disk_path)
            if not disk_path.is_file():
                result.error(f"{asset_id}: missing image file {relative_path}")
            elif disk_path.suffix.lower() not in IMAGE_SUFFIXES:
                result.error(f"{asset_id}: unsupported image file type {relative_path}")

    mapping_ids: list[tuple[str, str]] = []
    mappings = catalog.get("object_mappings", {})
    if not isinstance(mappings, dict):
        result.error("object_mappings must be an object")
    else:
        for required_kind in ("port", "planet", "ship"):
            if required_kind not in mappings:
                result.error(f"object_mappings is missing {required_kind!r}")
        walk_mapping_ids(mappings, "object_mappings", mapping_ids)

    used_ids: set[str] = set()
    for asset_id, where in mapping_ids:
        if not ASSET_ID_RE.fullmatch(asset_id):
            result.error(f"{where}: invalid asset ID {asset_id!r}")
        elif asset_id not in entries:
            result.error(f"{where}: missing catalogue entry for {asset_id!r}")
        else:
            used_ids.add(asset_id)

    literal_refs: dict[str, list[str]] = {}
    for source_root in source_roots:
        for path in client_source_files([source_root]):
            try:
                text = path.read_text(encoding="utf-8")
            except (OSError, UnicodeDecodeError):
                continue
            for match in ASSET_LITERAL_RE.finditer(text):
                asset_id = match.group(1)
                # Namespace fragments used to build an ID dynamically are not
                # asset references (for example "planet.class." in Godot).
                if asset_id.endswith((".", "-", "_")) or namespace_category(asset_id) is None:
                    continue
                literal_refs.setdefault(asset_id, []).append(str(path.relative_to(ROOT)))
                if not ASSET_ID_RE.fullmatch(asset_id):
                    result.error(f"{path}: invalid asset ID literal {asset_id!r}")
                elif asset_id not in entries:
                    result.error(f"{path}: missing catalogue entry for {asset_id!r}")
                else:
                    used_ids.add(asset_id)

    for asset_id in sorted(entries.keys() - used_ids):
        result.warning(f"unused catalogue asset {asset_id!r}")

    for folder in (ROOT / "assets").rglob("*"):
        if not folder.is_file() or folder.suffix.lower() not in IMAGE_SUFFIXES:
            continue
        if folder.name.startswith("source-"):
            continue
        resolved = folder.resolve()
        if resolved not in catalogued_paths:
            result.warning(f"image file is not listed by the catalogue: {folder.relative_to(ROOT)}")

    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, default=CATALOG_PATH)
    parser.add_argument(
        "--source",
        type=Path,
        action="append",
        help="client source tree to scan (repeatable; defaults to client/)",
    )
    args = parser.parse_args()
    roots = args.source or [ROOT / "client"]
    result = validate(args.catalog.resolve(), [path.resolve() for path in roots])
    for warning in result.warnings:
        print(f"WARN: {warning}")
    for error in result.errors:
        print(f"ERROR: {error}")
    if result.errors:
        print(f"FAIL: {len(result.errors)} error(s), {len(result.warnings)} warning(s)")
        return 1
    print(
        f"PASS: {len(json.loads(args.catalog.read_text(encoding='utf-8'))['assets'])} catalogue assets; "
        f"{len(result.warnings)} warning(s)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
