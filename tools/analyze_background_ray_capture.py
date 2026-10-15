"""Measure real pre-tonemap DXR diagnostics without opening a window."""
import argparse
import json
import re
from pathlib import Path

import numpy as np


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def analyze(root):
    evidence = read_json(root / "evidence.json")
    launcher = read_json(root / "launcher.status.json")
    launch_manifest = read_json(root / "manifest.launcher.json")
    meta = read_json(root / "captures" / "composite_pre_tonemap.meta.json")
    request = meta["request"]
    registry = read_json(root / "manifest.input.json")
    scenario = next((item for item in registry["scenarios"] if item["id"] == request["scenario_id"]), None)
    mapped = scenario is not None and scenario.get("status") == "MAPPED" and any(
        item["id"] == scenario.get("mappingEvidenceId") and item["entrance"] == scenario["entrance"]
        and item["scene"] == scenario["expectedScene"] and item["room"] == scenario["expectedRoom"]
        for item in registry["verifiedMappings"]
    )
    scene_matches = mapped and (
        int(request["scene"], 16) == scenario["expectedScene"]
        and int(request["room"]) == scenario["expectedRoom"]
        and request["view_id"] == scenario["viewId"]
        and launch_manifest["scenarioId"] == request["scenario_id"]
        and launch_manifest["truthMode"] == request["truth_mode"]
        and launch_manifest["scene"] == request["scene"]
        and launch_manifest["room"] == request["room"]
        and evidence["scenario_id"] == request["scenario_id"]
        and evidence["scene"] == request["scene"] and evidence["room"] == request["room"]
    )
    payload = root / "captures" / meta["payload"]
    width, height = int(meta["width"]), int(meta["height"])
    values = np.fromfile(payload, dtype="<f2")
    if values.size != width * height * 4:
        raise ValueError("GPU payload size differs from capture metadata")
    rgba = values.reshape(height, width, 4).astype(np.float32)
    rgb = rgba[..., :3]
    finite = np.isfinite(rgba).all(axis=-1)
    total = width * height

    def color_mask(color):
        return finite & (np.abs(rgb - np.asarray(color, dtype=np.float32)).max(axis=-1) < 0.005)

    mode = request["truth_mode"]
    result = {
        "schema_version": 1,
        "scope": "GPU_PRIMARY_RAYS_IN_REQUESTED_SCENARIO_VIEW_ONLY",
        "scenario": request["scenario_id"],
        "view": request["view_id"],
        "verified_scene_room_view_match": bool(scene_matches),
        "run_root": str(root),
        "mode": mode,
        "scene": request["scene"],
        "room": request["room"],
        "frame": meta["frame"],
        "width": width,
        "height": height,
        "total_pixels": total,
        "non_finite_pixels": int((~finite).sum()),
        "launcher_status": launcher["status"],
        "same_dxr_frame": evidence.get("dxr_frame", {}).get("request_matches_capture", False),
        "original_saves_unchanged": launcher.get("originalSaveUnchanged", False),
        "test_process_stopped": launcher.get("processExited", False),
        "presentation": "NOT_RUN_BACKGROUND_TEST",
        "visual_quality": "NOT_CERTIFIED",
    }
    if launch_manifest.get("primaryGateCounters", False):
        gate_meta = read_json(root / "captures" / "dxr_reflection.meta.json")
        gate_values = np.fromfile(root / "captures" / gate_meta["payload"], dtype="<f2")
        if gate_values.size != width * height * 4 or gate_meta["frame"] != meta["frame"]:
            raise ValueError("Gate payload does not match the composite frame or dimensions")
        gate = gate_values.reshape(height, width, 4).astype(np.float32)
        if not np.isfinite(gate).all():
            raise ValueError("Non-finite primary gate payload")
        flags = np.rint(gate[..., 0] * 255).astype(np.uint32)
        errors = np.rint(gate[..., 1] * 255).astype(np.uint32)
        background = (flags == 1) & (errors == 10)
        hit = (flags & 2) != 0
        good_material = ((flags & 8) != 0) & (errors == 0)
        depth_mismatch = errors == 11
        written = ((flags & 64) != 0) & (errors == 0)
        geometry_pixels = int((~background).sum())
        log = (root / "harness-backend.log").read_text(encoding="utf-8", errors="replace")
        blocks = re.split(r"(?=\[COUCH-DXR-GATE\] COUNTER_READBACK_FRAME=)", log)
        block = next((part for part in blocks if part.startswith(
            f"[COUCH-DXR-GATE] COUNTER_READBACK_FRAME={meta['frame']} ")), "")
        counter_line = re.search(r"PRIMARY_GATE_COUNTERS mode=NORMAL ([^\r\n]+)", block)
        if counter_line is None:
            raise ValueError("No GPU gate counter readback for the captured frame")
        counters = dict(re.findall(r"(\w+)=(\w+)", counter_line.group(1)))
        for key, value in list(counters.items()):
            if value.isdigit():
                counters[key] = int(value)
        result.update(
            scope="GPU_GATE_DIAGNOSIS_IN_REQUESTED_SCENARIO_VIEW_ONLY",
            diagnostic_any_hit_alpha_bypass=True,
            forced_material_coverage=False,
            raster_geometry_pixels=geometry_pixels,
            raster_background_pixels=int(background.sum()),
            geometric_hit_pixels=int(hit.sum()),
            valid_material_pixels=int(good_material.sum()),
            raster_depth_mismatch_pixels=int(depth_mismatch.sum()),
            surface_written_pixels=int(written.sum()),
            gate_errors={str(code): int((errors == code).sum()) for code in np.unique(errors)},
            gpu_counter_readback=counters,
            material_hit_percent_of_raster_geometry=round(100 * float(good_material.sum()) / max(geometry_pixels, 1), 6),
            denominator_note="Gate view bypasses AnyHit alpha; geometric counts are not normal-path coverage.",
        )
        observed = (
            counters.get("invariants") == "PASS"
            and counters.get("hit") == int(hit.sum())
            and counters.get("materialValid") == int(good_material.sum())
            and counters.get("surfaceWritten") == int(written.sum())
        )
    elif mode == "HIT_MISS":
        green = color_mask([0, 1, 0])
        raw_meta = read_json(root / "captures" / "dxr_reflection.meta.json")
        raw_values = np.fromfile(root / "captures" / raw_meta["payload"], dtype="<f2")
        if raw_values.size != width * height * 4 or raw_meta["frame"] != meta["frame"]:
            raise ValueError("Truth payload does not match the composite frame or dimensions")
        raw = raw_values.reshape(height, width, 4).astype(np.float32)
        # HIT_MISS uses the actual closest-hit roughness encoding (debug 4).
        # Cleared/aborted raygen pixels have alpha 0 and are not hit evidence,
        # even if an older composite mislabeled their zero RGB as green.
        hit_payload = (
            np.isfinite(raw).all(axis=-1) & (raw[..., 3] > 0.99)
            & (np.ptp(raw[..., :3], axis=-1) < 0.005)
            & (raw[..., 0] >= 0.039) & (raw[..., 0] <= 1.001)
        )
        hit = green & hit_payload
        miss = color_mask([1, 0, 0])
        result.update(
            hit_pixels=int(hit.sum()),
            diagnostic_green_pixels=int(green.sum()),
            green_without_closest_hit_payload=int((green & ~hit_payload).sum()),
            hit_percent_of_full_capture=round(100 * float(hit.mean()), 6),
            miss_or_background_pixels=int(miss.sum()),
            other_pixels=int((~(hit | miss)).sum()),
            denominator_note="Miss/red includes the background; this is not geometry-only hit efficiency.",
        )
        observed = result["hit_pixels"] > 100 and result["other_pixels"] == 0 and result["green_without_closest_hit_payload"] == 0
    elif mode == "WORLD_NORMAL":
        miss = color_mask([1, 0, 0])
        invalid = color_mask([0, 0, 1])
        normal = rgb * 2 - 1
        lengths = np.linalg.norm(normal, axis=-1)
        hit = finite & ~miss & ~invalid
        good = hit & (np.abs(lengths - 1) < 0.01)
        result.update(
            valid_normal_pixels=int(good.sum()),
            invalid_normal_pixels=int(invalid.sum()),
            hit_normal_length_errors=int((hit & ~good).sum()),
            max_unit_length_error=float(np.abs(lengths[hit] - 1).max()) if hit.any() else None,
            miss_or_background_pixels=int(miss.sum()),
            normal_space="FAST3D_TLAS_SPACE (legacy diagnostic name WORLD_NORMAL)",
        )
        observed = result["valid_normal_pixels"] > 100 and result["invalid_normal_pixels"] == 0 and result["hit_normal_length_errors"] == 0
    elif mode == "HIT_DISTANCE":
        miss = color_mask([1, 0, 0])
        gray = finite & ~miss & (np.ptp(rgb, axis=-1) < 0.005)
        positive = gray & (rgb[..., 0] > 0)
        result.update(
            positive_hit_distance_pixels=int(positive.sum()),
            grayscale_hit_pixels=int(gray.sum()),
            miss_or_background_pixels=int(miss.sum()),
            other_pixels=int((~(gray | miss)).sum()),
            encoding="saturate(RayTCurrent / 256), primary ray origin at raster near plane",
        )
        observed = result["positive_hit_distance_pixels"] > 100 and result["other_pixels"] == 0
    elif mode in ("SYNTHETIC_LAMBERT", "GAME_LIGHT_LAMBERT"):
        normal_meta = read_json(root / "captures" / "dxr_reflection.meta.json")
        normal_values = np.fromfile(root / "captures" / normal_meta["payload"], dtype="<f2")
        if normal_values.size != width * height * 4 or normal_meta["frame"] != meta["frame"]:
            raise ValueError("Lambert normal payload does not match the composite frame")
        raw = normal_values.reshape(height, width, 4).astype(np.float32)
        miss = color_mask([1, 0, 0])
        hit = finite & ~miss & (raw[..., 3] > 0.99) & (np.ptp(rgb, axis=-1) < 0.005)
        normal = raw[..., :3] * 2 - 1
        lengths = np.linalg.norm(normal, axis=-1)
        normal = normal / np.maximum(lengths[..., None], 1e-9)
        if mode == "SYNTHETIC_LAMBERT":
            light = np.asarray([1, -1, 1], dtype=np.float32)
            light_source = "FIXED_SHADER_LIGHT_IN_TLAS_SPACE"
        else:
            log = (root / "harness-backend.log").read_text(encoding="utf-8", errors="replace")
            match = re.search(
                rf"\[COUCH-DXR-LIGHT-SYNC\] frame={meta['frame']} [^\r\n]*?gpuDir=\(([^)]+)\)", log
            )
            if match is None:
                raise ValueError("No same-frame directional light upload in the GPU log")
            light = np.asarray([float(value) for value in match.group(1).split(",")], dtype=np.float32)
            light_source = "SAME_FRAME_GAME_DIRECTION_CPU_UPLOAD; RESPONSE_MEASURED_FROM_GPU_PIXELS"
        light /= max(float(np.linalg.norm(light)), 1e-9)
        expected = np.clip((normal * light).sum(axis=-1), 0, 1)
        errors = np.abs(rgb[..., 0] - expected)
        good = hit & (errors < 0.0015) & (np.abs(lengths - 1) < 0.01)
        result.update(
            lambert_hit_pixels=int(hit.sum()),
            valid_lambert_response_pixels=int(good.sum()),
            lit_hit_pixels=int((hit & (rgb[..., 0] > 0.01)).sum()),
            dark_hit_pixels=int((hit & (rgb[..., 0] <= 0.01)).sum()),
            lambert_equation_errors=int((hit & ~good).sum()),
            max_lambert_error=float(errors[hit].max()) if hit.any() else None,
            gpu_measured_lambert_min=float(rgb[..., 0][hit].min()) if hit.any() else None,
            gpu_measured_lambert_max=float(rgb[..., 0][hit].max()) if hit.any() else None,
            light_direction=light.tolist(),
            light_source=light_source,
            other_pixels=int((~(hit | miss)).sum()),
            lighting_scope="DIRECT_DIFFUSE_DIAGNOSTIC; REFLECTION_GI_AND_SHADOW_QUALITY_NOT_CERTIFIED",
        )
        observed = result["valid_lambert_response_pixels"] > 100 and result["lambert_equation_errors"] == 0 and result["other_pixels"] == 0
    elif mode == "INSTANCE_IDS":
        miss = color_mask([0, 0, 0])
        hit = finite & ~miss
        colors = np.unique(rgb[hit].astype(np.float16), axis=0)
        result.update(
            instance_hit_pixels=int(hit.sum()),
            distinct_instance_hash_colors=int(len(colors)),
            miss_or_background_pixels=int(miss.sum()),
            identity_note="GPU InstanceID hash, not a complete certification of every Fast3D material combiner.",
        )
        observed = result["instance_hit_pixels"] > 100 and result["distinct_instance_hash_colors"] > 1
    else:
        result.update(
            rgb_min=rgb.min(axis=(0, 1)).tolist(),
            rgb_max=rgb.max(axis=(0, 1)).tolist(),
            rgb_mean=rgb.mean(axis=(0, 1)).tolist(),
        )
        def rgba_capture(name):
            capture_meta = read_json(root / "captures" / (name + ".meta.json"))
            if (capture_meta["frame"], capture_meta["width"], capture_meta["height"], capture_meta["format"]) != (
                meta["frame"], width, height, "R16G16B16A16_FLOAT"
            ):
                raise ValueError("Normal-path captures do not share the same frame, dimensions and format")
            capture_values = np.fromfile(root / "captures" / capture_meta["payload"], dtype="<f2")
            if capture_values.size != width * height * 4:
                raise ValueError("Normal-path GPU payload size differs from metadata")
            return capture_values.reshape(height, width, 4).astype(np.float32)

        scene_copy = rgba_capture("raster_scene_color_copy")
        raster = rgba_capture("raster_hdr")
        reflection = rgba_capture("dxr_reflection")
        indirect = rgba_capture("dxr_indirect")
        surface_delta = np.abs(rgb - scene_copy[..., :3]).max(axis=-1)
        full_delta = np.abs(rgb - raster[..., :3]).max(axis=-1)
        all_signal_finite = all(np.isfinite(values).all() for values in (scene_copy, raster, reflection, indirect))
        result.update(
            captured_signal_buffers_finite=bool(all_signal_finite),
            reflection_nonzero_pixels=int((np.abs(reflection[..., :3]).max(axis=-1) > 1e-4).sum()),
            indirect_nonzero_pixels=int((np.abs(indirect[..., :3]).max(axis=-1) > 1e-4).sum()),
            surface_composite_changed_pixels=int((surface_delta > 1e-4).sum()),
            total_dxr_stage_changed_pixels=int((full_delta > 1e-4).sum()),
            max_surface_composite_rgb_delta=float(surface_delta.max()),
            mean_surface_composite_rgb_delta=float(surface_delta.mean()),
            comparison="SAME_FRAME_GPU_STAGE_READBACKS, BEFORE_AND_AFTER_SURFACE_COMPOSITE",
            controlled_dxr_off_ablation="NOT_RUN",
        )
        observed = result["non_finite_pixels"] == 0 and all_signal_finite and result["surface_composite_changed_pixels"] > 100

    presentations = [json.loads(line) for line in (root / "presentation.evidence.jsonl").read_text().splitlines() if line.strip()]
    result["hidden_gpu_submission_records"] = len(presentations)
    hidden = bool(presentations) and all(
        row.get("dxgi_present_call") == "SKIPPED_BACKGROUND"
        and row.get("window_visible") is False
        and row.get("window_foreground") is False
        for row in presentations
    )
    result["hidden_no_present_verified"] = hidden
    valid_run = (
        hidden and result["same_dxr_frame"] and result["original_saves_unchanged"]
        and result["test_process_stopped"] and result["non_finite_pixels"] == 0
        and scene_matches and int(request["stable_frames"]) >= 30
        and evidence.get("capture_status") == "CAPTURE_COMPLETE"
        and len(evidence.get("captures", {})) == 8
    )
    result["result"] = "PASS_SCOPED_GPU_OBSERVATION" if valid_run and observed else "FAIL_OR_INCOMPLETE"
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("run_root", type=Path)
    args = parser.parse_args()
    result = analyze(args.run_root.resolve())
    output = args.run_root / "primary-ray-analysis.json"
    output.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2, allow_nan=False))
    raise SystemExit(0 if result["result"] == "PASS_SCOPED_GPU_OBSERVATION" else 1)
