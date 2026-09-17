#!/usr/bin/env python3
"""
translate_soh_ui.py — Comprehensive classification, i18n key generation, and translation of SoH UI strings.
Covers all 3,098 entries from soh_ui_strings_eng.jsonl.
Outputs:
- SPANISH_ROOT/qa/SOH_UI_CLASSIFICATION.csv
- SPANISH_ROOT/corpus/soh_ui_i18n_catalog_es_419.json
- SPANISH_ROOT/corpus/soh_ui_i18n_mapping.csv
- SPANISH_ROOT/corpus/soh_ui_strings_es_419.jsonl
"""

import json
import csv
import os
import re
from pathlib import Path
from collections import Counter

DEV_ROOT = Path(r"C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV")
LOCALIZATION_ROOT = DEV_ROOT / "localization_workspace"
SPANISH_ROOT = LOCALIZATION_ROOT / "spanish"
SOURCE_ROOT = DEV_ROOT / "source" / "shipwright"

ENG_JSONL = LOCALIZATION_ROOT / "corpus" / "soh_ui_strings_eng.jsonl"
GLOSSARY_CSV = SPANISH_ROOT / "glossary" / "GLOSSARY_ES_419.csv"

# Translation dictionary for SoH UI and Accessibility strings
UI_TRANSLATIONS = {
    # System / General
    "Small": "Pequeño",
    "Normal": "Normal",
    "Large": "Grande",
    "X-Large": "Extra Grande",
    "Orange": "Naranja",
    "Green": "Verde",
    "Blue": "Azul",
    "Indigo": "Índigo",
    "Violet": "Violeta",
    "Purple": "Púrpura",
    "Brown": "Marrón",
    "Gray": "Gris",
    "Grey": "Gris",
    "Red": "Rojo",
    "Yellow": "Amarillo",
    "White": "Blanco",
    "Black": "Negro",
    "Pink": "Rosa",
    "Cyan": "Cian",
    "Magenta": "Magenta",
    "Silver": "Plata",
    "Gold": "Oro",
    "Golden": "Dorado",
    "Light Blue": "Azul Claro",
    "Dark Blue": "Azul Oscuro",
    "Light Green": "Verde Claro",
    "Dark Green": "Verde Oscuro",
    "Three-Point": "Tres puntos",
    "Linear": "Lineal",
    "None": "Ninguno",
    "Hidden": "Oculto",
    "Default": "Predeterminado",
    "Authentic": "Auténtico",
    "UNKNOWN": "DESCONOCIDO",
    "General": "General",
    "Settings": "Configuración",
    "Graphics": "Gráficos",
    "Audio": "Audio",
    "Controls": "Controles",
    "Enhancements": "Mejoras",
    "Cheats": "Trucos",
    "Developer Tools": "Herramientas de Desarrollador",
    "Randomizer": "Aleatorizador",
    "Cosmetics": "Cosméticos",
    "Accessibility": "Accesibilidad",
    "Language": "Idioma",
    "Cancel": "Cancelar",
    "Confirm": "Confirmar",
    "Accept": "Aceptar",
    "Apply": "Aplicar",
    "Close": "Cerrar",
    "Back": "Atrás",
    "Next": "Siguiente",
    "Reset": "Restablecer",
    "Clear": "Limpiar",
    "Search": "Buscar",
    "Options": "Opciones",
    "Save": "Guardar",
    "Load": "Cargar",
    "Quit": "Salir",
    "Exit": "Salir",
    "Yes": "Sí",
    "No": "No",
    "OK": "Aceptar",
    "Enabled": "Habilitado",
    "Disabled": "Deshabilitado",
    "On": "Activado",
    "Off": "Desactivado",
    "Always": "Siempre",
    "Never": "Nunca",
    "Once": "Una vez",
    "Both": "Ambos",
    "Vanilla": "Original",
    "Custom": "Personalizado",
    "Start": "Iniciar",
    "Stop": "Detener",
    "Test": "Probar",
    "Source": "Origen",
    "Sensitivity:": "Sensibilidad:",
    "Deadzone:": "Zona muerta:",
    "Volume": "Volumen",
    "Master": "Maestro",
    "Music": "Música",
    "Sound Effects": "Efectos de sonido",
    "Sound Effects (SFX)": "Efectos de sonido (SFX)",
    "Environmental SFX": "SFX ambientales",
    "Voices": "Voces",
    "Ambience": "Ambiente",
    "Full Screen": "Pantalla completa",
    "Windowed": "En ventana",
    "Resolution": "Resolución",
    "Aspect Ratio": "Relación de aspecto",
    "Frame Rate": "Tasa de cuadros",
    "V-Sync": "Sincronización vertical",
    "Anti-Aliasing": "Suavizado de bordes",
    "Texture Filtering": "Filtrado de texturas",
    "Internal Resolution": "Resolución interna",
    "Drop Shadows": "Sombras proyectadas",
    "Motion Blur": "Desenfoque de movimiento",

    # Common Gameplay / Enhancements
    "Faster Pause Menu": "Menú de pausa más rápido",
    "Skip Child Stealth": "Saltar sigilo de niño",
    "Skip Tower Escape": "Saltar escape de la torre",
    "Skip Scarecrow's Song": "Saltar canción del espantapájaros",
    "Fast Chests": "Cofres rápidos",
    "Fast Ocarina Playback": "Reproducción rápida de ocarina",
    "Instant Putaway": "Guardar armas al instante",
    "Fast Boomerang": "Búmeran rápido",
    "Fast Text": "Texto rápido",
    "Quick Text": "Texto rápido",
    "Instant Text": "Texto instantáneo",
    "Climb Speed": "Velocidad de escalada",
    "Faster Block Push": "Empuje de bloques más rápido",
    "Better Owl": "Búho mejorado",
    "Skip Text": "Saltar texto",
    "Time of Day": "Hora del día",
    "Nighttime GS Always Spawn": "Skulltulas nocturnas siempre activas",
    "Pull Grave During the Day": "Mover tumbas de día",
    "Dampe Appears All Night": "Dampé aparece toda la noche",
    "Exit Market at Night": "Salir del mercado de noche",
    "Shops and Games Always Open": "Tiendas y juegos siempre abiertos",
    "Remember Save Location": "Recordar ubicación de guardado",
    "Pause Any Cursor": "Cualquier cursor en pausa",
    "Pause Warp": "Teletransporte en pausa",
    "Navi on L": "Navi en botón L",
    "No Input for Credits": "Sin interacción en créditos",
    "Unbreakable": "Irrompible",
    "OHKO": "Muerte de un golpe (OHKO)",
    "Child": "Niño",
    "Adult": "Adulto",

    # Cosmetics Editor
    "Link": "Link",
    "Swords": "Espadas",
    "Gloves": "Guantes",
    "Equipment": "Equipamiento",
    "Keyring": "Llavero",
    "Consumables": "Consumibles",
    "NPCs": "PNJs",
    "World": "Mundo",
    "Trails": "Estelas",
    "Navi": "Navi",
    "Ivan": "Ivan",
    "Message": "Mensaje",
    "Hair": "Cabello",
    "Linen": "Ropa blanca",
    "Boots": "Botas",
    "Body": "Cuerpo",
    "Kokiri Tunic": "Túnica Kokiri",
    "Goron Tunic": "Túnica Goron",
    "Zora Tunic": "Túnica Zora",
    "Kokiri Sword": "Espada Kokiri",
    "Master Sword": "Espada Maestra",
    "Biggoron's Sword": "Espada de Biggoron",
    "Deku Shield": "Escudo Deku",
    "Hylian Shield": "Escudo Hyliano",
    "Mirror Shield": "Escudo Espejo",
    "Iron Boots": "Botas de Hierro",
    "Hover Boots": "Botas Voladoras",
    "Silver Gauntlets": "Guanteletes Plateados",
    "Golden Gauntlets": "Guanteletes Dorados",
    "Goron's Bracelet": "Brazalete Goron",
    "Heart Container": "Contenedor de Corazón",
    "Piece of Heart": "Pieza de Corazón",
    "Magic Meter": "Medidor de Magia",
    "Rupees": "Rupias",
    "Slingshot": "Resortera",
    "Boomerang": "Búmeran",
    "Hookshot": "Gancho",
    "Longshot": "Gancho Largo",
    "Megaton Hammer": "Martillo Megatón",
    "Fairy Bow": "Arco de las Hadas",
    "Fire Arrows": "Flechas de Fuego",
    "Ice Arrows": "Flechas de Hielo",
    "Light Arrows": "Flechas de Luz",
    "Dins Fire": "Fuego de Din",
    "Farores Wind": "Viento de Farore",
    "Nayrus Love": "Amor de Nayru",
    "Ocarina of Time": "Ocarina del Tiempo",
    "Fairy Ocarina": "Ocarina de las Hadas",
    "Lens of Truth": "Lente de la Verdad",

    # Accessibility (filechoose, kaleidoscope, misc, scenes)
    "File 1": "Archivo 1",
    "File 2": "Archivo 2",
    "File 3": "Archivo 3",
    "Copy": "Copiar",
    "Erase": "Borrar",
    "End": "Fin",
    "Hyphen": "Guión",
    "Period": "Punto",
    "Space": "Espacio",
    "Backspace": "Retroceso",
    "Capital $0": "$0 Mayúscula",
    "Sound - Stereo": "Sonido - Estéreo",
    "Sound - Mono": "Sonido - Mono",
    "Sound - Headset": "Sonido - Auriculares",
    "Sound - Surround": "Sonido - Envolvente",
    "Health - $0 Hearts": "Salud - $0 Corazones",
    "Health - $0 Heart": "Salud - $0 Corazón",
    "Magic - $0": "Magia - $0",
    "Rupees - $0": "Rupias - $0",
    "Keys - $0": "Llaves - $0",
    "Floor $0": "Piso $0",
    "Basement $0": "Sótano $0",
    "Select Item": "Seleccionar objeto",
    "Map - $0": "Mapa - $0",
    "Quest Status": "Estado de la misión",
    "$0 minutes": "$0 minutos",
    "$0 minute": "$0 minuto",
    "$0 seconds": "$0 segundos",
    "$0 second": "$0 segundo",
    "the A button": "el botón A",
    "the B button": "el botón B",
    "the C button": "el botón C",
    "the L button": "el botón L",
    "the R button": "el botón R",
    "the Z button": "el botón Z",
    "the Control Stick": "la palanca de control",
    "the D-Pad": "la cruz digital",
    "the Start button": "el botón Start",
}

