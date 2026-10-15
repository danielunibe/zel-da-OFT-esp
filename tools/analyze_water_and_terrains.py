import os
import sys
import json
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from pathlib import Path

def create_before_after_image(before_np, after_np, title_left, title_right, main_title, out_path, diff_mode=False):
    h, w, c = before_np.shape
    
    banner_height = 54
    footer_height = 32
    border = 4
    
    combined_w = w * 2 + border * 3
    combined_h = h + banner_height + footer_height
    
    out_img = Image.new("RGB", (combined_w, combined_h), (14, 16, 20))
    draw = ImageDraw.Draw(out_img)
    
    try:
        font_title = ImageFont.truetype("arial.ttf", 22)
        font_sub = ImageFont.truetype("arial.ttf", 15)
        font_small = ImageFont.truetype("arial.ttf", 12)
    except:
        font_title = font_sub = font_small = ImageFont.load_default()
        
    draw.text((20, 14), main_title, fill=(240, 245, 255), font=font_title)
    
    img_before = Image.fromarray(np.clip(before_np, 0, 255).astype(np.uint8))
    img_after = Image.fromarray(np.clip(after_np, 0, 255).astype(np.uint8))
    
    x1 = border
    y1 = banner_height
    x2 = w + border * 2
    
    out_img.paste(img_before, (x1, y1))
    out_img.paste(img_after, (x2, y1))
    
    # Draw badges
    draw.rectangle([x1 + 10, y1 + 10, x1 + 240, y1 + 38], fill=(0, 0, 0, 190), outline=(230, 60, 60))
    draw.text((x1 + 18, y1 + 14), title_left, fill=(255, 120, 120), font=font_sub)
    
    draw.rectangle([x2 + 10, y1 + 10, x2 + 300, y1 + 38], fill=(0, 0, 0, 190), outline=(0, 180, 255))
    draw.text((x2 + 18, y1 + 14), title_right, fill=(0, 220, 255), font=font_sub)
    
    draw.text((20, combined_h - 24), "Ocarina Couch Edition RTX — DXR Physical Terrain & Water Reflection Pipeline", fill=(130, 145, 165), font=font_small)
    
    out_img.save(out_path)
    print(f"Generated comparison: {out_path}")

def create_quad_image(img_tl, img_tr, img_bl, img_br, lbl_tl, lbl_tr, lbl_bl, lbl_br, title, out_path):
    h, w, c = img_tl.shape
    banner_height = 56
    footer_height = 32
    border = 4
    
    combined_w = w * 2 + border * 3
    combined_h = h * 2 + border * 3 + banner_height + footer_height
    
    out_img = Image.new("RGB", (combined_w, combined_h), (12, 14, 18))
    draw = ImageDraw.Draw(out_img)
    
    try:
        font_title = ImageFont.truetype("arial.ttf", 22)
        font_sub = ImageFont.truetype("arial.ttf", 15)
        font_small = ImageFont.truetype("arial.ttf", 12)
    except:
        font_title = font_sub = font_small = ImageFont.load_default()
        
    draw.text((20, 14), title, fill=(240, 245, 255), font=font_title)
    
    x1 = border
    y1 = banner_height
    x2 = w + border * 2
    y2 = banner_height + h + border
    
    out_img.paste(Image.fromarray(np.clip(img_tl, 0, 255).astype(np.uint8)), (x1, y1))
    out_img.paste(Image.fromarray(np.clip(img_tr, 0, 255).astype(np.uint8)), (x2, y1))
    out_img.paste(Image.fromarray(np.clip(img_bl, 0, 255).astype(np.uint8)), (x1, y2))
    out_img.paste(Image.fromarray(np.clip(img_br, 0, 255).astype(np.uint8)), (x2, y2))
    
    for (x, y, lbl, outline_col, text_col) in [
        (x1, y1, lbl_tl, (220, 60, 60), (255, 120, 120)),
        (x2, y1, lbl_tr, (0, 180, 255), (0, 220, 255)),
        (x1, y2, lbl_bl, (200, 160, 40), (240, 200, 80)),
        (x2, y2, lbl_br, (180, 80, 240), (220, 140, 255)),
    ]:
        draw.rectangle([x + 10, y + 10, x + 280, y + 38], fill=(0, 0, 0, 200), outline=outline_col)
        draw.text((x + 18, y + 14), lbl, fill=text_col, font=font_sub)
        
    draw.text((20, combined_h - 24), "Ocarina Couch Edition RTX — DXR 4-Way Water & Terrain Forensic Analysis", fill=(130, 145, 165), font=font_small)
    out_img.save(out_path)
    print(f"Generated quad: {out_path}")

