import os, sys, glob, json
import cv2
import numpy as np

WORKSPACE = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SCENE_HARNESS = os.path.join(WORKSPACE, "reports", "scene_harness")
OUTPUT_DIR = os.path.join(WORKSPACE, "reports", "nightly", "comparisons")
os.makedirs(OUTPUT_DIR, exist_ok=True)

SCENARIOS = [
    "TEMPLE_INTERIOR",
    "NAVI_TEMPLE_REFLECTIVE_FLOOR",
    "BASELINE_KOKIRI",
    "NAVI_LAKE_WATER_FEET",
    "NAVI_LAKE_NIGHT_WATER_FEET",
    "HYRULE_FIELD_DAY",
    "HYRULE_FIELD_NIGHT"
]

def find_runs(scenario):
    pattern = os.path.join(SCENE_HARNESS, f"*_{scenario}_NORMAL")
    matches = sorted(glob.glob(pattern))
    return matches

metrics_summary = {}

for sc in SCENARIOS:
    runs = find_runs(sc)
    if not runs:
        print(f"No runs found for {sc}")
        continue
    
    # Latest run (from nightly 14)
    latest_run = runs[-1]
    # Prior run (baseline from RTX 12/12B) if available
    prior_run = runs[-2] if len(runs) >= 2 else None
    
    captures_latest = os.path.join(latest_run, "captures")
    on_img_path = os.path.join(captures_latest, "comparison_dxr_on_frame425_raw.png")
    off_img_path = os.path.join(captures_latest, "comparison_dxr_off_frame425_raw.png")
    diff_img_path = os.path.join(captures_latest, "comparison_dxr_off_on_difference_x4.png")
    
    sc_metric = {
        "latest_run": os.path.basename(latest_run),
        "has_captures": os.path.exists(on_img_path)
    }
    
    if os.path.exists(on_img_path) and os.path.exists(off_img_path):
        img_on = cv2.imread(on_img_path)
        img_off = cv2.imread(off_img_path)
        
        diff = np.abs(img_on.astype(float) - img_off.astype(float))
        mae = np.mean(diff)
        diff_pixels = int(np.sum(np.any(diff > 0, axis=-1)))
        sc_metric["dxr_on_vs_off_mae"] = float(mae)
        sc_metric["dxr_on_vs_off_diff_pixels"] = diff_pixels
        sc_metric["dxr_on_vs_off_diff_percent"] = float(diff_pixels / (img_on.shape[0] * img_on.shape[1]) * 100)
        
        # Create Side-by-Side comparison image
        sbs = np.hstack([img_off, img_on])
        # Annotate
        cv2.putText(sbs, f"{sc} - DXR OFF (Raster Baseline)", (20, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 255), 2)
        cv2.putText(sbs, f"{sc} - DXR ON (Nightly 14 Candidate)", (img_on.shape[1] + 20, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
        cv2.imwrite(os.path.join(OUTPUT_DIR, f"{sc.lower()}_ab_dxr_off_on.png"), sbs)
        
        if prior_run:
            captures_prior = os.path.join(prior_run, "captures")
            prior_on_path = os.path.join(captures_prior, "comparison_dxr_on_frame425_raw.png")
            if os.path.exists(prior_on_path):
                img_prior = cv2.imread(prior_on_path)
                diff_prior = np.abs(img_on.astype(float) - img_prior.astype(float))
                mae_prior = np.mean(diff_prior)
                diff_prior_pixels = int(np.sum(np.any(diff_prior > 0, axis=-1)))
                sc_metric["prior_run"] = os.path.basename(prior_run)
                sc_metric["prior_vs_nightly_mae"] = float(mae_prior)
                sc_metric["prior_vs_nightly_diff_pixels"] = diff_prior_pixels
                sc_metric["prior_vs_nightly_diff_percent"] = float(diff_prior_pixels / (img_on.shape[0] * img_on.shape[1]) * 100)
                
                # Evolution Side-by-Side
                evo_sbs = np.hstack([img_prior, img_on])
                cv2.putText(evo_sbs, f"{sc} - Prior RTX Build", (20, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 200, 255), 2)
                cv2.putText(evo_sbs, f"{sc} - Nightly 14 Validated", (img_on.shape[1] + 20, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
                cv2.imwrite(os.path.join(OUTPUT_DIR, f"{sc.lower()}_evolution_prior_vs_nightly14.png"), evo_sbs)
                
                # Difference heatmap (x4 amplified)
                diff_amplified = np.clip(diff_prior * 4.0, 0, 255).astype(np.uint8)
                cv2.imwrite(os.path.join(OUTPUT_DIR, f"{sc.lower()}_diff_map_x4.png"), diff_amplified)
    
    metrics_summary[sc] = sc_metric

out_json = os.path.join(WORKSPACE, "reports", "nightly", "OCZ_NIGHTLY_14_METRICS.json")
with open(out_json, "w", encoding="utf-8") as f:
    json.dump(metrics_summary, f, indent=2)

print("Comparison generator finished successfully.")
