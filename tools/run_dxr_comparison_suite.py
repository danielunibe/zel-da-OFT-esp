"""Run mapped scenarios sequentially in the existing hidden scene harness; publish live results."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import subprocess
import sys

from analyze_background_ray_capture import analyze as analyze_rays
from analyze_dxr_comparison import analyze as analyze_comparison
from analyze_visual_acceptance import analyze as analyze_application

ROOT = Path(__file__).resolve().parents[1]


def publish(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(data, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--executable", type=Path)
    parser.add_argument("--scenarios", nargs="+", default=["BASELINE_KOKIRI", "LAKE_HYLIA_DAY",
                        "TEMPLE_INTERIOR", "HYRULE_FIELD_DAY", "HYRULE_FIELD_NIGHT"])
    parser.add_argument("--skip-hit-miss", action="store_true")
    parser.add_argument("--status", type=Path, default=ROOT / "reports/dxr_comparison/suite.status.json")
    args = parser.parse_args()
    runtime = args.runtime.resolve()
    exe = (args.executable or runtime / "soh.exe").resolve()
    if not runtime.is_dir() or not exe.is_file():
        parser.error("The configured isolated runtime/executable is missing")
    registry = json.loads((ROOT / "source/shipwright/scripts/scene-harness/scenarios/scenarios.v1.json").read_text())
    known = {item["id"] for item in registry["scenarios"] if item["status"] == "MAPPED"}
    if len(set(args.scenarios)) != len(args.scenarios) or not set(args.scenarios).issubset(known):
        parser.error("Scenarios must be unique MAPPED scenario ids")
    state = {"schema_version": 1, "started_utc": datetime.now(timezone.utc).isoformat(),
        "status": "RUNNING", "scenarios": args.scenarios, "current": None, "items": [],
        "mode": "HIDDEN_GPU_CAPTURES", "update_scope": "PUBLISHED_AFTER_EACH_CAPTURE; NOT_A_GAME_VIDEO_STREAM"}
    publish(args.status, state)
    for scenario in args.scenarios:
        item = {"scenario": scenario, "status": "RUNNING", "comparison": None, "rays": None, "failures": []}
        state["items"].append(item)
        modes = ["NORMAL"] if args.skip_hit_miss else ["NORMAL", "HIT_MISS"]
        for mode in modes:
            state["current"] = {"scenario": scenario, "truth_mode": mode}
            publish(args.status, state)
            command = ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                str(ROOT / "source/shipwright/scripts/scene-harness/run-scene.ps1"),
                "-Executable", str(exe), "-RuntimeDirectory", str(runtime), "-Background",
                "-RunScenario", scenario, "-TruthMode", mode, "-VisualProfile", "2", "-RtxQuality", "3",
                "-EnableD3D12DebugLayer", "-PollSeconds", "300"]
            if mode == "NORMAL":
                command.append("-VisualAblation")
            print(f"RUNNING {scenario} {mode}", flush=True)
            # run-scene owns its renderer and terminates it in finally. Never launch
            # another game instance until this bounded launcher has completed.
            process = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                encoding="utf-8", errors="replace", creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            print(process.stdout.strip(), flush=True)
            match = re.search(r"^RUN_ROOT=(.+)$", process.stdout, re.MULTILINE)
            if not match:
                item["failures"].append({"mode": mode, "reason": "NO_RUN_ROOT",
                    "exit_code": process.returncode, "stderr": process.stderr[-2000:]})
                publish(args.status, state)
                continue
            run = Path(match.group(1).strip())
            try:
                if mode == "NORMAL":
                    comparison = analyze_comparison(run)
                    application = analyze_application(run, expect_shadows=False, expect_optics=False)
                    item["comparison"] = comparison
                    item["application"] = application
                    if not application["status"].startswith("PASS"):
                        item["failures"].append({"mode": mode, "reason": application["status"], "details": application["failures"]})
                else:
                    rays = analyze_rays(run)
                    (run / "primary-ray-analysis.json").write_text(json.dumps(rays, indent=2, allow_nan=False) + "\n")
                    item["rays"] = rays
                    if rays["result"] != "PASS_SCOPED_GPU_OBSERVATION":
                        item["failures"].append({"mode": mode, "reason": rays["result"]})
            except (OSError, ValueError, KeyError, StopIteration) as error:
                item["failures"].append({"mode": mode, "run_id": run.name, "reason": str(error)})
            publish(args.status, state)
        item["status"] = "FAIL_OR_INCOMPLETE" if item["failures"] else "PASS_SCOPED_GPU_COMPARISON"
        publish(args.status, state)
    state["current"] = None
    state["status"] = "COMPLETE_WITH_FAILURES" if any(item["failures"] for item in state["items"]) else "COMPLETE_SCOPED_GPU_COMPARISONS"
    state["finished_utc"] = datetime.now(timezone.utc).isoformat()
    state["global_visual_quality"] = "NOT_CERTIFIED_BY_NUMERICAL_CHANGE"
    publish(args.status, state)
    print("COMPARISON_SUITE=" + state["status"], flush=True)
    return 1 if state["status"] == "COMPLETE_WITH_FAILURES" else 0


if __name__ == "__main__":
    raise SystemExit(main())
