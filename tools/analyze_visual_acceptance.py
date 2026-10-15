"""Measure GPU composition controls and correlate a held Present with the desktop.

Controls are actual render targets produced on the same GPU command list.
This tool never substitutes a generated image for a game capture.
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def difference(a, b, threshold=1e-5):
    delta = a[..., :3] - b[..., :3]
    magnitude = np.max(np.abs(delta), axis=2)
    return {
        "changed_pixels": int(np.count_nonzero(magnitude > threshold)),
        "total_pixels": int(magnitude.size),
        "mean_absolute_rgb_delta": float(np.mean(np.abs(delta))),
        "max_absolute_rgb_delta": float(np.max(magnitude)),
        "brightened_pixels": int(np.count_nonzero(np.max(delta, axis=2) > 0.001)),
        "darkened_pixels": int(np.count_nonzero(np.min(delta, axis=2) < -0.001)),
    }


def analyze(run, expect_shadows, expect_optics):
    failures = []
    arrays, metadata = {}, {}
    stages = ("raster_hdr", "raster_scene_color_copy", "composite_pre_tonemap",
              "surface_optics_off", "indirect_off", "dxr_reflection", "dxr_indirect")
    for name in stages:
        meta = read_json(run / "captures" / f"{name}.meta.json")
        if meta["format"] != "R16G16B16A16_FLOAT":
            raise ValueError(f"Unexpected format for {name}: {meta['format']}")
        data = np.fromfile(run / "captures" / meta["payload"], dtype="<f2")
        expected = meta["width"] * meta["height"] * 4
        if data.size != expected:
            raise ValueError(f"Incomplete payload: {name}")
        arrays[name] = data.reshape(meta["height"], meta["width"], 4).astype(np.float32)
        metadata[name] = meta
        if not np.all(np.isfinite(arrays[name])):
            failures.append(f"NONFINITE:{name}")
    first = metadata["raster_hdr"]
    same_frame = all(m["frame"] == first["frame"] and m["request"] == first["request"]
                     for m in metadata.values())
    if not same_frame:
        failures.append("CONTROL_FRAME_OR_REQUEST_MISMATCH")
    image_stages = stages[:5]
    if any(arrays[name].shape != arrays["raster_hdr"].shape for name in image_stages):
        raise ValueError("Control dimensions differ; comparing or broadcasting them would invalidate attribution.")
    shader = (run / "harness-backend.log").read_text(errors="replace")
    marker = f"[COUCH-DXR-SURFACE][ABLATION] frame={first['frame']} sameScene=1 sameSignals=1 sameCommandList=1"
    if marker not in shader:
        failures.append("MISSING_SAME_COMMAND_LIST_CONTROL_MARKER")
    if "[COUCH-D3D12][DEVICE_REMOVED]" in shader or "DEVICE_HUNG" in shader:
        failures.append("GPU_DEVICE_FAILURE")

    shadows = difference(arrays["raster_scene_color_copy"], arrays["raster_hdr"])
    optics = difference(arrays["composite_pre_tonemap"], arrays["surface_optics_off"])
    indirect = difference(arrays["composite_pre_tonemap"], arrays["indirect_off"])
    if shadows["brightened_pixels"]:
        failures.append("SHADOW_COMPOSITE_BRIGHTENED_RASTER")
    if expect_shadows and not shadows["changed_pixels"]:
        failures.append("NO_MEASURABLE_SHADOW_IN_EXPECTED_VIEW")
    if expect_optics and not optics["changed_pixels"]:
        failures.append("NO_MEASURABLE_SURFACE_OPTICS_IN_EXPECTED_VIEW")
    if indirect["darkened_pixels"]:
        failures.append("INDIRECT_ADDITION_REMOVED_ENERGY")

    launcher = read_json(run / "launcher.status.json")
    verification = read_json(run / "verification.status.json")
    if not launcher["originalSaveUnchanged"] or not launcher["processExited"] or launcher["backendDeviceRemoved"]:
        failures.append("LAUNCHER_INTEGRITY_OR_CLEANUP")
    if verification["failures"]:
        failures.append("HARNESS_CAPTURE_VERIFICATION")
    if not verification["sameFrame"] or verification["captureStatus"] != "CAPTURE_COMPLETE":
        failures.append("REQUIRED_CAPTURE_SET_INCOMPLETE")
    final_meta = read_json(run / "captures" / "final_sdr_pre_present.meta.json")
    if final_meta["frame"] != first["frame"] or final_meta["request"] != first["request"]:
        failures.append("FINAL_SDR_FRAME_OR_REQUEST_MISMATCH")
    final_path = run / "captures" / final_meta["payload"]
    final_image = np.asarray(Image.open(final_path).convert("RGB"))
    if list(final_image.shape[:2]) != [final_meta["height"], final_meta["width"]]:
        failures.append("FINAL_SDR_PAYLOAD_DIMENSIONS_MISMATCH")

    desktop = {"status": "NOT_REQUESTED", "physical_panel_measurement": "NOT_AVAILABLE"}
    monitor_path = run / "monitor.capture.json"
    if monitor_path.is_file():
        proof = read_json(monitor_path)
        if proof["status"] != "CAPTURED_WINDOWS_DESKTOP_CLIENT":
            failures.append("DESKTOP_CAPTURE_FAILED")
            desktop.update(status="FAIL", evidence=proof)
        else:
            desktop_image = np.asarray(Image.open(run / proof["png"]).convert("RGB"))
            if desktop_image.shape != final_image.shape:
                failures.append("DESKTOP_CLIENT_DIMENSIONS_MISMATCH")
                desktop.update(status="FAIL_DIMENSIONS", desktop_shape=list(desktop_image.shape),
                               expected_shape=list(final_image.shape))
            else:
                d = np.abs(desktop_image.astype(np.int16) - final_image.astype(np.int16))
                mae = float(np.mean(d))
                matching = float(100 * np.mean(np.max(d, axis=2) <= 2))
                accepted = (proof["frame"] == first["frame"] and proof["present"] == "ACCEPTED_BY_DXGI"
                            and proof["windowVisible"] and proof["foregroundBefore"] and proof["foregroundAfter"]
                            and proof["dwmFlushHresult"] == 0
                            and proof.get("coordinateSpace") == "PHYSICAL_DESKTOP_PIXELS_BOTH_CORNERS_CONVERTED_IN_ONE_NATIVE_CALL"
                            and mae <= 1.0 and matching >= 99.0)
                desktop.update(status="PASS_SAME_FRAME_WINDOWS_DESKTOP" if accepted else "FAIL_CORRELATION",
                               mean_absolute_rgb_byte_delta=mae, pixels_within_two_codes_percent=matching,
                               frame=proof["frame"], source=proof["source"], present=proof["present"])
                if not accepted:
                    failures.append("DESKTOP_FRAME_CORRELATION_FAILED")
    result = {
        "schema_version": 1,
        "status": "PASS_SCOPED_VISUAL_APPLICATION" if not failures else "FAIL",
        "run_root": str(run), "scenario": verification["scenarioId"],
        "frame": first["frame"], "width": first["width"], "height": first["height"],
        "same_frame_gpu_controls": same_frame, "all_signals_finite": not any(f.startswith("NONFINITE") for f in failures),
        "shadows": shadows, "surface_optics": optics, "indirect": indirect,
        "optics_control_scope": "SPECULAR_REFLECTION_AND_WATER_TRANSMISSION_COMPOSITION; EMISSION_PRESERVED",
        "desktop": desktop,
        "capture_sha256": hashlib.sha256(final_path.read_bytes()).hexdigest(),
        "human_physical_panel_inspection": "NOT_AVAILABLE_TO_AUTOMATION",
        "whole_game_remaster_art_acceptance": "NOT_ESTABLISHED_BY_THIS_SCENE_TEST",
        "original_saves_unchanged": launcher["originalSaveUnchanged"],
        "test_process_stopped": launcher["processExited"], "failures": failures,
    }
    (run / "visual-acceptance.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("run", type=Path)
    parser.add_argument("--expect-shadows", action="store_true")
    parser.add_argument("--expect-optics", action="store_true")
    args = parser.parse_args()
    try:
        result = analyze(args.run, args.expect_shadows, args.expect_optics)
    except (OSError, ValueError, KeyError) as error:
        result = {
            "schema_version": 1,
            "status": "FAIL_EVIDENCE_INCOMPLETE_OR_INCONSISTENT",
            "run_root": str(args.run),
            "failures": [str(error)],
            "visual_quality": "NOT_CERTIFIED",
        }
        if args.run.is_dir():
            (args.run / "visual-acceptance.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result["status"].startswith("PASS") else 1)
