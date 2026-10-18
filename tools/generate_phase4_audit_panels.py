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
    "HYRULE_FIELD_DAY": "20261016_001942_642_HYRULE_FIELD_DAY_NORMAL",
    "HYRULE_FIELD_NIGHT": "20261016_002103_817_HYRULE_FIELD_NIGHT_NORMAL"
}

RC1_RUNS = {
    "TEMPLE_INTERIOR": "20261016_014351_673_TEMPLE_INTERIOR_NORMAL",
    "NAVI_TEMPLE_REFLECTIVE_FLOOR": "20261016_014629_362_NAVI_TEMPLE_REFLECTIVE_FLOOR_NORMAL",
    "BASELINE_KOKIRI": "20261016_014847_070_BASELINE_KOKIRI_NORMAL",
    "HYRULE_FIELD_DAY": "20261016_015459_747_HYRULE_FIELD_DAY_NORMAL",
    "HYRULE_FIELD_NIGHT": "20261016_015657_300_HYRULE_FIELD_NIGHT_NORMAL"
}

def load_img(run_dir, pattern):
    files = glob.glob(os.path.join(run_dir, "captures", pattern))
    if not files:
        return None
    return cv2.imread(sorted(files)[0], cv2.IMREAD_UNCHANGED)

def create_tri_panel(title, img_n14, img_rc1, out_name):
    # Left: N14, Middle: RC1, Right: Diff x8
    h, w = img_rc1.shape[:2]
    diff = np.abs(img_rc1[:,:,:3].astype(float) - img_n14[:,:,:3].astype(float))
    diff_x8 = np.clip(diff * 8.0, 0, 255).astype(np.uint8)
    
    panel = np.zeros((h + 50, w * 3, 3), dtype=np.uint8)
    panel[50:, :w] = img_n14[:,:,:3]
    panel[50:, w:2*w] = img_rc1[:,:,:3]
    panel[50:, 2*w:] = diff_x8
    
    cv2.putText(panel, f"{title} - Nightly 14", (20, 32), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (200, 200, 255), 2)
    cv2.putText(panel, f"{title} - RC1 Candidate", (w + 20, 32), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (150, 255, 150), 2)
    cv2.putText(panel, f"Absolute Difference (x8 Amplified)", (2*w + 20, 32), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (100, 200, 255), 2)
    
    out_path = os.path.join(OUTPUT_DIR, out_name)
    cv2.imwrite(out_path, panel)
    print(f"Generated comparison panel: {out_path}")

print("=== GENERATING DETAILED VISUAL AUDIT PANELS FOR PHASE 4 ===")
for sc in ["TEMPLE_INTERIOR", "NAVI_TEMPLE_REFLECTIVE_FLOOR", "BASELINE_KOKIRI", "HYRULE_FIELD_DAY", "HYRULE_FIELD_NIGHT"]:
    n14_dir = os.path.join(SCENE_HARNESS, NIGHTLY14_RUNS[sc])
    rc1_dir = os.path.join(SCENE_HARNESS, RC1_RUNS[sc])
    
    img_n14 = load_img(n14_dir, "comparison_dxr_on_*_raw.png")
    img_rc1 = load_img(rc1_dir, "comparison_dxr_on_*_raw.png")
    
    if img_n14 is not None and img_rc1 is not None:
        create_tri_panel(sc, img_n14, img_rc1, f"audit_{sc.lower()}_n14_vs_rc1_dxr_on.png")

print("Visual audit panels generated successfully.")
