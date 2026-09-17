#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
validate_recovery.py
Prueba automatizada para certificar que el repositorio contiene todo lo
necesario para la reconstrucción completa del proyecto desde cero.
"""

import sys
import os
import hashlib
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

def compute_sha256(filepath):
    h = hashlib.sha256()
    with open(filepath, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def run_tests():
    failures = []
    print("=== INICIANDO VALIDACIÓN DE RECUPERABILIDAD DEL ENTORNO ===")
    
    # 1. Verificar ausencia de ROMs y oot.o2r
    prohibited = ["oot.o2r", "baserom.z64", "baserom_original.z64"]
    for root, dirs, files in os.walk(REPO_ROOT):
        # Ignorar .git
        if "/.git" in root.replace("\\", "/"):
            continue
        for f in files:
            if f.lower() in prohibited or any(f.lower().endswith(ext) for ext in [".z64", ".n64", ".v64"]):
                failures.append(f"VIOLACIÓN DE COPYRIGHT: Se encontró archivo prohibido en el repo: {os.path.join(root, f)}")

    # 2. Verificar parches canónicos
    patch_dir = REPO_ROOT / "patches"
    sw_patch = patch_dir / "shipwright_couch_edition_v03_1_1.patch"
    lus_patch = patch_dir / "libultraship_couch_edition_v03_1_1.patch"
    if not sw_patch.exists():
        failures.append(f"Falta parche canónico de Shipwright: {sw_patch}")
    if not lus_patch.exists():
        failures.append(f"Falta parche canónico de LibUltraShip: {lus_patch}")
        
    # Verificar parches legacy en archive
    legacy_dir = REPO_ROOT / "archive" / "legacy-patches"
    if not (legacy_dir / "DO_NOT_APPLY.md").exists():
        failures.append("Falta DO_NOT_APPLY.md en archive/legacy-patches")

    # 3. Verificar source-overlay
    overlay_root = REPO_ROOT / "source-overlay"
    required_overlay = [
        overlay_root / "shipwright" / "soh" / "src" / "code" / "z_message_PAL.c",
        overlay_root / "shipwright" / "soh" / "include" / "glyph_resolver.h",
        overlay_root / "libultraship" / "src" / "fast" / "backends" / "gfx_direct3d11.cpp",
        overlay_root / "libultraship" / "src" / "fast" / "MaterialRegistry.cpp",
        overlay_root / "apply_source_overlay.ps1"
    ]
    for ro in required_overlay:
        if not ro.exists():
            failures.append(f"Falta archivo requerido en source-overlay: {ro}")

    # 4. Verificar Material Intelligence
    mat_dir = REPO_ROOT / "materials" / "material_intelligence_01"
    required_mat = [
        mat_dir / "02_RESOURCE_INVENTORY.csv",
        mat_dir / "04_MATERIAL_CLASSIFICATION.csv",
        mat_dir / "05_PBR_DEFAULTS.json",
        mat_dir / "17_MATERIAL_RULES_CANDIDATE.json",
        mat_dir / "20_FINAL_MATERIAL_INTELLIGENCE_REPORT.md"
    ]
    for rm in required_mat:
        if not rm.exists():
            failures.append(f"Falta dataset de Material Intelligence: {rm}")

    # 5. Verificar Localización
    loc_dir = REPO_ROOT / "localization"
    if not (loc_dir / "spanish").exists():
        failures.append(f"Falta directorio localization/spanish")

    # 6. Verificar Runtime Template
    rt_dir = REPO_ROOT / "runtime-template"
    if not (rt_dir / "USER_PROVIDES_OOT_O2R_HERE.txt").exists():
        failures.append("Falta USER_PROVIDES_OOT_O2R_HERE.txt en runtime-template")
    if not (rt_dir / "mods" / "es.o2r").exists():
        failures.append("Falta mods/es.o2r en runtime-template")

    # 7. Verificar Documentación
    docs_dir = REPO_ROOT / "docs"
    if not (docs_dir / "RECOVERY_FROM_ZERO.md").exists():
        failures.append("Falta docs/RECOVERY_FROM_ZERO.md")
    if not (docs_dir / "PROJECT_STRUCTURE.md").exists():
        failures.append("Falta docs/PROJECT_STRUCTURE.md")

    # Resultado
    if failures:
        print("\n--- FALLOS ENCONTRADOS ---")
        for f in failures:
            print(f"[FAIL] {f}")
        print("\nRECOVERY_VALIDATION: FAIL")
        sys.exit(1)
    else:
        print("\nTodos los componentes canónicos requeridos están presentes y validados.")
        print("RECOVERY_VALIDATION: PASS")
        sys.exit(0)

if __name__ == "__main__":
    run_tests()
