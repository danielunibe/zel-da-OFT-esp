import os
import sys
import json
import numpy as np
from PIL import Image
from pathlib import Path

def analyze_temple_run(run_dir_str):
    run_dir = Path(run_dir_str)
    captures_dir = run_dir / "captures"
    
    print(f"=== ANALYZING TEMPLE RUN: {run_dir.name} ===")
    
    # 1. Load key images
    dxr_off_path = captures_dir / "comparison_dxr_off_frame425_raw.png"
    dxr_on_path = captures_dir / "comparison_dxr_on_frame425_raw.png"
    optics_off_path = captures_dir / "comparison_optics_off_frame425_raw.png"
    relight_off_path = captures_dir / "comparison_relight_off_frame425_raw.png"
    indirect_off_path = captures_dir / "comparison_indirect_off_frame425_raw.png"
    navi_on_path = captures_dir / "navi_point_on_sdr_frame425_raw.png"
    navi_off_path = captures_dir / "navi_point_off_sdr_frame425_raw.png"
    final_sdr_path = captures_dir / "final_sdr_pre_present_frame425_raw.png"
    
    img_dxr_off = np.array(Image.open(dxr_off_path).convert("RGB"), dtype=np.float32)
    img_dxr_on = np.array(Image.open(dxr_on_path).convert("RGB"), dtype=np.float32)
    img_optics_off = np.array(Image.open(optics_off_path).convert("RGB"), dtype=np.float32)
    img_relight_off = np.array(Image.open(relight_off_path).convert("RGB"), dtype=np.float32)
    img_indirect_off = np.array(Image.open(indirect_off_path).convert("RGB"), dtype=np.float32)
    img_navi_on = np.array(Image.open(navi_on_path).convert("RGB"), dtype=np.float32)
    img_navi_off = np.array(Image.open(navi_off_path).convert("RGB"), dtype=np.float32)
    img_final_sdr = np.array(Image.open(final_sdr_path).convert("RGB"), dtype=np.float32)
    
    h, w, _ = img_dxr_on.shape
    print(f"Image resolution: {w}x{h}")
    
    # 2. Overall DXR effect metrics
    delta_dxr = np.abs(img_dxr_on - img_dxr_off)
    differing_pixels_dxr = np.sum(np.any(delta_dxr > 1.0, axis=-1))
    mean_delta_dxr = np.mean(delta_dxr)
    max_delta_dxr = np.max(delta_dxr)
    print(f"\n[OVERALL DXR vs RASTER]")
    print(f"Differing pixels (>1 byte): {differing_pixels_dxr} / {w*h} ({differing_pixels_dxr / (w*h) * 100:.2f}%)")
    print(f"Mean absolute delta: {mean_delta_dxr:.3f} bytes")
    print(f"Max absolute delta: {max_delta_dxr:.1f} bytes")
    
    # 3. Optics (Reflections & Specular) on Floor
    # Floor region: roughly bottom half y from 260 to 470, x from 50 to 590
    floor_slice = (slice(260, 470), slice(50, 590))
    floor_optics_delta = np.abs(img_dxr_on[floor_slice] - img_optics_off[floor_slice])
    floor_optics_active_px = np.sum(np.any(floor_optics_delta > 0.5, axis=-1))
    floor_total_px = floor_optics_delta.shape[0] * floor_optics_delta.shape[1]
    floor_mean_optics = np.mean(floor_optics_delta)
    floor_max_optics = np.max(floor_optics_delta)
    print(f"\n[TEMPLE CERAMIC FLOOR OPTICS (REFLECTIONS & SPECULAR)]")
    print(f"Floor region sampled: {floor_total_px} pixels")
    print(f"Floor optics modulated pixels: {floor_optics_active_px} ({floor_optics_active_px / floor_total_px * 100:.2f}%)")
    print(f"Floor optics mean delta: {floor_mean_optics:.3f} bytes, max: {floor_max_optics:.1f} bytes")
    
    # 4. Navi Point Light Contribution on Scene & Link
    delta_navi = np.abs(img_navi_on - img_navi_off)
    navi_active_px = np.sum(np.any(delta_navi > 0.5, axis=-1))
    navi_mean_delta = np.mean(delta_navi)
    navi_max_delta = np.max(delta_navi)
    print(f"\n[NAVI POINT LIGHT RESPONSE]")
    print(f"Navi point light modulated pixels: {navi_active_px} / {w*h} ({navi_active_px / (w*h) * 100:.2f}%)")
    print(f"Navi mean delta: {navi_mean_delta:.4f} bytes, max: {navi_max_delta:.1f} bytes")
    
    # 5. Link Receiver Area Analysis (Link is around center-screen x: 260..380, y: 160..300)
    link_slice = (slice(160, 300), slice(260, 380))
    link_navi_delta = delta_navi[link_slice]
    link_active_px = np.sum(np.any(link_navi_delta > 0.5, axis=-1))
    link_total_px = link_navi_delta.shape[0] * link_navi_delta.shape[1]
    link_mean_delta = np.mean(link_navi_delta)
    link_max_delta = np.max(link_navi_delta)
    print(f"\n[LINK RECEIVER RESPONSE (PLAYER CHARACTER)]")
    print(f"Link area sampled: {link_total_px} pixels")
    print(f"Link pixels receiving Navi light: {link_active_px} ({link_active_px / link_total_px * 100:.2f}%)")
    print(f"Link mean Navi irradiance: {link_mean_delta:.3f} bytes, max: {link_max_delta:.1f} bytes")
    
    # 6. Indirect GI Analysis
    delta_gi = np.abs(img_dxr_on - img_indirect_off)
    gi_active_px = np.sum(np.any(delta_gi > 0.5, axis=-1))
    print(f"\n[INDIRECT GI CONTRIBUTION]")
    print(f"GI modulated pixels: {gi_active_px} ({gi_active_px / (w*h) * 100:.2f}%)")
    print(f"GI mean delta: {np.mean(delta_gi):.3f} bytes, max: {np.max(delta_gi):.1f} bytes")
    
    # 7. Generate Output Evidence Images for the Report
    reports_dir = Path("reports")
    reports_dir.mkdir(exist_ok=True)
    
    # A. Quad Comparison (DXR OFF, DXR ON, OPTICS OFF, FINAL SDR)
    quad = np.zeros((h * 2, w * 2, 3), dtype=np.uint8)
    quad[0:h, 0:w] = np.clip(img_dxr_off, 0, 255).astype(np.uint8)
    quad[0:h, w:w*2] = np.clip(img_dxr_on, 0, 255).astype(np.uint8)
    quad[h:h*2, 0:w] = np.clip(img_optics_off, 0, 255).astype(np.uint8)
    quad[h:h*2, w:w*2] = np.clip(img_final_sdr, 0, 255).astype(np.uint8)
    Image.fromarray(quad).save(reports_dir / "temple_quad_evidence.png")
    print(f"Saved reports/temple_quad_evidence.png")
    
    # B. A/B Comparison and Delta x8 for Navi light
    ab_panel = np.zeros((h, w * 3, 3), dtype=np.uint8)
    ab_panel[:, 0:w] = np.clip(img_navi_off, 0, 255).astype(np.uint8)
    ab_panel[:, w:w*2] = np.clip(img_navi_on, 0, 255).astype(np.uint8)
    ab_panel[:, w*2:w*3] = np.clip(delta_navi * 8.0, 0, 255).astype(np.uint8)
    Image.fromarray(ab_panel).save(reports_dir / "temple_navi_ab_comparison.png")
    print(f"Saved reports/temple_navi_ab_comparison.png")
    
    # C. Floor Optics Delta x6 (Reflections and Specular Highlight)
    delta_optics = np.abs(img_dxr_on - img_optics_off)
    Image.fromarray(np.clip(delta_optics * 6.0, 0, 255).astype(np.uint8)).save(
        reports_dir / "temple_floor_optics_delta_x6.png")
    print(f"Saved reports/temple_floor_optics_delta_x6.png")
    
    # D. Save JSON summary metrics
    summary = {
        "runId": run_dir.name,
        "resolution": [w, h],
        "differingPixelsDxrVsRaster": int(differing_pixels_dxr),
        "differingPixelsPercent": float(differing_pixels_dxr / (w * h) * 100),
        "meanDeltaDxr": float(mean_delta_dxr),
        "maxDeltaDxr": float(max_delta_dxr),
        "floorOpticsActivePixels": int(floor_optics_active_px),
        "floorOpticsPercent": float(floor_optics_active_px / floor_total_px * 100),
        "floorOpticsMeanDelta": float(floor_mean_optics),
        "floorOpticsMaxDelta": float(floor_max_optics),
        "naviActivePixels": int(navi_active_px),
        "naviPercent": float(navi_active_px / (w * h) * 100),
        "naviMeanDelta": float(navi_mean_delta),
        "naviMaxDelta": float(navi_max_delta),
        "linkActivePixels": int(link_active_px),
        "linkPercent": float(link_active_px / link_total_px * 100),
        "linkMeanDelta": float(link_mean_delta),
        "linkMaxDelta": float(link_max_delta),
        "giActivePixels": int(gi_active_px),
        "giPercent": float(gi_active_px / (w * h) * 100)
    }
    with open(reports_dir / "temple_physical_analysis.json", "w") as f:
        json.dump(summary, f, indent=2)
    print(f"Saved reports/temple_physical_analysis.json")
    return summary

if __name__ == "__main__":
    run_path = sys.argv[1] if len(sys.argv) > 1 else "reports/scene_harness/20261015_151010_040_NAVI_TEMPLE_REFLECTIVE_FLOOR_NORMAL"
    analyze_temple_run(run_path)
