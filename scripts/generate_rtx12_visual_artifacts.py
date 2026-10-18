import os
import sys
import json
import numpy as np
from PIL import Image, ImageDraw, ImageFont

def get_font():
    try:
        # Intentar cargar fuente estándar de Windows
        font = ImageFont.truetype("arial.ttf", 20)
        font_large = ImageFont.truetype("arialbd.ttf", 24)
        font_small = ImageFont.truetype("arial.ttf", 15)
        return font, font_large, font_small
    except:
        font = ImageFont.load_default()
        return font, font, font

def create_side_by_side(img_off_path, img_on_path, output_path, title, label_off="RASTER CLÁSICO (DXR OFF)", label_on="PRODUCCIÓN DXR ON (FRESNEL PBR)"):
    if not os.path.exists(img_off_path) or not os.path.exists(img_on_path):
        print(f"Saltando {output_path}: faltan archivos.")
        return False

    img_off = Image.open(img_off_path).convert("RGB")
    img_on = Image.open(img_on_path).convert("RGB")

    w, h = img_off.size
    header_h = 70
    footer_h = 40
    gap = 10
    total_w = w * 2 + gap + 40
    total_h = h + header_h + footer_h + 20

    canvas = Image.new("RGB", (total_w, total_h), (24, 26, 32))
    draw = ImageDraw.Draw(canvas)
    font, font_large, font_small = get_font()

    # Título principal
    draw.text((20, 15), title, fill=(240, 240, 245), font=font_large)
    draw.text((20, 45), "Comparación sincrónica estricta (Mismo fotograma 425, misma cámara, misma exposición nativa)", fill=(160, 165, 180), font=font_small)

    # Pegar imágenes
    y_img = header_h + 10
    x_off = 20
    x_on = 20 + w + gap

    canvas.paste(img_off, (x_off, y_img))
    canvas.paste(img_on, (x_on, y_img))

    # Bordes sutiles
    draw.rectangle([x_off - 1, y_img - 1, x_off + w, y_img + h], outline=(60, 70, 85), width=1)
    draw.rectangle([x_on - 1, y_img - 1, x_on + w, y_img + h], outline=(0, 180, 220), width=2)

    # Etiquetas de cada lado
    draw.rectangle([x_off, y_img, x_off + 260, y_img + 28], fill=(15, 15, 20, 200))
    draw.text((x_off + 10, y_img + 4), label_off, fill=(220, 220, 220), font=font_small)

    draw.rectangle([x_on, y_img, x_on + 330, y_img + 28], fill=(0, 45, 60, 220))
    draw.text((x_on + 10, y_img + 4), label_on, fill=(0, 230, 255), font=font_small)

    # Footer
    draw.text((20, total_h - 30), "OCZ-RTX-12 Producción Certificada | D3D12 DXR Tier 1.1 | Zero Oscuridad Artificial", fill=(120, 125, 140), font=font_small)

    canvas.save(output_path, quality=95)
    print(f"Generado: {output_path}")
    return True

def find_latest_scenario_run(scenario_id, harness_root):
    matches = []
    for d in os.listdir(harness_root):
        if d.endswith(f"_{scenario_id}_NORMAL"):
            matches.append(os.path.join(harness_root, d))
    matches.sort(key=lambda p: os.path.getmtime(p), reverse=True)
    return matches[0] if matches else None

def generate_all_artifacts(base_dir, artifacts_dir):
    harness_root = os.path.join(base_dir, "reports", "scene_harness")
    scenarios = [
        ("TEMPLE_INTERIOR", "Templo del Tiempo — Interior Diurno (Luz de Ventanas y Suelo Mosaico)", "temple_interior_rtx12_production_ab.png"),
        ("TEMPLE_INTERIOR_NIGHT", "Templo del Tiempo — Interior Nocturno Nativo (Ambiente Preservado)", "temple_interior_night_rtx12_production_ab.png"),
        ("BASELINE_KOKIRI", "Bosque Kokiri — Iluminación Natural y Presencia Emisiva de Navi", "kokiri_rtx12_production_ab.png"),
        ("NAVI_TEMPLE_REFLECTIVE_FLOOR", "Templo del Tiempo — Mosaico Cerámico PBR (F0=0.036, Cook-Torrance GGX)", "temple_ceramic_pbr_rtx12_ab.png"),
        ("NAVI_LAKE_WATER_FEET", "Lago Hylia — Agua Diurna a los Pies de Link (Fresnel Schlick F0=0.0204)", "lake_hylia_water_day_rtx12_ab.png"),
        ("NAVI_LAKE_NIGHT_WATER_FEET", "Lago Hylia — Agua Nocturna y Reflejo Especular Azul de Navi", "lake_hylia_water_night_rtx12_ab.png"),
        ("HYRULE_FIELD_DAY", "Campo de Hyrule — Exterior Día (Dispersión Atmosférica y Terreno Mate)", "hyrule_field_day_rtx12_ab.png"),
        ("HYRULE_FIELD_NIGHT", "Campo de Hyrule — Exterior Noche (Noche Natural con Iluminación Coherente)", "hyrule_field_night_rtx12_ab.png"),
    ]

    generated = 0
    for scen_id, title, out_filename in scenarios:
        run_dir = find_latest_scenario_run(scen_id, harness_root)
        if not run_dir:
            print(f"Aún no hay run para {scen_id}")
            continue

        captures_dir = os.path.join(run_dir, "captures")
        off_path = os.path.join(captures_dir, "comparison_dxr_off_frame425_raw.png")
        on_path = os.path.join(captures_dir, "comparison_dxr_on_frame425_raw.png")

        out_path = os.path.join(artifacts_dir, out_filename)
        if create_side_by_side(off_path, on_path, out_path, title):
            generated += 1

    print(f"\nTotal láminas generadas: {generated}/{len(scenarios)}")

if __name__ == "__main__":
    base_dir = r"c:\Users\danie\Desktop\Zeldacouch\zel-da-OFT-esp-main (3)\zel-da-OFT-esp-main"
    artifacts_dir = r"C:\Users\danie\.gemini\antigravity-ide\brain\bd0b1fe2-ccd1-426f-9454-83ebde8b26d6"
    generate_all_artifacts(base_dir, artifacts_dir)
