#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
consolidate_repository_components.py
Copia y organiza de forma canónica todos los componentes de Couch Edition en el
repositorio objetivo zel-da-OFT-esp.
"""

import os
import shutil
from pathlib import Path

SRC_DEV = Path(r"C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV")
DST_GIT = Path(r"C:\Users\danie\Desktop\zel-da-OFT-esp")

def safe_copy_file(src_file, dst_file):
    dst_file.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src_file, dst_file)

def copy_overlay_files():
    print("1. Consolidando source-overlay...")
    overlay_root = DST_GIT / "source-overlay"
    
    # Lista de archivos modificados/añadidos en Shipwright
    shipwright_root = SRC_DEV / "source" / "shipwright"
    sw_files = [
        "CMakeLists.txt",
        "soh/assets/custom/textures/buttons/LTBtn.i4.png",
        "soh/include/glyph_resolver.h",
        "soh/include/z64.h",
        "soh/soh/Enhancements/FileSelectEnhancements.cpp",
        "soh/soh/Enhancements/boss-rush/BossRush.cpp",
        "soh/soh/Enhancements/custom-message/CustomMessageManager.cpp",
        "soh/soh/Enhancements/custom-message/CustomMessageManager.h",
        "soh/soh/Enhancements/debugger/MessageViewer.cpp",
        "soh/soh/Enhancements/glyphs/GlyphResolver.cpp",
        "soh/soh/OTRGlobals.cpp",
        "soh/soh/ResourceManagerHelpers.cpp",
        "soh/soh/ShipUtils.cpp",
        "soh/soh/SohGui/SohMenu.h",
        "soh/soh/SohGui/SohMenuSettings.cpp",
        "soh/soh/z_message_OTR.cpp",
        "soh/src/code/game.c",
        "soh/src/code/z_kanfont.c",
        "soh/src/code/z_message_PAL.c",
        "soh/src/code/z_parameter.c",
        "soh/src/overlays/actors/ovl_En_Mag/z_en_mag.c",
        "soh/src/overlays/gamestates/ovl_file_choose/z_file_choose.c",
        "soh/src/overlays/gamestates/ovl_file_choose/z_file_nameset_PAL.c",
        "soh/src/overlays/misc/ovl_kaleido_scope/z_kaleido_map_PAL.c",
        "soh/src/overlays/misc/ovl_kaleido_scope/z_kaleido_scope_PAL.c"
    ]
    for rel in sw_files:
        src = shipwright_root / rel
        dst = overlay_root / "shipwright" / rel
        if src.exists():
            safe_copy_file(src, dst)
        else:
            print(f"WARN: No existe {src}")

    # Lista de archivos modificados/añadidos en LibUltraShip
    lus_root = shipwright_root / "libultraship"
    lus_files = [
        "include/fast/MaterialRegistry.h",
        "include/fast/MaterialRulesPilot.h",
        "include/fast/backends/gfx_direct3d_common.h",
        "include/fast/backends/gfx_rendering_api.h",
        "include/fast/interpreter.h",
        "src/fast/MaterialRegistry.cpp",
        "src/fast/backends/gfx_direct3d11.cpp",
        "src/fast/interpreter.cpp",
        "src/ship/Context.cpp",
        "src/ship/resource/ResourceManager.cpp"
    ]
    for rel in lus_files:
        src = lus_root / rel
        dst = overlay_root / "libultraship" / rel
        if src.exists():
            safe_copy_file(src, dst)
        else:
            print(f"WARN: No existe {src}")

def copy_localization():
    print("2. Consolidando localization...")
    loc_dst = DST_GIT / "localization"
    loc_src = SRC_DEV / "localization_workspace"
    glyph_src = SRC_DEV / "glyph_workspace"
    
    # Copiar localization_workspace
    for root, dirs, files in os.walk(loc_src):
        # Excluir pycache
        dirs[:] = [d for d in dirs if d != "__pycache__"]
        for f in files:
            ext = os.path.splitext(f)[1].lower()
            if ext in [".exe", ".pyc"]:
                continue
            src_f = Path(root) / f
            rel = src_f.relative_to(loc_src)
            dst_f = loc_dst / rel
            safe_copy_file(src_f, dst_f)
            
    # Copiar glyph_workspace
    glyph_dst = loc_dst / "glyphs"
    for root, dirs, files in os.walk(glyph_src):
        dirs[:] = [d for d in dirs if d != "__pycache__"]
        for f in files:
            ext = os.path.splitext(f)[1].lower()
            if ext in [".exe", ".pyc"]:
                continue
            src_f = Path(root) / f
            rel = src_f.relative_to(glyph_src)
            dst_f = glyph_dst / rel
            safe_copy_file(src_f, dst_f)

def copy_visual():
    print("3. Consolidando visual development...")
    vis_dst = DST_GIT / "visual"
    vis_src = SRC_DEV / "visual_workspace"
    
    for root, dirs, files in os.walk(vis_src):
        # Omitir material_intelligence_01 ya que reside en materials/
        if "material_intelligence_01" in root or "_work" in root:
            continue
        dirs[:] = [d for d in dirs if d not in ["__pycache__", "material_intelligence_01", "_work"]]
        for f in files:
            ext = os.path.splitext(f)[1].lower()
            if ext in [".exe", ".pyc", ".obj", ".pdb"]:
                continue
            src_f = Path(root) / f
            rel = src_f.relative_to(vis_src)
            dst_f = vis_dst / rel
            safe_copy_file(src_f, dst_f)

def copy_reports_and_docs():
    print("4. Consolidando reports y docs...")
    rep_dst = DST_GIT / "reports"
    rep_src = SRC_DEV / "reports"
    if rep_src.exists():
        for root, dirs, files in os.walk(rep_src):
            for f in files:
                src_f = Path(root) / f
                rel = src_f.relative_to(rep_src)
                dst_f = rep_dst / rel
                safe_copy_file(src_f, dst_f)
                
    doc_dst = DST_GIT / "docs"
    doc_src = SRC_DEV / "docs"
    if doc_src.exists():
        for root, dirs, files in os.walk(doc_src):
            for f in files:
                src_f = Path(root) / f
                rel = src_f.relative_to(doc_src)
                dst_f = doc_dst / rel
                safe_copy_file(src_f, dst_f)

def copy_tools_and_tests():
    print("5. Consolidando tools y tests...")
    tools_dst = DST_GIT / "tools"
    tools_src = SRC_DEV / "tools"
    tests_dst = DST_GIT / "tests"
    
    tools_dst.mkdir(parents=True, exist_ok=True)
    tests_dst.mkdir(parents=True, exist_ok=True)
    
    if tools_src.exists():
        for f in tools_src.iterdir():
            if f.is_file():
                ext = f.suffix.lower()
                if ext in [".exe", ".pyc"]:
                    continue
                if "test" in f.name.lower():
                    safe_copy_file(f, tests_dst / f.name)
                safe_copy_file(f, tools_dst / f.name)

if __name__ == "__main__":
    copy_overlay_files()
    copy_localization()
    copy_visual()
    copy_reports_and_docs()
    copy_tools_and_tests()
    print("CONSOLIDACIÓN COMPLETADA.")