def run_analysis():
    import glob
    day_dirs = sorted(glob.glob('reports/scene_harness/*NAVI_LAKE_WATER_FEET_NORMAL'), key=os.path.getmtime)
    night_dirs = sorted(glob.glob('reports/scene_harness/*NAVI_LAKE_NIGHT_WATER_FEET_NORMAL'), key=os.path.getmtime)
    
    if not day_dirs:
        print("No day dirs found")
        return
    day_dir = Path(day_dirs[-1])
    night_dir = Path(night_dirs[-1]) if night_dirs else None
    
    print(f"Analyzing Day: {day_dir}")
    if night_dir:
        print(f"Analyzing Night: {night_dir}")
        
    # Load Day images
    day_off = np.array(Image.open(day_dir / "captures" / "comparison_dxr_off_frame425_raw.png").convert("RGB"))
    day_on = np.array(Image.open(day_dir / "captures" / "comparison_dxr_on_frame425_raw.png").convert("RGB"))
    day_optics_off = np.array(Image.open(day_dir / "captures" / "comparison_optics_off_frame425_raw.png").convert("RGB"))
    
    # Delta
    day_delta = np.abs(day_on.astype(np.float32) - day_off.astype(np.float32))
    day_delta_x4 = np.clip(day_delta * 4.0, 0, 255).astype(np.uint8)
    
    # Stats
    mean_delta = np.mean(day_delta)
    max_delta = np.max(day_delta)
    diff_pixels = np.sum(np.any(day_delta > 1.0, axis=2))
    total_pixels = day_off.shape[0] * day_off.shape[1]
    
    print(f"[DAY] Mean delta: {mean_delta:.2f}, Max delta: {max_delta:.2f}, Diff pixels: {diff_pixels}/{total_pixels} ({diff_pixels/total_pixels*100:.2f}%)")
    
    # Save artifacts in brain directory and reports directory
    brain_dir = Path(r"C:\Users\danie\.gemini\antigravity-ide\brain\bd0b1fe2-ccd1-426f-9454-83ebde8b26d6")
    
    # 1. Day Before / After
    create_before_after_image(
        day_off, day_on,
        "Raster Puro (DXR OFF)",
        "DXR ON (Agua Reflectiva Fresnel + Navi)",
        "Lago Hylia · Link con Agua a los Pies y Navi Azul Eléctrico (Día)",
        str(brain_dir / "lake_hylia_day_water_feet_ab.png")
    )
    
    # 2. Day Delta Amplified
    create_before_after_image(
        day_on, day_delta_x4,
        "DXR ON (Render Final)",
        "Delta DXR Amplificado x4 (Reflexión Agua)",
        "Lago Hylia · Aislamiento Forense de Reflexión en Agua (Día)",
        str(brain_dir / "lake_hylia_day_water_delta_x4.png")
    )
    
    # 3. If Night is available, process night
    if night_dir:
        night_off = np.array(Image.open(night_dir / "captures" / "comparison_dxr_off_frame425_raw.png").convert("RGB"))
        night_on = np.array(Image.open(night_dir / "captures" / "comparison_dxr_on_frame425_raw.png").convert("RGB"))
        night_delta = np.abs(night_on.astype(np.float32) - night_off.astype(np.float32))
        night_delta_x4 = np.clip(night_delta * 4.0, 0, 255).astype(np.uint8)
        
        create_before_after_image(
            night_off, night_on,
            "Raster Nocturno (DXR OFF)",
            "DXR ON (Reflexión Agua Nocturna + Navi)",
            "Lago Hylia Nocturno · Link con Agua a los Pies y Navi (Noche)",
            str(brain_dir / "lake_hylia_night_water_feet_ab.png")
        )
        
        # 4. Quad analysis: Day vs Night
        create_quad_image(
            day_off, day_on,
            night_off, night_on,
            "Día: Raster Puro (DXR OFF)",
            "Día: DXR ON (Reflexión + Refracción Agua)",
            "Noche: Raster Puro (DXR OFF)",
            "Noche: DXR ON (Reflexión Navi en Agua)",
            "Lago Hylia · Matriz Día/Noche Link con Agua a los Pies y Navi",
            str(brain_dir / "lake_hylia_water_quad_matrix.png")
        )

if __name__ == "__main__":
    run_analysis()
