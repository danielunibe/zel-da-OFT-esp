"""Measure Navi's production point-light contribution in one hidden GPU frame.

The analyzer pairs the regular compositor with its same-frame Navi-point-off
ablation. It does not equate ray hits or a nonzero intermediate buffer with a
visible lighting result.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def read(path: Path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def capture(run: Path, stage: str, expected_format: str):
    meta = read(run / "captures" / f"{stage}.meta.json")
    if meta.get("format") != expected_format:
        raise ValueError(f"Unexpected capture format for {stage}: {meta.get('format')}")
    payload = (run / "captures" / meta["payload"]).resolve()
    if payload.parent != (run / "captures").resolve() or not payload.is_file():
        raise ValueError(f"Missing or invalid payload for {stage}")
    return meta, payload


def analyze(run: Path):
    run = run.resolve()
    sources = read(run / "navi.sources.json")
    launcher = read(run / "launcher.status.json")
    verify = read(run / "verification.status.json")
    pose = read(run / "navi.pose.json")
    source = next((s for s in sources["sources"]
                   if s.get("ownerId") == sources.get("selectedOwner") and s.get("role") == 1), None)
    if not source:
        raise ValueError("Navi's native point source identity was not captured")
    if sources.get("captureMode") != "PRODUCTION_COMPOSITE" or sources.get("isolation") != "NONE":
        raise ValueError("This analyzer requires an ordinary production-compositor capture")

    metas = {}
    paths = {}
    stages = {
        "direct": ("navi_point_direct", "R16G16B16A16_FLOAT"),
        "off": ("navi_point_off_sdr", "R8G8B8A8_UNORM"),
        "on": ("navi_point_on_sdr", "R8G8B8A8_UNORM"),
    }
    for key, (stage, fmt) in stages.items():
        metas[key], paths[key] = capture(run, stage, fmt)
    base = metas["off"]
    for meta in metas.values():
        if meta.get("frame") != sources.get("frame") or meta.get("frame") != base.get("frame"):
            raise ValueError("Source, direct buffer, and ON/OFF captures must be from one GPU frame")
        if meta.get("request") != base.get("request"):
            raise ValueError("Capture requests do not match")
    request = base["request"]
    scenario = request.get("scenario_id")
    if not scenario:
        raise ValueError("Capture request does not identify a scenario")

    height, width = int(metas["direct"]["height"]), int(metas["direct"]["width"])
    direct = np.fromfile(paths["direct"], dtype="<f2")
    if direct.size != height * width * 4:
        raise ValueError("Navi direct-radiance payload does not match its dimensions")
    direct = direct.reshape(height, width, 4).astype(np.float32)[..., :3]
    finite = bool(np.isfinite(direct).all())
    if not finite:
        raise ValueError("Navi direct-radiance buffer contains NaN or infinity")
    direct_luma = direct @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    direct_summary = {
        "width": width, "height": height,
        "nonzero_rgb_pixels": int(np.count_nonzero(np.max(np.abs(direct), axis=2) > 1e-7)),
        "mean_linear_luminance": float(direct_luma.mean()),
        "max_linear_luminance": float(direct_luma.max(initial=0.0)),
        "max_abs_linear_rgb": float(np.max(np.abs(direct), initial=0.0)),
        "finite": finite,
        "color_space": "HDR_LINEAR",
    }

    preview_paths = {}
    images = {}
    sdr = {}
    for key, stage in (("off", "navi_point_off_sdr"), ("on", "navi_point_on_sdr")):
        meta = metas[key]
        preview_name = meta["payload"].removesuffix("_raw.png") + "_opaque_preview.png"
        preview = (run / "captures" / preview_name).resolve()
        if preview.parent != (run / "captures").resolve() or not preview.is_file():
            raise ValueError(f"Missing production preview for {stage}")
        image = Image.open(preview).convert("RGB")
        sdr[key] = np.asarray(image, dtype=np.uint8)
        if image.size != (int(meta["width"]), int(meta["height"])):
            raise ValueError(f"Preview dimensions mismatch for {stage}")
        preview_paths[key] = preview_name
        images[stage] = {"raw": meta["payload"], "preview": preview_name,
                         "sha256": sha256(preview)}
    if sdr["on"].shape != sdr["off"].shape:
        raise ValueError("Production ON/OFF images have different dimensions")
    delta = np.abs(sdr["on"].astype(np.int16) - sdr["off"].astype(np.int16))
    per_pixel = delta.max(axis=2)
    pair = {
        "on": "navi_point_on_sdr", "off": "navi_point_off_sdr",
        "labels": ["Puntual Navi desactivada", "Compositor normal · puntual Navi activa"],
        "total_pixels": int(per_pixel.size),
        "mean_absolute_rgb_byte_delta": float(delta.mean()),
        "max_channel_byte_delta": int(per_pixel.max(initial=0)),
        "pixel_counts_above_byte_delta": {
            str(n): int(np.count_nonzero(per_pixel > n)) for n in (0, 2, 8, 16, 32)
        },
        "hdr_linear": {
            "mean_absolute_rgb_delta": float(np.abs(direct).mean()),
            "max_absolute_rgb_delta": float(np.abs(direct).max(initial=0.0)),
        },
        "tone_mapping": "PRODUCTION_ACES_GAMMA22",
    }

    presentations = [json.loads(line) for line in
                     (run / "presentation.evidence.jsonl").read_text(encoding="utf-8-sig").splitlines()
                     if line.strip()]
    hidden = bool(presentations) and all(
        p.get("dxgi_present_call") == "SKIPPED_BACKGROUND" and
        not p.get("window_visible") and not p.get("window_foreground") for p in presentations)
    runtime_ok = bool(launcher.get("background") and launcher.get("processExited") and
                      launcher.get("originalSaveUnchanged") and not launcher.get("backendDeviceRemoved") and
                      not verify.get("failures") and verify.get("sameFrame") and hidden)
    active = bool(source.get("enabled") and abs(float(source.get("rawRadius", 0))) > 0)
    direct_present = direct_summary["nonzero_rgb_pixels"] > 0
    visible = int(np.count_nonzero(per_pixel > 0))
    gates = {
        "hidden_isolated_runtime_and_save_integrity": "PASS" if runtime_ok else "FAIL",
        "native_navi_source_identity_and_activation": "PASS" if active else "FAIL",
        "same_frame_production_on_off_pair": "PASS",
        "finite_hdr_native_point_buffer": "PASS" if finite else "FAIL",
        "native_point_radiance_reaches_receivers": "PASS" if direct_present else "FAIL",
        "visible_final_compositor_difference": "PASS" if visible > 0 else "FAIL",
        "physical_coherence": "PASS_SCOPED" if active and direct_present and visible > 0 and runtime_ok else "INCOMPLETE",
        "human_visual_acceptance": "PENDING",
    }
    record = {
        "schema_version": 1, "kind": "NAVI_POINT_PRODUCTION",
        "status": "MEASURED_PRODUCTION_POINT_CONTRIBUTION",
        "run_id": run.name, "scenario": scenario, "view": request.get("view_id"),
        "frame": base["frame"], "width": int(metas["on"]["width"]),
        "height": int(metas["on"]["height"]), "images": images,
        "pairs": {"navi_point_production": pair}, "source": source,
        "sources": sources["sources"], "pose": pose, "gates": gates,
        "production": {"direct_radiance": direct_summary,
                       "final_changed_pixels": visible,
                       "final_changed_pixel_fraction": visible / max(1, per_pixel.size),
                       "other_game_lights": "left in the production scene for both same-frame images",
                       "bloom": sources.get("bloom")},
        "identity": {
            "shader_source_sha256": sources["shaderSourceSha256"],
            "shader_abi": sources["shaderAbi"],
            "executable_sha256": read(run / "manifest.launcher.json")["executableIdentity"]["sha256"],
            "launcher_manifest": read(run / "manifest.launcher.json"),
        },
        "representative_receiver": None, "receiver_samples": [],
        "physical_coherence": gates["physical_coherence"],
        "visual_acceptance": "PENDING_HUMAN_REVIEW",
        "hidden_no_present_verified": hidden,
        "original_saves_unchanged": bool(launcher.get("originalSaveUnchanged")),
    }
    (run / "navi-production-analysis.json").write_text(
        json.dumps(record, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    return record


def publish(record):
    path = ROOT / "reports/dxr_comparison/suite.status.json"
    data = read(path) if path.exists() else {"items": []}
    data["items"] = [item for item in data.get("items", [])
                     if item.get("comparison", {}).get("run_id") != record["run_id"]]
    data["items"].insert(0, {"scenario": record["scenario"], "status": record["physical_coherence"],
                             "comparison": record, "failures": []})
    data.update(status="NAVI_PRODUCTION_UPDATED", current=None,
                finished_utc=datetime.now(timezone.utc).isoformat())
    temp = path.with_suffix(".tmp")
    temp.write_text(json.dumps(data, ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")
    temp.replace(path)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    parser.add_argument("--publish", action="store_true")
    args = parser.parse_args()
    result = analyze(args.run)
    if args.publish:
        publish(result)
    print(json.dumps({"scenario": result["scenario"], "frame": result["frame"],
                      "gates": result["gates"], "direct_radiance": result["production"]["direct_radiance"],
                      "final_changed_pixels": result["production"]["final_changed_pixels"]}, indent=2))
