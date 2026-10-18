import os, sys, glob, json
import cv2
import numpy as np

WORKSPACE = r"c:\Users\danie\Desktop\Zeldacouch\zel-da-OFT-esp-main (3)\zel-da-OFT-esp-main"
SCENE_HARNESS = os.path.join(WORKSPACE, "reports", "scene_harness")
OUTPUT_DIR = os.path.join(WORKSPACE, "reports", "nightly", "audited_comparisons")
os.makedirs(OUTPUT_DIR, exist_ok=True)

NIGHTLY14_RUNS = {
    "TEMPLE_INTERIOR": "20261016_001257_084_TEMPLE_INTERIOR_NORMAL",
    "NAVI_TEMPLE_REFLECTIVE_FLOOR": "20261016_001501_276_NAVI_TEMPLE_REFLECTIVE_FLOOR_NORMAL",
    "BASELINE_KOKIRI": "20261016_001603_959_BASELINE_KOKIRI_NORMAL",
    "NAVI_LAKE_WATER_FEET": "20261016_001719_284_NAVI_LAKE_WATER_FEET_NORMAL",
    "NAVI_LAKE_NIGHT_WATER_FEET": "20261016_001834_097_NAVI_LAKE_NIGHT_WATER_FEET_NORMAL",
    "HYRULE_FIELD_DAY": "20261016_001942_642_HYRULE_FIELD_DAY_NORMAL",
    "HYRULE_FIELD_NIGHT": "20261016_002103_817_HYRULE_FIELD_NIGHT_NORMAL"
}

RC1_RUNS = {
    "TEMPLE_INTERIOR": "20261016_014351_673_TEMPLE_INTERIOR_NORMAL",
    "NAVI_TEMPLE_REFLECTIVE_FLOOR": "20261016_014629_362_NAVI_TEMPLE_REFLECTIVE_FLOOR_NORMAL",
    "BASELINE_KOKIRI": "20261016_014847_070_BASELINE_KOKIRI_NORMAL",
    "NAVI_LAKE_WATER_FEET": "20261016_015046_971_NAVI_LAKE_WATER_FEET_NORMAL",
    "NAVI_LAKE_NIGHT_WATER_FEET": "20261016_015258_131_NAVI_LAKE_NIGHT_WATER_FEET_NORMAL",
    "HYRULE_FIELD_DAY": "20261016_015459_747_HYRULE_FIELD_DAY_NORMAL",
    "HYRULE_FIELD_NIGHT": "20261016_015657_300_HYRULE_FIELD_NIGHT_NORMAL",
    "NAVI_KOKIRI_TWO_FAIRIES": "20261016_015845_971_NAVI_KOKIRI_TWO_FAIRIES_NORMAL",
    "NAVI_KOKIRI_DISABLED": "20261016_020051_502_NAVI_KOKIRI_DISABLED_NORMAL"
}

def load_capture_pair(run_dir):
    captures_dir = os.path.join(run_dir, "captures")
    # find comparison_dxr_off_*_raw.png and comparison_dxr_on_*_raw.png
    off_files = glob.glob(os.path.join(captures_dir, "comparison_dxr_off_*_raw.png"))
    on_files = glob.glob(os.path.join(captures_dir, "comparison_dxr_on_*_raw.png"))
    if not off_files or not on_files:
        # maybe navi_a_sdr or final_sdr
        off_files = glob.glob(os.path.join(captures_dir, "*off*raw.png"))
        on_files = glob.glob(os.path.join(captures_dir, "*on*raw.png"))
    if not off_files or not on_files:
        return None, None
    off_path = sorted(off_files)[0]
    on_path = sorted(on_files)[0]
    img_off = cv2.imread(off_path, cv2.IMREAD_UNCHANGED)
    img_on = cv2.imread(on_path, cv2.IMREAD_UNCHANGED)
    return img_off, img_on

results = {}

print("=== STARTING COMPREHENSIVE AUDIT OF NIGHTLY 14 VS RC1 ===")

