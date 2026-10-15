"""Check source/overlay/build-mirror identity without changing any checkout."""
import hashlib
import json
import argparse
from pathlib import Path


def portable_bytes(path):
    data = path.read_bytes()
    return data.replace(b"\r\n", b"\n") if b"\0" not in data else data


root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--runtime", type=Path, default=root / "runtime/background-ray-validation-20261013-213613")
parser.add_argument("--output", type=Path, default=root / "reports/OCZ_RTX_BACKGROUND_SOURCE_PARITY.json")
args = parser.parse_args()
manifest = json.loads((root / "docs/development/SNAPSHOT_FILES.json").read_text())
mirror = root / "build/OCZ_RTX_HOTFIX_02_RELEASE/source_clean"
sources = {
    "shipwright": root / "source/shipwright",
    "libultraship": root / "source/shipwright/libultraship",
}
mirrors = {"shipwright": mirror, "libultraship": mirror / "libultraship"}
failures = []
counts = {}
for kind, records in manifest["files"].items():
    counts[kind] = len(records)
    for record in records:
        for label, base in (
            ("canonical", sources[kind]),
            ("overlay", root / "source-overlay/development" / kind),
            ("build_mirror", mirrors[kind]),
        ):
            path = base / record["path"]
            if not path.is_file():
                failures.append({"kind": kind, "copy": label, "path": record["path"], "reason": "MISSING"})
                continue
            data = portable_bytes(path)
            if hashlib.sha256(data).hexdigest() != record["sha256"] or len(data) != record["size_bytes"]:
                failures.append({"kind": kind, "copy": label, "path": record["path"], "reason": "CONTENT_DIFFERS"})

build_exe = mirror / "x64/Release/soh.exe"
runtime_exe = args.runtime / "soh.exe"
exe_match = build_exe.is_file() and runtime_exe.is_file() and build_exe.read_bytes() == runtime_exe.read_bytes()
result = {
    "schema_version": 1,
    "status": "PASS" if not failures and exe_match else "FAIL",
    "file_counts": counts,
    "failures": failures,
    "canonical_overlay_build_mirror_match": not failures,
    "runtime_matches_build_executable": exe_match,
    "executable_sha256": hashlib.sha256(build_exe.read_bytes()).hexdigest() if build_exe.is_file() else None,
    "build_mirror": str(mirror),
    "base_commits": manifest["base_commits"],
    "patches": manifest["patches"],
}
out = args.output
out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
print(json.dumps(result, indent=2))
raise SystemExit(0 if result["status"] == "PASS" else 1)