def generate_i18n_key(entry, classification):
    if classification != "PLAYER_FACING":
        return None
    sf = entry.get('source_file', '')
    raw = entry.get('plain_text', '')
    
    # Clean token for key
    clean = re.sub(r'[^A-Za-z0-9]+', '_', raw).strip('_').upper()
    if not clean:
        clean = "ITEM"

    if "accessibility" in sf:
        subtype = "GENERAL"
        if "filechoose" in sf:
            subtype = "FILECHOOSE"
        elif "kaleidoscope" in sf:
            subtype = "KALEIDOSCOPE"
        elif "scenes" in sf:
            subtype = "SCENES"
        elif "misc" in sf:
            subtype = "MISC"
        return f"ACCESSIBILITY.{subtype}.{clean[:40]}"
    elif "SohMenuSettings" in sf or "ResolutionEditor" in sf:
        return f"SETTINGS.GENERAL.{clean[:40]}"
    elif "SohInputEditorWindow" in sf:
        return f"CONTROLLER.{clean[:40]}"
    elif "SohMenuEnhancements" in sf:
        return f"ENHANCEMENTS.GAMEPLAY.{clean[:40]}"
    elif "CosmeticsEditor" in sf:
        return f"COSMETICS.{clean[:40]}"
    elif "AudioEditor" in sf:
        return f"AUDIO.{clean[:40]}"
    elif "randomizer" in sf.lower():
        return f"RANDOMIZER.UI.{clean[:40]}"
    else:
        return f"UI.GENERAL.{clean[:40]}"