for sc, rc1_id in RC1_RUNS.items():
    rc1_dir = os.path.join(SCENE_HARNESS, rc1_id)
    rc1_off, rc1_on = load_capture_pair(rc1_dir)
    
    n14_id = NIGHTLY14_RUNS.get(sc)
    n14_dir = os.path.join(SCENE_HARNESS, n14_id) if n14_id else None
    n14_off, n14_on = load_capture_pair(n14_dir) if n14_dir else (None, None)
    
    sc_data = {
        "rc1_run_id": rc1_id,
        "n14_run_id": n14_id
    }
    
    # 1. Analyze RC1 DXR OFF vs ON
    if rc1_off is not None and rc1_on is not None:
        # RGB diff
        diff_rgb = np.abs(rc1_on[:,:,:3].astype(float) - rc1_off[:,:,:3].astype(float))
        mae_rgb = np.mean(diff_rgb)
        diff_px_rgb = int(np.sum(np.any(diff_rgb > 0, axis=-1)))
        
        # RGBA diff
        diff_rgba = np.abs(rc1_on.astype(float) - rc1_off.astype(float))
        mae_rgba = np.mean(diff_rgba)
        diff_px_rgba = int(np.sum(np.any(diff_rgba > 0, axis=-1)))
        
        # Alpha diff
        diff_a = np.abs(rc1_on[:,:,3].astype(float) - rc1_off[:,:,3].astype(float))
        mae_a = np.mean(diff_a)
        
        total_px = rc1_on.shape[0] * rc1_on.shape[1]
        sc_data["rc1_dxr_off_vs_on"] = {
            "rgb_mae": float(mae_rgb),
            "rgba_mae": float(mae_rgba),
            "alpha_mae": float(mae_a),
            "diff_pixels_rgb": diff_px_rgb,
            "diff_percent_rgb": float(diff_px_rgb / total_px * 100),
            "diff_pixels_rgba": diff_px_rgba,
            "diff_percent_rgba": float(diff_px_rgba / total_px * 100)
        }
    
    # 2. Analyze Nightly 14 DXR OFF vs ON
    if n14_off is not None and n14_on is not None:
        diff_rgb = np.abs(n14_on[:,:,:3].astype(float) - n14_off[:,:,:3].astype(float))
        mae_rgb = np.mean(diff_rgb)
        diff_px_rgb = int(np.sum(np.any(diff_rgb > 0, axis=-1)))
        
        diff_rgba = np.abs(n14_on.astype(float) - n14_off.astype(float))
        mae_rgba = np.mean(diff_rgba)
        diff_px_rgba = int(np.sum(np.any(diff_rgba > 0, axis=-1)))
        
        diff_a = np.abs(n14_on[:,:,3].astype(float) - n14_off[:,:,3].astype(float))
        mae_a = np.mean(diff_a)
        
        total_px = n14_on.shape[0] * n14_on.shape[1]
        sc_data["n14_dxr_off_vs_on"] = {
            "rgb_mae": float(mae_rgb),
            "rgba_mae": float(mae_rgba),
            "alpha_mae": float(mae_a),
            "diff_pixels_rgb": diff_px_rgb,
            "diff_percent_rgb": float(diff_px_rgb / total_px * 100),
            "diff_pixels_rgba": diff_px_rgba,
            "diff_percent_rgba": float(diff_px_rgba / total_px * 100)
        }
    
    # 3. Direct comparison: Nightly 14 DXR ON vs RC1 DXR ON
    if n14_on is not None and rc1_on is not None:
        diff_cross = np.abs(rc1_on[:,:,:3].astype(float) - n14_on[:,:,:3].astype(float))
        mae_cross = np.mean(diff_cross)
        diff_px_cross = int(np.sum(np.any(diff_cross > 0, axis=-1)))
        total_px = rc1_on.shape[0] * rc1_on.shape[1]
        
        sc_data["n14_vs_rc1_direct_dxr_on"] = {
            "cross_build_rgb_mae": float(mae_cross),
            "cross_build_diff_pixels": diff_px_cross,
            "cross_build_diff_percent": float(diff_px_cross / total_px * 100)
        }
        print(f"[{sc}] N14 vs RC1 Direct RGB MAE: {mae_cross:.6f} (Diff pixels: {diff_px_cross})")
        if "rc1_dxr_off_vs_on" in sc_data and "n14_dxr_off_vs_on" in sc_data:
            print(f"      RC1 RGB MAE: {sc_data['rc1_dxr_off_vs_on']['rgb_mae']:.4f} | N14 RGB MAE: {sc_data['n14_dxr_off_vs_on']['rgb_mae']:.4f}")
            print(f"      RC1 RGBA MAE: {sc_data['rc1_dxr_off_vs_on']['rgba_mae']:.4f} | N14 RGBA MAE: {sc_data['n14_dxr_off_vs_on']['rgba_mae']:.4f}")
            print(f"      RC1 Alpha MAE: {sc_data['rc1_dxr_off_vs_on']['alpha_mae']:.4f} | N14 Alpha MAE: {sc_data['n14_dxr_off_vs_on']['alpha_mae']:.4f}")
    
    results[sc] = sc_data

print("\n=== ISOLATING LAKE HYLIA MASKS (WATER, STONE, SKY, LINK, NAVI, COLUMN) ===")

