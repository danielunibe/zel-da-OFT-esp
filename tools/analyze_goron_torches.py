import os
import json
import numpy as np
from PIL import Image

def analyze_torches():
    run_dir = r"c:\Users\danie\Desktop\Zeldacouch\zel-da-OFT-esp-main (3)\zel-da-OFT-esp-main\reports\scene_harness\20261016_104614_077_GORON_CITY_DARUNIA_TORCHES_NORMAL\captures"
    artifact_dir = r"C:\Users\danie\.gemini\antigravity-ide\brain\bd0b1fe2-ccd1-426f-9454-83ebde8b26d6"
    
    on_path = os.path.join(run_dir, "comparison_navi_point_on_frame352_raw.png")
    off_path = os.path.join(run_dir, "comparison_navi_point_off_frame352_raw.png")
    dxr_off_path = os.path.join(run_dir, "comparison_dxr_off_frame352_raw.png")
    dxr_on_path = os.path.join(run_dir, "comparison_dxr_on_frame352_raw.png")
    
    img_on = Image.open(on_path).convert("RGBA")
    img_off = Image.open(off_path).convert("RGBA")
    img_dxr_off = Image.open(dxr_off_path).convert("RGBA")
    img_dxr_on = Image.open(dxr_on_path).convert("RGBA")
    
    arr_on = np.array(img_on, dtype=np.float32)
    arr_off = np.array(img_off, dtype=np.float32)
    arr_dxr_off = np.array(img_dxr_off, dtype=np.float32)
    arr_dxr_on = np.array(img_dxr_on, dtype=np.float32)
    
    # Analyze Torch ON vs Torch OFF (RGB channels only)
    rgb_on = arr_on[:, :, :3]
    rgb_off = arr_off[:, :, :3]
    
    delta = rgb_on - rgb_off
    abs_delta = np.abs(delta)
    
    # Difference statistics
    total_pixels = rgb_on.shape[0] * rgb_on.shape[1]
    diff_mask = np.any(abs_delta > 0.5, axis=2)
    diff_pixel_count = int(np.sum(diff_mask))
    diff_pixel_pct = (diff_pixel_count / total_pixels) * 100.0
    
    mae_rgb = np.mean(abs_delta)
    mae_r = np.mean(abs_delta[:, :, 0])
    mae_g = np.mean(abs_delta[:, :, 1])
    mae_b = np.mean(abs_delta[:, :, 2])
    
    max_delta_r = np.max(abs_delta[:, :, 0])
    max_delta_g = np.max(abs_delta[:, :, 1])
    max_delta_b = np.max(abs_delta[:, :, 2])
    max_delta_total = np.max(abs_delta)
    
    # Verify chromaticity: torch is amber/warm fire -> R >= G >> B
    # Non-zero delta pixels:
    active_deltas = delta[diff_mask]
    mean_active_r = float(np.mean(active_deltas[:, 0])) if len(active_deltas) > 0 else 0.0
    mean_active_g = float(np.mean(active_deltas[:, 1])) if len(active_deltas) > 0 else 0.0
    mean_active_b = float(np.mean(active_deltas[:, 2])) if len(active_deltas) > 0 else 0.0
    
    # Raw float16 direct point buffer analysis
    rgba16_path = os.path.join(run_dir, "navi_point_direct_frame000352.rgba16f")
    rgba16_stats = {}
    if os.path.exists(rgba16_path):
        raw_bytes = open(rgba16_path, "rb").read()
        f16_arr = np.frombuffer(raw_bytes, dtype=np.float16).reshape((480, 640, 4))
        f16_rgb = f16_arr[:, :, :3].astype(np.float32)
        f16_active = f16_rgb[np.any(f16_rgb > 0.0001, axis=2)]
        rgba16_stats = {
            "min_r": float(np.min(f16_rgb[:, :, 0])),
            "max_r": float(np.max(f16_rgb[:, :, 0])),
            "mean_r": float(np.mean(f16_rgb[:, :, 0])),
            "min_g": float(np.min(f16_rgb[:, :, 1])),
            "max_g": float(np.max(f16_rgb[:, :, 1])),
            "mean_g": float(np.mean(f16_rgb[:, :, 1])),
            "min_b": float(np.min(f16_rgb[:, :, 2])),
            "max_b": float(np.max(f16_rgb[:, :, 2])),
            "mean_b": float(np.mean(f16_rgb[:, :, 2])),
            "active_pixel_count": int(np.sum(np.any(f16_rgb > 0.0001, axis=2))),
            "strictly_zero_pixels": int(np.sum(np.all(f16_rgb == 0.0, axis=2)))
        }
    
    results = {
        "scenario": "GORON_CITY_DARUNIA_TORCHES",
        "frame": 352,
        "total_pixels": total_pixels,
        "torch_causal_diff_pixels": diff_pixel_count,
        "torch_causal_diff_pct": diff_pixel_pct,
        "mae_rgb": float(mae_rgb),
        "mae_r": float(mae_r),
        "mae_g": float(mae_g),
        "mae_b": float(mae_b),
        "max_delta_r": float(max_delta_r),
        "max_delta_g": float(max_delta_g),
        "max_delta_b": float(max_delta_b),
        "max_delta_total": float(max_delta_total),
        "mean_active_r": mean_active_r,
        "mean_active_g": mean_active_g,
        "mean_active_b": mean_active_b,
        "chromatic_ratio_g_to_r": float(mean_active_g / mean_active_r) if mean_active_r > 0 else 0,
        "chromatic_ratio_b_to_r": float(mean_active_b / mean_active_r) if mean_active_r > 0 else 0,
        "f16_direct_buffer": rgba16_stats
    }
    
    print(json.dumps(results, indent=2))
    
    # Save delta visualization images
    # 1. Delta x4 amplified
    delta_x4 = np.clip(abs_delta * 4.0, 0, 255).astype(np.uint8)
    img_delta_x4 = Image.fromarray(delta_x4, mode="RGB")
    img_delta_x4.save(os.path.join(artifact_dir, "goron_torch_diff_x4.png"))
    
    # 2. Side-by-side A/B comparison: Torch OFF vs Torch ON
    w, h = 640, 480
    ab_img = Image.new("RGB", (w * 2, h))
    ab_img.paste(img_off.convert("RGB"), (0, 0))
    ab_img.paste(img_on.convert("RGB"), (w, 0))
    ab_img.save(os.path.join(artifact_dir, "goron_torch_ab_off_vs_on.png"))
    
    # 3. Triple comparison: Torch OFF vs Torch ON vs Delta x4
    triptych = Image.new("RGB", (w * 3, h))
    triptych.paste(img_off.convert("RGB"), (0, 0))
    triptych.paste(img_on.convert("RGB"), (w, 0))
    triptych.paste(img_delta_x4, (w * 2, 0))
    triptych.save(os.path.join(artifact_dir, "goron_torch_triptych.png"))
    
    # 4. Quad comparison: DXR OFF, DXR ON, Torch OFF, Torch ON
    quad = Image.new("RGB", (w * 2, h * 2))
    quad.paste(img_dxr_off.convert("RGB"), (0, 0))
    quad.paste(img_dxr_on.convert("RGB"), (w, 0))
    quad.paste(img_off.convert("RGB"), (0, h))
    quad.paste(img_on.convert("RGB"), (w, h))
    quad.save(os.path.join(artifact_dir, "goron_torch_quad_matrix.png"))
    
    # 5. Save report data
    report_json_path = os.path.join(r"c:\Users\danie\Desktop\Zeldacouch\zel-da-OFT-esp-main (3)\zel-da-OFT-esp-main\reports", "OCZ_RTX_18_GORON_TORCH_ANALYSIS.json")
    with open(report_json_path, "w") as f:
        json.dump(results, f, indent=2)
    print("Report saved to", report_json_path)

if __name__ == "__main__":
    analyze_torches()
