import os
import sys
import json
import glob
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from pathlib import Path

def create_before_after_image(before_np, after_np, title_left, title_right, main_title, out_path):
    h, w, c = before_np.shape
    
    banner_height = 50
    footer_height = 30
    border = 4
    
    combined_w = w * 2 + border * 3
    combined_h = h + banner_height + footer_height
    
    out_img = Image.new("RGB", (combined_w, combined_h), (12, 14, 18))
    draw = ImageDraw.Draw(out_img)
    
    try:
        font_title = ImageFont.truetype("arial.ttf", 20)
        font_sub = ImageFont.truetype("arial.ttf", 15)
        font_small = ImageFont.truetype("arial.ttf", 13)
    except:
        font_title = font_sub = font_small = ImageFont.load_default()
        
    draw.text((20, 14), main_title, fill=(240, 240, 255), font=font_title)
    
    img_before = Image.fromarray(np.clip(before_np, 0, 255).astype(np.uint8))
    img_after = Image.fromarray(np.clip(after_np, 0, 255).astype(np.uint8))
    
    x1 = border
    y1 = banner_height
    x2 = w + border * 2
    
    out_img.paste(img_before, (x1, y1))
    out_img.paste(img_after, (x2, y1))
    
    # Draw labels
    draw.rectangle([x1 + 10, y1 + 10, x1 + 250, y1 + 38], fill=(0, 0, 0, 190), outline=(220, 70, 70))
    draw.text((x1 + 18, y1 + 14), title_left, fill=(255, 120, 120), font=font_sub)
    
    draw.rectangle([x2 + 10, y1 + 10, x2 + 310, y1 + 38], fill=(0, 0, 0, 190), outline=(40, 160, 255))
    draw.text((x2 + 18, y1 + 14), title_right, fill=(80, 210, 255), font=font_sub)
    
    draw.text((20, combined_h - 22), "Ocarina Couch Edition RTX — Modo Noche Diagnostico Aislado (Navi Luz Exclusiva)", fill=(140, 150, 170), font=font_small)
    
    out_img.save(out_path)
    print(f"Saved: {out_path}")

def analyze_scenario(run_dir_str, scenario_name):
    run_dir = Path(run_dir_str)
    captures = run_dir / "captures"
    
    dxr_off_path = captures / "comparison_dxr_off_frame425_raw.png"
    dxr_on_path = captures / "comparison_dxr_on_frame425_raw.png"
    navi_off_path = captures / "navi_point_off_sdr_frame425_raw.png"
    navi_on_path = captures / "navi_point_on_sdr_frame425_raw.png"
    final_sdr_path = captures / "final_sdr_pre_present_frame425_raw.png"
    
    img_dxr_off = np.array(Image.open(dxr_off_path).convert("RGB"), dtype=np.float32)
    img_dxr_on = np.array(Image.open(dxr_on_path).convert("RGB"), dtype=np.float32)
    img_navi_off = np.array(Image.open(navi_off_path).convert("RGB"), dtype=np.float32)
    img_navi_on = np.array(Image.open(navi_on_path).convert("RGB"), dtype=np.float32)
    img_final_sdr = np.array(Image.open(final_sdr_path).convert("RGB"), dtype=np.float32)
    
    h, w, _ = img_dxr_on.shape
    total_px = h * w
    
    # Delta DXR vs Raster
    delta_dxr = np.abs(img_dxr_on - img_dxr_off)
    diff_dxr = np.sum(np.any(delta_dxr > 1.0, axis=-1))
    
    # Delta Navi (Electric Blue contribution in darkness)
    delta_navi = np.abs(img_navi_on - img_navi_off)
    diff_navi = np.sum(np.any(delta_navi > 1.0, axis=-1))
    
    blue_delta = delta_navi[:, :, 2]
    red_delta = delta_navi[:, :, 0]
    green_delta = delta_navi[:, :, 1]
    
    blue_dominant_px = np.sum((blue_delta > red_delta + 1.0) & (blue_delta > 2.0))
    
    # Link region
    link_slice = (slice(int(h * 0.45), int(h * 0.85)), slice(int(w * 0.35), int(w * 0.65)))
    link_navi_delta = delta_navi[link_slice]
    link_active_px = np.sum(np.any(link_navi_delta > 1.0, axis=-1))
    link_total_px = link_navi_delta.shape[0] * link_navi_delta.shape[1]
    link_mean_delta = np.mean(link_navi_delta)
    link_max_delta = np.max(link_navi_delta)
    
    metrics = {
        "scenario": scenario_name,
        "runId": run_dir.name,
        "resolution": [w, h],
        "differingPixelsDxrVsRaster": int(diff_dxr),
        "differingPixelsPercent": float(diff_dxr / total_px * 100),
        "naviActivePixels": int(diff_navi),
        "naviPercent": float(diff_navi / total_px * 100),
        "blueDominantPixels": int(blue_dominant_px),
        "linkActivePixels": int(link_active_px),
        "linkPercent": float(link_active_px / link_total_px * 100),
        "linkMeanDelta": float(link_mean_delta),
        "linkMaxDelta": float(link_max_delta)
    }
    
    print(f"\n=== METRICS: {scenario_name} ===")
    print(f"DXR vs Raster Differing Pixels: {diff_dxr} ({diff_dxr/total_px*100:.2f}%)")
    print(f"Navi Electric Blue Modulated Pixels: {diff_navi} ({diff_navi/total_px*100:.2f}%)")
    print(f"Blue-Dominant Radiance Pixels: {blue_dominant_px}")
    print(f"Link Modulation: {link_active_px} px ({link_active_px/link_total_px*100:.2f}%), Mean: {link_mean_delta:.2f}, Max: {link_max_delta:.1f}")
    
    return metrics, img_dxr_off, img_dxr_on, img_navi_off, img_navi_on