def analyze_lake_masks(scenario_name, rc1_id):
    rc1_dir = os.path.join(SCENE_HARNESS, rc1_id)
    off_img, on_img = load_capture_pair(rc1_dir)
    h, w = on_img.shape[:2]
    
    # Let's segment based on color, position, and luminance characteristics of Lake Hylia camera:
    # Camera view: Link standing in shallow water near shore looking towards the water / columns / sky.
    # Navi is a bright electric blue spot around link.
    # Link is in the center-foreground.
    # Water is the reflective lower/mid region.
    # Sky is the upper region.
    # Stone/bank is shore rock and islands.
    
    b, g, r = on_img[:,:,0], on_img[:,:,1], on_img[:,:,2]
    
    # 1. Sky mask: upper third (y < h*0.35) with sky color
    sky_mask = np.zeros((h, w), dtype=bool)
    sky_mask[:int(h*0.32), :] = True
    
    # 2. Navi mask: bright cyan/blue highlight (high B, high G, concentrated spot)
    navi_mask = (b > 180) & (g > 150) & (r < 220) & (np.arange(h)[:, None] > int(h*0.3)) & (np.arange(h)[:, None] < int(h*0.7)) & (np.arange(w)[None, :] > int(w*0.3)) & (np.arange(w)[None, :] < int(w*0.7))
    # Dilate navi to capture halo
    navi_mask = cv2.dilate(navi_mask.astype(np.uint8), np.ones((15, 15), np.uint8)).astype(bool)
    
    # 3. Link mask: center character silhouette
    link_mask = (np.arange(h)[:, None] > int(h*0.45)) & (np.arange(h)[:, None] < int(h*0.85)) & (np.arange(w)[None, :] > int(w*0.42)) & (np.arange(w)[None, :] < int(w*0.58)) & (~navi_mask)
    
    # 4. Water mask: lower and mid water surface (excluding Link and immediate bank)
    # Lake Hylia water has distinct green/teal/blue water texture or transparency in off_img
    alpha_channel = off_img[:,:,3]
    water_mask = (alpha_channel < 250) & (~sky_mask) & (~link_mask) & (~navi_mask)
    
    # 5. Columns: tall vertical structures in the distance
    col_mask = (np.arange(h)[:, None] >= int(h*0.25)) & (np.arange(h)[:, None] <= int(h*0.6)) & (
        ((np.arange(w)[None, :] > int(w*0.12)) & (np.arange(w)[None, :] < int(w*0.22))) |
        ((np.arange(w)[None, :] > int(w*0.78)) & (np.arange(w)[None, :] < int(w*0.88)))
    ) & (~sky_mask) & (~water_mask)
    
    # 6. Stone/Bank mask: shoreline, islands, background hills (not water, not sky, not link, not navi, not columns)
    stone_mask = (~sky_mask) & (~water_mask) & (~link_mask) & (~navi_mask) & (~col_mask)
    
    # 7. Non-water overall
    non_water_mask = (~water_mask)
    
    masks = {
        "water": water_mask,
        "stone_shore": stone_mask,
        "sky": sky_mask,
        "link": link_mask,
        "navi": navi_mask,
        "columns": col_mask,
        "non_water_surfaces": non_water_mask
    }
    
    mask_metrics = {}
    diff_rgb = np.abs(on_img[:,:,:3].astype(float) - off_img[:,:,:3].astype(float))
    diff_rgba = np.abs(on_img.astype(float) - off_img.astype(float))
    diff_a = np.abs(on_img[:,:,3].astype(float) - off_img[:,:,3].astype(float))
    
    for m_name, m_bool in masks.items():
        px_count = int(np.sum(m_bool))
        if px_count == 0:
            continue
        m_rgb_mae = float(np.mean(diff_rgb[m_bool]))
        m_rgba_mae = float(np.mean(diff_rgba[m_bool]))
        m_alpha_mae = float(np.mean(diff_a[m_bool]))
        m_diff_px = int(np.sum(np.any(diff_rgb[m_bool] > 0, axis=-1)))
        
        mask_metrics[m_name] = {
            "pixel_count": px_count,
            "percent_of_frame": float(px_count / (h*w) * 100),
            "rgb_mae": m_rgb_mae,
            "rgba_mae": m_rgba_mae,
            "alpha_mae": m_alpha_mae,
            "diff_pixels": m_diff_px,
            "diff_percent_in_mask": float(m_diff_px / px_count * 100)
        }
        print(f"  [{scenario_name}] Mask {m_name.upper():<18}: Pixels={px_count:<6} ({px_count/(h*w)*100:4.1f}%) | RGB MAE={m_rgb_mae:6.3f} | Alpha MAE={m_alpha_mae:6.3f} | RGBA MAE={m_rgba_mae:6.3f}")
    
    return mask_metrics

results["LAKE_HYLIA_DAY_MASKS"] = analyze_lake_masks("NAVI_LAKE_WATER_FEET", RC1_RUNS["NAVI_LAKE_WATER_FEET"])
results["LAKE_HYLIA_NIGHT_MASKS"] = analyze_lake_masks("NAVI_LAKE_NIGHT_WATER_FEET", RC1_RUNS["NAVI_LAKE_NIGHT_WATER_FEET"])

out_json = os.path.join(WORKSPACE, "reports", "nightly", "OCZ_RTX_16_AUDITED_METRICS.json")
with open(out_json, "w", encoding="utf-8") as f:
    json.dump(results, f, indent=2)

print(f"\nSaved comprehensive audited metrics to: {out_json}")
