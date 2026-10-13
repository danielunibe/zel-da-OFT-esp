#!/usr/bin/env python3
"""Export the newest local Shipwright worktree as a reproducible development snapshot.

The tool reads a Shipwright checkout and its libultraship submodule without
changing either Git index or worktree. It writes a separate overlay, patch set,
and file manifest into this repository.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path, PurePosixPath

SHIPWRIGHT_BASE = "4aaad850bd5540cd77c2d83f3ad348d3b38605b2"
LIBULTRASHIP_BASE = "17a0b7939bd05f5e617cef89457ca43774fc9a9f"
PRIVATE_WINDOWS_PATH = re.compile(rb"[A-Z]:[\\/]+Users[\\/]+[^\\/ \t\r\n\"']+", re.IGNORECASE)


class ExportError(RuntimeError):
    pass


def run_git(root: Path, *args: str, expected_codes: tuple[int, ...] = (0,)) -> bytes:
    result = subprocess.run(
        ["git", *args],
        cwd=root,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode not in expected_codes:
        detail = result.stderr.decode("utf-8", errors="replace").strip()
        raise ExportError(f"git {' '.join(args)} failed ({result.returncode}): {detail}")
    return result.stdout


def nul_paths(data: bytes) -> list[str]:
    return [item.decode("utf-8", errors="surrogateescape") for item in data.split(b"\0") if item]


def verify_checkout(root: Path, expected_head: str, label: str) -> None:
    if not root.is_dir():
        raise ExportError(f"{label} checkout not found: {root}")
    head = run_git(root, "rev-parse", "HEAD").decode().strip()
    if head != expected_head:
        raise ExportError(f"{label} HEAD mismatch: expected {expected_head}, found {head}")


def changed_paths(root: Path, base: str, exclude_submodule: bool = False) -> tuple[list[str], list[str]]:
    pathspecs = [".", ":(exclude)libultraship"] if exclude_submodule else ["."]
    deleted = nul_paths(run_git(root, "diff", "--diff-filter=D", "--name-only", "-z", base, "--", *pathspecs))
    if deleted:
        raise ExportError(
            "The current snapshot deletes upstream files, which a copy overlay cannot represent: "
            + ", ".join(deleted)
        )
    modified = nul_paths(run_git(root, "diff", "--name-only", "-z", base, "--", *pathspecs))
    untracked = nul_paths(run_git(root, "ls-files", "--others", "--exclude-standard", "-z", "--", *pathspecs))
    paths = sorted(set(modified + untracked), key=str.casefold)
    if not paths:
        raise ExportError(f"No modified or untracked files found relative to {base} in {root}")
    for relative in paths:
        rel = PurePosixPath(relative)
        if rel.is_absolute() or ".." in rel.parts:
            raise ExportError(f"Unsafe source path: {relative}")
        source = root.joinpath(*rel.parts)
        if not source.is_file() or source.is_symlink():
            raise ExportError(f"Snapshot path is missing, not a file, or a symlink: {relative}")
        if PRIVATE_WINDOWS_PATH.search(source.read_bytes()):
            raise ExportError(f"Machine-specific Windows user path found in {relative}; sanitize it before export")
    return paths, untracked


def patch_for(root: Path, base: str, untracked: list[str], exclude_submodule: bool = False) -> bytes:
    pathspecs = [".", ":(exclude)libultraship"] if exclude_submodule else ["."]
    tracked = run_git(root, "diff", "--binary", "--full-index", base, "--", *pathspecs)
    parts = [tracked] if tracked else []
    for relative in sorted(untracked, key=str.casefold):
        addition = run_git(
            root,
            "diff",
            "--no-index",
            "--binary",
            "--full-index",
            "--",
            "/dev/null",
            relative,
            expected_codes=(0, 1),
        )
        if addition:
            parts.append(addition)
    patch = b"".join(parts)
    if patch and not patch.endswith(b"\n"):
        patch += b"\n"
    return patch


def safe_replace(path: Path, allowed_parent: Path) -> None:
    resolved_parent = allowed_parent.resolve()
    resolved_path = path.resolve()
    if resolved_path.parent != resolved_parent:
        raise ExportError(f"Refusing to replace a path outside {resolved_parent}: {resolved_path}")
    if path.is_dir():
        shutil.rmtree(path)
    elif path.exists():
        path.unlink()


def write_patch(path: Path, data: bytes, replace: bool) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and not replace:
        raise ExportError(f"Output already exists; pass --replace to update it: {path}")
    path.write_bytes(data)


def canonical_file_bytes(data: bytes) -> bytes:
    # Git's text normalization stores CRLF as LF; hash that portable form.
    # Binary assets retain their exact byte representation.
    if b"\0" not in data:
        return data.replace(b"\r\n", b"\n")
    return data


def copy_overlay(source_root: Path, relative_paths: list[str], target_root: Path) -> list[dict[str, object]]:
    target_root.mkdir(parents=True, exist_ok=True)
    records: list[dict[str, object]] = []
    for relative in relative_paths:
        rel = PurePosixPath(relative)
        source = source_root.joinpath(*rel.parts)
        target = target_root.joinpath(*rel.parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        canonical = canonical_file_bytes(target.read_bytes())
        digest = hashlib.sha256(canonical).hexdigest()
        records.append({"path": relative, "size_bytes": len(canonical), "sha256": digest})
    return records


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path, help="Path to the canonical Shipwright checkout")
    parser.add_argument("--replace", action="store_true", help="Replace this tool's development overlay and patch outputs")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[1]
    source_root = args.source.resolve()
    lus_root = source_root / "libultraship"
    verify_checkout(source_root, SHIPWRIGHT_BASE, "Shipwright")
    verify_checkout(lus_root, LIBULTRASHIP_BASE, "libultraship")

    ship_paths, ship_untracked = changed_paths(source_root, SHIPWRIGHT_BASE, exclude_submodule=True)
    lus_paths, lus_untracked = changed_paths(lus_root, LIBULTRASHIP_BASE)
    overlay_root = repo_root / "source-overlay" / "development"
    if overlay_root.exists():
        if not args.replace:
            raise ExportError(f"Overlay already exists; pass --replace to update it: {overlay_root}")
        safe_replace(overlay_root, overlay_root.parent)

    ship_overlay = overlay_root / "shipwright"
    lus_overlay = overlay_root / "libultraship"
    ship_records = copy_overlay(source_root, ship_paths, ship_overlay)
    lus_records = copy_overlay(lus_root, lus_paths, lus_overlay)

    ship_patch = patch_for(source_root, SHIPWRIGHT_BASE, ship_untracked, exclude_submodule=True)
    lus_patch = patch_for(lus_root, LIBULTRASHIP_BASE, lus_untracked)
    if not ship_patch or not lus_patch:
        raise ExportError("Generated an empty patch; refusing to publish an incomplete snapshot")

    patch_dir = repo_root / "patches" / "development"
    ship_patch_path = patch_dir / "shipwright_9_1_1_development.patch"
    lus_patch_path = patch_dir / "libultraship_1_3_1_development.patch"
    write_patch(ship_patch_path, ship_patch, args.replace)
    write_patch(lus_patch_path, lus_patch, args.replace)

    manifest_path = repo_root / "docs" / "development" / "SNAPSHOT_FILES.json"
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    if manifest_path.exists() and not args.replace:
        raise ExportError(f"Manifest already exists; pass --replace to update it: {manifest_path}")
    manifest = {
        "schema_version": 1,
        "base_commits": {
            "shipwright": SHIPWRIGHT_BASE,
            "libultraship": LIBULTRASHIP_BASE,
        },
        "file_hash_policy": "SHA-256 and size after CRLF-to-LF normalization for text files; binary files are hashed as stored",
        "source_head": {
            "shipwright": run_git(source_root, "rev-parse", "HEAD").decode().strip(),
            "libultraship": run_git(lus_root, "rev-parse", "HEAD").decode().strip(),
        },
        "files": {
            "shipwright": ship_records,
            "libultraship": lus_records,
        },
        "patches": {
            "shipwright": {
                "path": ship_patch_path.relative_to(repo_root).as_posix(),
                "size_bytes": len(ship_patch),
                "sha256": hashlib.sha256(ship_patch).hexdigest(),
            },
            "libultraship": {
                "path": lus_patch_path.relative_to(repo_root).as_posix(),
                "size_bytes": len(lus_patch),
                "sha256": hashlib.sha256(lus_patch).hexdigest(),
            },
        },
    }
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Shipwright files: {len(ship_records)}")
    print(f"libultraship files: {len(lus_records)}")
    print(f"Shipwright patch: {ship_patch_path} ({len(ship_patch)} bytes, {sha256(ship_patch_path)})")
    print(f"libultraship patch: {lus_patch_path} ({len(lus_patch)} bytes, {sha256(lus_patch_path)})")
    print(f"File manifest: {manifest_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ExportError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(2)