def find_latest_run(scenario_prefix):
    matches = glob.glob(f"reports/scene_harness/*_{scenario_prefix}_*")
    if not matches:
        raise FileNotFoundError(f"No runs found matching {scenario_prefix}")
    matches.sort()
    return matches[-1]

def main():
    temple_run = find_latest_run("NAVI_TEMPLE_NIGHT_SOLE_LIGHT")
    kokiri_run = find_latest_run("NAVI_KOKIRI_NIGHT_SOLE_LIGHT")
    
    out_dir = Path("reports")
    out_dir.mkdir(parents=True, exist_ok=True)
    
    # 1. Analyze Temple Night Sole Light
    temple_m, t_dxr_off, t_dxr_on, t_navi_off, t_navi_on = analyze_scenario(temple_run, "TEMPLE_NIGHT_SOLE_LIGHT")
    create_before_after_image(
        t_navi_off, t_navi_on,
        "BEFORE: Total Nocturnal Blackout",
        "AFTER: Navi Sole Light (Electric Blue + Ceramic Floor)",
        "TEMPLO DEL TIEMPO (NOCHE) — NAVI FUENTE UNICA DE EMISION",
        out_dir / "temple_night_sole_light_before_after.png"
    )
    create_before_after_image(
        t_dxr_off, t_dxr_on,
        "BEFORE: N64 Raster (Unlit Night)",
        "AFTER: DXR RTX Hybrid (Direct + Bounce GI + Reflections)",
        "TEMPLO DEL TIEMPO (NOCHE) — RASTER vs DXR RAYTRACING",
        out_dir / "temple_night_raster_vs_dxr.png"
    )
    
    # 2. Analyze Kokiri Night Sole Light
    kokiri_m, k_dxr_off, k_dxr_on, k_navi_off, k_navi_on = analyze_scenario(kokiri_run, "KOKIRI_NIGHT_SOLE_LIGHT")
    create_before_after_image(
        k_navi_off, k_navi_on,
        "BEFORE: Total Nocturnal Darkness",
        "AFTER: Navi Sole Light (Electric Blue on Grass & Link)",
        "BOSQUE KOKIRI (NOCHE) — NAVI FUENTE UNICA DE EMISION",
        out_dir / "kokiri_night_sole_light_before_after.png"
    )
    create_before_after_image(
        k_dxr_off, k_dxr_on,
        "BEFORE: N64 Raster (Unlit Night)",
        "AFTER: DXR RTX Hybrid (Direct + Bounce GI Kokiri)",
        "BOSQUE KOKIRI (NOCHE) — RASTER vs DXR RAYTRACING",
        out_dir / "kokiri_night_raster_vs_dxr.png"
    )
    
    summary = {
        "temple_night": temple_m,
        "kokiri_night": kokiri_m
    }
    
    with open(out_dir / "night_sole_light_analysis.json", "w") as f:
        json.dump(summary, f, indent=2)
    print("Saved: reports/night_sole_light_analysis.json")

if __name__ == "__main__":
    main()