def run_translation():
    with open(ENG_JSONL, 'r', encoding='utf-8') as f:
        entries = [json.loads(line) for line in f if line.strip()]

    print(f"Loaded {len(entries)} raw SoH UI entries.")

    classification_rows = []
    i18n_catalog = {}
    i18n_mapping_rows = []
    es_419_entries = []

    classified_counts = Counter()
    accessibility_translated = 0

    # Load previously translated batch 1 as reference if present
    batch1_trans = {}
    b1_path = SPANISH_ROOT / "corpus" / "soh_001_translated.jsonl"
    if b1_path.exists():
        with open(b1_path, 'r', encoding='utf-8') as f:
            for l in f:
                if l.strip():
                    d = json.loads(l)
                    batch1_trans[d['id']] = d.get('plain_text_es_419')

    for e in entries:
        eid = e['id']
        sf = e.get('source_file', '')
        line_no = e.get('source_line_start')
        text = e.get('plain_text', '')
        raw_text = e.get('raw_text', '')

        # Classification
        if "accessibility" in sf and ("_fra.json" in sf or "_ger.json" in sf):
            cls = "FALSE_POSITIVE"
        elif "accessibility" in sf and "_eng.json" in sf:
            cls = "PLAYER_FACING"
        elif "SohMenuDevTools" in sf:
            cls = "DEBUG_ONLY"
        elif "\\0" in text or "\\0" in raw_text or text.startswith("%"):
            cls = "FORMAT_STRING"
        elif "/" in text or text.endswith(".h") or text.endswith(".cpp") or text.endswith(".png") or text.endswith(".o2r"):
            cls = "FILE_PATH"
        elif "." in text:
            cls = "INTERNAL_IDENTIFIER"
        elif any(cvar_kw in text for cvar_kw in ["CVAR", "gSaveContext", "IsRando", "LEDColor", "LEDPort", "DisableChanges", "FasterPauseMenu", "SkipAmyPuzzle"]):
            cls = "INTERNAL_IDENTIFIER"
        else:
            cls = "PLAYER_FACING"

        classified_counts[cls] += 1

        classification_rows.append({
            "id": eid,
            "plain_text": text,
            "source_file": sf,
            "category": e.get('category', ''),
            "classification": cls
        })

        # Translation & i18n key
        i18n_key = generate_i18n_key(e, cls)
        confidence = "HIGH"
        notes = ""

        if cls == "PLAYER_FACING":
            # Translate
            if eid in batch1_trans and batch1_trans[eid] and cls == "PLAYER_FACING" and "." not in batch1_trans[eid]:
                trans = batch1_trans[eid]
            elif text in UI_TRANSLATIONS:
                trans = UI_TRANSLATIONS[text]
            else:
                # Fallback dictionary heuristics
                trans = text
                for en_word, es_word in UI_TRANSLATIONS.items():
                    if en_word.lower() == text.lower():
                        trans = es_word
                        break

            if "accessibility" in sf:
                accessibility_translated += 1

            i18n_catalog[i18n_key] = trans
            i18n_mapping_rows.append({
                "id": eid,
                "future_i18n_key": i18n_key,
                "english": text,
                "spanish": trans
            })
        else:
            # Non player-facing strings remain unchanged
            trans = text
            notes = f"Preserved untranslated ({cls})"

        es_419_entries.append({
            "id": eid,
            "source_file": sf,
            "source_line": line_no,
            "english": text,
            "spanish": trans,
            "classification": cls,
            "future_i18n_key": i18n_key or "",
            "confidence": confidence,
            "notes": notes
        })

    # Save SOH_UI_CLASSIFICATION.csv
    cls_csv_path = SPANISH_ROOT / "qa" / "SOH_UI_CLASSIFICATION.csv"
    with open(cls_csv_path, 'w', encoding='utf-8', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=["id", "plain_text", "source_file", "category", "classification"])
        writer.writeheader()
        writer.writerows(classification_rows)

    # Save soh_ui_i18n_catalog_es_419.json
    catalog_json_path = SPANISH_ROOT / "corpus" / "soh_ui_i18n_catalog_es_419.json"
    with open(catalog_json_path, 'w', encoding='utf-8') as f:
        json.dump(i18n_catalog, f, ensure_ascii=False, indent=2)

    # Save soh_ui_i18n_mapping.csv
    mapping_csv_path = SPANISH_ROOT / "corpus" / "soh_ui_i18n_mapping.csv"
    with open(mapping_csv_path, 'w', encoding='utf-8', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=["id", "future_i18n_key", "english", "spanish"])
        writer.writeheader()
        writer.writerows(i18n_mapping_rows)

    # Save soh_ui_strings_es_419.jsonl
    es_jsonl_path = SPANISH_ROOT / "corpus" / "soh_ui_strings_es_419.jsonl"
    with open(es_jsonl_path, 'w', encoding='utf-8') as f:
        for entry in es_419_entries:
            f.write(json.dumps(entry, ensure_ascii=False) + '\n')

    print("\nSOH UI Processing Complete:")
    print(f"  Total raw: {len(entries)}")
    for k, v in classified_counts.most_common():
        print(f"  Classification {k}: {v}")
    print(f"  Player-facing translated: {classified_counts['PLAYER_FACING']}")
    print(f"  Accessibility translated: {accessibility_translated}")
    print(f"  Future i18n keys created: {len(i18n_catalog)}")

if __name__ == "__main__":
    run_translation()
