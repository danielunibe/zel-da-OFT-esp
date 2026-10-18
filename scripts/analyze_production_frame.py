import os
import sys
import json
import numpy as np
from PIL import Image

def analyze_run(run_dir):
    captures_dir = os.path.join(run_dir, "captures")
    if not os.path.exists(captures_dir):
        print(f"Error: {captures_dir} no existe.")
        return None

    dxr_off_path = os.path.join(captures_dir, "comparison_dxr_off_frame425_raw.png")
    dxr_on_path = os.path.join(captures_dir, "comparison_dxr_on_frame425_raw.png")

    if not os.path.exists(dxr_off_path) or not os.path.exists(dxr_on_path):
        print(f"Error: Faltan capturas en {captures_dir}")
        return None

    img_off = Image.open(dxr_off_path).convert("RGB")
    img_on = Image.open(dxr_on_path).convert("RGB")

    arr_off = np.array(img_off, dtype=np.float32)
    arr_on = np.array(img_on, dtype=np.float32)

    diff = np.abs(arr_on - arr_off)
    diff_per_pixel = np.max(diff, axis=2)

    # Luminancia Rec.709
    lum_off = 0.2126 * arr_off[:, :, 0] + 0.7152 * arr_off[:, :, 1] + 0.0722 * arr_off[:, :, 2]
    lum_on  = 0.2126 * arr_on[:, :, 0]  + 0.7152 * arr_on[:, :, 1]  + 0.0722 * arr_on[:, :, 2]

    total_pixels = arr_off.shape[0] * arr_off.shape[1]
    differing_pixels = np.sum(diff_per_pixel > 0.5)
    differing_percent = (differing_pixels / total_pixels) * 100.0

    mean_lum_off = np.mean(lum_off)
    mean_lum_on  = np.mean(lum_on)
    lum_ratio    = mean_lum_on / (mean_lum_off + 1e-6)

    mae = np.mean(diff)
    max_delta = np.max(diff)
    p95_delta = np.percentile(diff, 95)

    # Análisis de oscurecimiento o preservación de luz ambiental
    # Identificar si hay zonas artificialmente aplastadas al 2% (oscuridad diagnóstica previa)
    crushed_pixels = np.sum((lum_off > 10.0) & (lum_on < 2.0))
    crushed_percent = (crushed_pixels / total_pixels) * 100.0

    # Separación por cuadrantes / regiones (suelo: mitad inferior, entorno: mitad superior)
    h, w = arr_off.shape[:2]
    floor_off = lum_off[h//2:, :]
    floor_on  = lum_on[h//2:, :]
    upper_off = lum_off[:h//2, :]
    upper_on  = lum_on[:h//2, :]

    result = {
        "run_dir": run_dir,
        "width": w,
        "height": h,
        "total_pixels": int(total_pixels),
        "differing_pixels": int(differing_pixels),
        "differing_percent": float(differing_percent),
        "mean_luminance_raster_off": float(mean_lum_off),
        "mean_luminance_dxr_on": float(mean_lum_on),
        "luminance_ratio_on_over_off": float(lum_ratio),
        "mae": float(mae),
        "max_delta": float(max_delta),
        "p95_delta": float(p95_delta),
        "crushed_pixels_count": int(crushed_pixels),
        "crushed_pixels_percent": float(crushed_percent),
        "floor_region_lum_off": float(np.mean(floor_off)),
        "floor_region_lum_on": float(np.mean(floor_on)),
        "upper_region_lum_off": float(np.mean(upper_off)),
        "upper_region_lum_on": float(np.mean(upper_on)),
        "legibility_status": "EXCELLENT" if lum_ratio >= 0.75 and crushed_percent < 0.5 else "CRUSHED_OR_DARKENED"
    }

    print(json.dumps(result, indent=2))
    return result

if __name__ == "__main__":
    if len(sys.argv) > 1:
        run_dir = sys.argv[1]
    else:
        # Por defecto el run más reciente en reports/scene_harness
        harness_dir = os.path.join(os.path.dirname(__file__), "..", "reports", "scene_harness")
        subdirs = [os.path.join(harness_dir, d) for d in os.listdir(harness_dir) if os.path.isdir(os.path.join(harness_dir, d))]
        subdirs.sort(key=lambda d: os.path.getmtime(d), reverse=True)
        run_dir = subdirs[0] if subdirs else None

    if run_dir:
        analyze_run(run_dir)
    else:
        print("No se encontró ningún run_dir para analizar.")
