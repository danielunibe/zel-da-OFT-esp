"""Analyze five GPU-rendered SDR controls from one frame; export an honest A/B preview."""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

STAGES = ("comparison_dxr_off", "comparison_dxr_on", "comparison_shadows_only",
          "comparison_optics_off", "comparison_indirect_off")


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def analyze(run):
    images, metas, files = {}, {}, {}
    for name in STAGES:
        meta = read_json(run / "captures" / (name + ".meta.json"))
        path = run / "captures" / meta["payload"]
        if path.resolve().parent != (run / "captures").resolve():
            raise ValueError("Capture payload is outside the run's capture directory")
        if meta["format"] != "R8G8B8A8_UNORM":
            raise ValueError("The comparison requires GPU SDR outputs, not a CPU tone-map preview")
        images[name] = np.asarray(Image.open(path).convert("RGB"))
        if images[name].shape != (meta["height"], meta["width"], 3):
            raise ValueError("Capture dimensions differ from metadata")
        metas[name] = meta
        files[name] = {"raw": meta["payload"],
                       "preview": meta["payload"].removesuffix("_raw.png") + "_opaque_preview.png",
                       "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
        if not (run / "captures" / files[name]["preview"]).is_file():
            raise ValueError("Opaque GPU preview is missing")
    first = metas[STAGES[0]]
    if any(meta["frame"] != first["frame"] or meta["request"] != first["request"]
           or images[name].shape != images[STAGES[0]].shape for name, meta in metas.items()):
        raise ValueError("Images do not share the same frame, request, camera, and dimensions")
    if first["request"]["truth_mode"] != "NORMAL":
        raise ValueError("Material diagnostics cannot serve as a normal-image comparison")
    gpu_log = (run / "harness-d3d12.log").read_text(errors="replace")
    for stage in STAGES:
        marker = f"[COUCH-DXR-COMPARISON] frame={first['frame']} stage={stage} source=GPU sameCommandList=1"
        line = next((line for line in gpu_log.splitlines() if line.startswith(marker)), "")
        if not line or "sameToneParameters=1" not in line:
            raise ValueError("GPU comparison provenance missing: " + stage)
    pairs = {
        "full_dxr": ("comparison_dxr_on", "comparison_dxr_off"),
        "shadows": ("comparison_shadows_only", "comparison_dxr_off"),
        "surface_optics": ("comparison_dxr_on", "comparison_optics_off"),
        "indirect": ("comparison_dxr_on", "comparison_indirect_off"),
    }
    measurements = {}
    for name, (on, off) in pairs.items():
        d = np.abs(images[on].astype(np.int16) - images[off].astype(np.int16))
        magnitude = d.max(axis=2)
        measurements[name] = {"on": on, "off": off, "total_pixels": int(magnitude.size),
            "mean_absolute_rgb_byte_delta": float(d.mean()), "max_channel_byte_delta": int(magnitude.max()),
            "p95_max_channel_delta": float(np.quantile(magnitude, 0.95)),
            "p99_max_channel_delta": float(np.quantile(magnitude, 0.99)),
            "pixel_counts_above_byte_delta": {str(n): int(np.count_nonzero(magnitude > n)) for n in (0, 2, 8, 16, 32)},
            "interpretation": "MEASURED_SDR_CHANGE; thresholds are descriptive, not a perceptual certification"}
    launcher = read_json(run / "launcher.status.json")
    verification = read_json(run / "verification.status.json")
    presentations = [json.loads(line) for line in (run / "presentation.evidence.jsonl").read_text().splitlines() if line.strip()]
    hidden = bool(presentations) and all(row["dxgi_present_call"] == "SKIPPED_BACKGROUND"
        and not row["window_visible"] and not row["window_foreground"] for row in presentations)
    if not (launcher["background"] and launcher["processExited"] and launcher["originalSaveUnchanged"]
            and not launcher["backendDeviceRemoved"] and not verification["failures"]
            and verification["captureStatus"] == "CAPTURE_COMPLETE" and verification["sameFrame"] and hidden):
        raise ValueError("Run integrity, cleanup, hidden presentation, or required capture verification failed")
    result = {"schema_version": 1, "status": "PASS_SCOPED_SAME_FRAME_GPU_COMPARISON",
        "run_id": run.name, "scenario": first["request"]["scenario_id"], "view": first["request"]["view_id"],
        "frame": first["frame"], "width": first["width"], "height": first["height"],
        "request": first["request"], "same_frame": True, "source": "GPU_RENDER_TARGET_READBACK",
        "tone": "PRODUCTION_FRAMEBUFFER_ACES_GAMMA22_WITH_SAME_SCENE_FOG", "hud": "EXCLUDED_FROM_BOTH_IMAGES",
        "off_scope": "DXR_STAGES_REMOVED; ENHANCED_RASTER_AND_TONEMAPPING_RETAINED",
        "images": files, "pairs": measurements,
        "hidden_no_present_verified": hidden, "own_test_process_stopped": True,
        "global_art_quality": "NOT_CERTIFIED", "original_saves_unchanged": True}
    final_meta = read_json(run / "captures/final_sdr_pre_present.meta.json")
    if final_meta["frame"] != first["frame"] or final_meta["request"] != first["request"]:
        raise ValueError("The final game image belongs to another frame/request")
    final_rgb = np.asarray(Image.open(run / "captures" / final_meta["payload"]).convert("RGB"))
    if final_rgb.shape != images["comparison_dxr_on"].shape:
        raise ValueError("The final image dimensions differ from the GPU comparison")
    final_delta = np.max(np.abs(final_rgb.astype(np.int16) - images["comparison_dxr_on"].astype(np.int16)), axis=2)
    h, w = final_delta.shape
    center_delta = final_delta[int(h*.25):int(h*.8), int(w*.2):int(w*.7)]
    result["correlation_with_final_game_image"] = {
        "exact_rgb_match_percent": float(100 * np.mean(final_delta == 0)),
        "within_two_byte_codes_percent": float(100 * np.mean(final_delta <= 2)),
        "center_rectangle_exact_rgb_match_percent": float(100 * np.mean(center_delta == 0)),
        "center_rectangle_normalized_xyxy": [0.2, 0.25, 0.7, 0.8],
        "scope": "Final image also includes HUD/cinematic overlays drawn after the comparison tap; these are descriptive correlation measures, not a full-buffer equality gate."}
    # Only the third pane amplifies the measured difference. Actual A/B pixels are not retouched.
    on, off = images["comparison_dxr_on"], images["comparison_dxr_off"]
    diff = np.abs(on.astype(np.int16) - off.astype(np.int16))
    Image.fromarray(np.minimum(diff * 4, 255).astype(np.uint8)).save(run / "comparison-difference-x4.png")
    width, height = on.shape[1], on.shape[0]
    scale = min(1.0, 640 / width)
    pw, ph = round(width * scale), round(height * scale)
    board = Image.new("RGB", (pw * 3, ph + 72), "#161b22")
    draw = ImageDraw.Draw(board)
    font = ImageFont.load_default()
    for idx, (label, pixels) in enumerate((("SIN PASADAS DXR", off), ("CON DXR", on),
                                          ("DIFERENCIA MEDIDA x4", np.minimum(diff * 4, 255).astype(np.uint8)))):
        draw.text((idx * pw + 12, 12), label, fill="white", font=font)
        board.paste(Image.fromarray(pixels).resize((pw, ph), Image.Resampling.LANCZOS), (idx * pw, 36))
    draw.text((12, ph + 48), f"{result['scenario']} | frame {result['frame']} | GPU pair, same camera and tone | HUD excluded", fill="#cdd3dc", font=font)
    board.save(run / "comparison-summary.png")
    result["summary_preview"] = "comparison-summary.png"
    (run / "comparison-analysis.json").write_text(json.dumps(result, indent=2, allow_nan=False) + "\n")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("run", type=Path)
    args = parser.parse_args()
    try:
        result = analyze(args.run.resolve())
    except (OSError, ValueError, KeyError, StopIteration) as error:
        result = {"status": "FAIL_EVIDENCE_INCOMPLETE_OR_INCONSISTENT", "failures": [str(error)]}
        if args.run.is_dir():
            (args.run / "comparison-analysis.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result["status"].startswith("PASS") else 1)
