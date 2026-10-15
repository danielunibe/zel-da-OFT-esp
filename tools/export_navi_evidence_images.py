import os
import shutil
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFont

RUN_DIR = Path("reports/scene_harness/20261015_021411_776_NAVI_KOKIRI_POINT_NORMAL")
CAPTURES = RUN_DIR / "captures"
ARTIFACT_DIR = Path(r"C:\Users\danie\.gemini\antigravity-ide\brain\bd0b1fe2-ccd1-426f-9454-83ebde8b26d6")

# Load SDR previews
img_a = Image.open(CAPTURES / "navi_a_sdr_frame499_opaque_preview.png").convert("RGB")
img_b = Image.open(CAPTURES / "navi_b_sdr_frame499_opaque_preview.png").convert("RGB")
img_c = Image.open(CAPTURES / "navi_c_sdr_frame499_opaque_preview.png").convert("RGB")
img_d = Image.open(CAPTURES / "navi_d_sdr_frame499_opaque_preview.png").convert("RGB")

arr_a = np.asarray(img_a).astype(np.float32)
arr_b = np.asarray(img_b).astype(np.float32)
arr_c = np.asarray(img_c).astype(np.float32)
arr_d = np.asarray(img_d).astype(np.float32)

# Compute Delta |D - C| (Navi point light contribution on top of full Kokiri DXR)
delta_dc = np.abs(arr_d - arr_c)
delta_dc_amplified = np.clip(delta_dc * 8.0, 0, 255).astype(np.uint8)

# Compute Delta |B - A| (Isolated Navi point light)
delta_ba = np.abs(arr_b - arr_a)
delta_ba_amplified = np.clip(delta_ba * 4.0, 0, 255).astype(np.uint8)

# False color heatmap for |D - C|
mag_dc = np.max(delta_dc, axis=2)
heatmap = np.zeros((*mag_dc.shape, 3), dtype=np.uint8)
# Red = high delta, Yellow = medium delta, Cyan = low delta
norm_mag = np.clip(mag_dc / 32.0, 0.0, 1.0)
heatmap[..., 0] = (np.clip(norm_mag * 2.0, 0.0, 1.0) * 255).astype(np.uint8)
heatmap[..., 1] = (np.clip((1.0 - np.abs(norm_mag - 0.5) * 2.0), 0.0, 1.0) * 255).astype(np.uint8)
heatmap[..., 2] = (np.clip((1.0 - norm_mag * 2.0), 0.0, 1.0) * 255).astype(np.uint8)
mask = mag_dc > 0
heatmap[~mask] = 0

img_delta_dc = Image.fromarray(delta_dc_amplified)
img_heatmap_dc = Image.fromarray(heatmap)

# Blend heatmap with Control D
img_overlay = Image.blend(img_d, img_heatmap_dc, alpha=0.55)

# Create a 2x2 grid panel (A, B, C, D)
w, h = 640, 480
header_h = 40
quad = Image.new("RGB", (w * 2 + 30, (h + header_h) * 2 + 30), (18, 20, 24))
draw = ImageDraw.Draw(quad)

panels = [
    ("Control A: Raster Base (Sin DXR)", img_a, 10, 10),
    ("Control B: Contribución Puntual Navi DXR Aislada", img_b, w + 20, 10),
    ("Control C: DXR Completo — Navi Puntual OFF", img_c, 10, h + header_h + 20),
    ("Control D: DXR Completo — Navi Puntual ON", img_d, w + 20, h + header_h + 20)
]

for title, p_img, x, y in panels:
    draw.rectangle([x, y, x + w, y + header_h], fill=(30, 35, 42))
    draw.text((x + 12, y + 10), title, fill=(240, 240, 240))
    quad.paste(p_img, (x, y + header_h))

# Create A/B side-by-side with Delta and Heatmap (1x3 panel)
tri = Image.new("RGB", (w * 3 + 40, h + header_h + 20), (18, 20, 24))
draw_tri = ImageDraw.Draw(tri)

tri_panels = [
    ("Control C: Kokiri DXR (Navi OFF)", img_c, 10, 10),
    ("Control D: Kokiri DXR (Navi ON)", img_d, w + 20, 10),
    ("Diferencia Amplificada |D - C| x8 (Huella Fotométrica)", img_delta_dc, w * 2 + 30, 10)
]

for title, p_img, x, y in tri_panels:
    draw_tri.rectangle([x, y, x + w, y + header_h], fill=(30, 35, 42))
    draw_tri.text((x + 12, y + 10), title, fill=(240, 240, 240))
    tri.paste(p_img, (x, y + header_h))

# Save outputs
out_quad_path = Path("reports/navi_light_quad_evidence.png")
out_tri_path = Path("reports/navi_light_ab_comparison.png")
out_delta_path = Path("reports/navi_light_delta_x8.png")

quad.save(out_quad_path)
tri.save(out_tri_path)
img_delta_dc.save(out_delta_path)

print(f"Saved {out_quad_path}")
print(f"Saved {out_tri_path}")
print(f"Saved {out_delta_path}")

# Copy to artifact directory
if ARTIFACT_DIR.exists():
    shutil.copy2(out_quad_path, ARTIFACT_DIR / "navi_light_quad_evidence.png")
    shutil.copy2(out_tri_path, ARTIFACT_DIR / "navi_light_ab_comparison.png")
    shutil.copy2(out_delta_path, ARTIFACT_DIR / "navi_light_delta_x8.png")
    print(f"Copied images to artifact directory: {ARTIFACT_DIR}")
