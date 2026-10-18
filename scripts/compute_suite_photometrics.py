import os
import json
import numpy as np
from PIL import Image

def compute_metrics_for_suite():
    base_dir = r"c:\Users\danie\Desktop\Zeldacouch\zel-da-OFT-esp-main (3)\zel-da-OFT-esp-main"
    harness_root = os.path.join(base_dir, "reports", "scene_harness")
    results_json = os.path.join(harness_root, "OCZ_RTX_12_FINAL_SUITE_RESULTS.json")

    with open(results_json, "r", encoding="utf-8") as f:
        runs = json.load(f)

    metrics_table = []

    for item in runs:
        scen = item["Scenario"]
        run_path = item["RunPath"]
        captures_dir = os.path.join(run_path, "captures")
        off_path = os.path.join(captures_dir, "comparison_dxr_off_frame425_raw.png")
        on_path = os.path.join(captures_dir, "comparison_dxr_on_frame425_raw.png")

        if not os.path.exists(off_path) or not os.path.exists(on_path):
            continue

        img_off = Image.open(off_path).convert("RGB")
        img_on = Image.open(on_path).convert("RGB")

        arr_off = np.array(img_off, dtype=np.float32)
        arr_on = np.array(img_on, dtype=np.float32)

        diff = np.abs(arr_on - arr_off)
        diff_max = np.max(diff, axis=2)

        total_px = arr_off.shape[0] * arr_off.shape[1]
        active_px = int(np.sum(diff_max > 0.5))
        active_pct = (active_px / total_px) * 100.0

        lum_off = 0.2126 * arr_off[:, :, 0] + 0.7152 * arr_off[:, :, 1] + 0.0722 * arr_off[:, :, 2]
        lum_on  = 0.2126 * arr_on[:, :, 0]  + 0.7152 * arr_on[:, :, 1]  + 0.0722 * arr_on[:, :, 2]

        mean_lum_off = float(np.mean(lum_off))
        mean_lum_on  = float(np.mean(lum_on))
        lum_ratio    = mean_lum_on / (mean_lum_off + 1e-6)

        mae = float(np.mean(diff))
        max_delta = float(np.max(diff))
        p95_delta = float(np.percentile(diff, 95))

        # Píxeles aplastados artificialmente a la oscuridad (< 2 cuando el raster tenía > 10)
        crushed = int(np.sum((lum_off > 10.0) & (lum_on < 2.0)))
        crushed_pct = (crushed / total_px) * 100.0

        metrics_table.append({
            "Scenario": scen,
            "TotalPixels": total_px,
            "ActivePixels": active_px,
            "ActivePercent": round(active_pct, 2),
            "MeanLumRaster": round(mean_lum_off, 2),
            "MeanLumDxr": round(mean_lum_on, 2),
            "LumRatio": round(lum_ratio, 3),
            "MAE": round(mae, 2),
            "MaxDelta": round(max_delta, 1),
            "P95Delta": round(p95_delta, 1),
            "CrushedPixels": crushed,
            "CrushedPercent": round(crushed_pct, 4),
            "Legibility": "EXCELLENT" if lum_ratio >= 0.75 and crushed_pct < 0.1 else "DEFICIENT"
        })

    out_metrics = os.path.join(harness_root, "OCZ_RTX_12_PHOTOMETRIC_METRICS.json")
    with open(out_metrics, "w", encoding="utf-8") as f:
        json.dump(metrics_table, f, indent=2)

    print("\n" + "="*80)
    print("TABLA FOTOMÉTRICA DE PRODUCCIÓN OCZ-RTX-12 (8 ESCENARIOS)")
    print("="*80)
    print(f"{'Escenario':<28} | {'Modulados':<11} | {'Lum Raster':<10} | {'Lum DXR':<8} | {'Ratio':<6} | {'Crushed':<8} | {'Legibilidad'}")
    print("-"*80)
    for m in metrics_table:
        print(f"{m['Scenario']:<28} | {m['ActivePercent']:>5}%     | {m['MeanLumRaster']:>10} | {m['MeanLumDxr']:>8} | {m['LumRatio']:>6} | {m['CrushedPercent']:>6}% | {m['Legibility']}")
    print("="*80)

if __name__ == "__main__":
    compute_metrics_for_suite()
