#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
generate_master_inventory.py
Auditoría e inventario maestro de archivos para Ocarina of Time PC - Couch Edition.
Escanea exhaustivamente las raíces autorizadas, calcula SHA-256, detecta duplicados,
audita derechos de autor y secretos, y genera los archivos CSV canónicos.
"""

import os
import sys
import hashlib
import csv
import re
from pathlib import Path

# Raíces autorizadas
ROOT_DEV = Path(r"C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV")
ROOT_GIT = Path(r"C:\Users\danie\Desktop\zel-da-OFT-esp")
OUT_DIR = ROOT_DEV / "github_migration"
OUT_DIR.mkdir(parents=True, exist_ok=True)

# Patrones de exclusión de secretos y temporales
SECRET_PATTERNS = [
    re.compile(r"ghp_[a-zA-Z0-9]{36}"),
    re.compile(r"github_pat_[a-zA-Z0-9_]{82}"),
    re.compile(r"AIza[0-9A-Za-z\-_]{35}"),
    re.compile(r"-----BEGIN (?:RSA|OPENSSH|EC|DSA|PRIVATE) KEY-----"),
]

COPYRIGHT_NAMES = {
    "oot.o2r", "baserom.z64", "baserom_original.z64", "baserom.n64", "baserom.v64",
    "zelda_ocarina_of_time.z64", "zelda_ocarina_of_time.n64"
}
COPYRIGHT_EXTS = {".z64", ".n64", ".v64"}

PERSONAL_SAVES = {"file1.sav", "file2.sav", "file3.sav", "global.sav", "oot_save.sav"}

def compute_sha256(filepath):
    h = hashlib.sha256()
    try:
        with open(filepath, "rb") as f:
            while chunk := f.read(65536):
                h.update(chunk)
        return h.hexdigest()
    except Exception as e:
        return f"ERROR: {e}"

def classify_file(abs_path_str, rel_path_str, size_bytes, filename, ext):
    norm_path = abs_path_str.replace("\\", "/").lower()
    
    # 1. Regla de copyright
    if filename.lower() in COPYRIGHT_NAMES or ext.lower() in COPYRIGHT_EXTS:
        return ("COPYRIGHT_EXCLUDED", "COPYRIGHT_PROHIBITED", "EXCLUDED", "Nintendo copyright rom/o2r asset")
    if filename.lower() in PERSONAL_SAVES:
        return ("PERSONAL", "PERSONAL_DATA", "EXCLUDED", "Personal user save game")
        
    # 2. Archivos temporales de build y cachés
    if any(p in norm_path for p in ["/build/", "/.vs/", "/out/", "/obj/", "/bin/x64/", "/bin/x86/", "/.agent_build.lock", "/__pycache__/"]):
        return ("BUILD_CACHE", "BUILD_ARTIFACT", "EXCLUDED", "Build intermediate / compiler cache")
        
    if any(p in norm_path for p in ["/.freebuff/", "/crash_dumps/", "/tmp/"]):
        return ("TEMPORARY", "TEMP_DATA", "EXCLUDED", "Temporary scratch/crash data")

    # 3. Source canónico
    if "/source/shipwright/" in norm_path:
        if any(p in norm_path for p in ["/build/", "/.git/"]):
            return ("BUILD_CACHE", "INTERNAL_GIT", "EXCLUDED", "Internal build/git data")
        return ("SOURCE_OF_TRUTH", "CANONICAL", "source-overlay/", "Shipwright/LibUltraShip development source")

    # 4. Parches
    if "/patches/" in norm_path or ext.lower() == ".patch":
        if "legacy" in norm_path or "v02" in norm_path or "rc1" in norm_path:
            return ("ARCHIVE", "HISTORICAL", "archive/legacy-patches/", "Historical patch")
        return ("REQUIRED_FOR_REPRODUCTION", "CANONICAL", "patches/", "Canonical production patch")

    # 5. Localización
    if "/localization_workspace/" in norm_path or "/glyph_workspace/" in norm_path:
        return ("REQUIRED_FOR_BUILD", "CANONICAL", "localization/", "Spanish localization & glyph source/tools")

    # 6. Visual y Material Intelligence
    if "/material_intelligence_01/" in norm_path:
        return ("REQUIRED_FOR_BUILD", "CANONICAL", "materials/material_intelligence_01/", "Material Intelligence dataset & rules")
    if "/visual_workspace/" in norm_path:
        return ("DEVELOPMENT_EVIDENCE", "CANONICAL", "visual/", "Visual pipeline architecture & reports")

    # 7. Herramientas y scripts
    if "/tools/" in norm_path or "/scripts/" in norm_path:
        return ("REQUIRED_FOR_BUILD", "CANONICAL", "tools/", "Development & validation tooling")

    # 8. Documentación y reportes
    if "/docs/" in norm_path:
        return ("DEVELOPMENT_EVIDENCE", "CANONICAL", "docs/", "Project documentation")
    if "/reports/" in norm_path:
        return ("DEVELOPMENT_EVIDENCE", "CANONICAL", "reports/", "Certification and QA reports")

    # 9. Runtimes consolidados (V03_1_1_RUNTIME)
    if "/v03_1_1_runtime/" in norm_path:
        if filename.lower() == "oot.o2r":
            return ("COPYRIGHT_EXCLUDED", "COPYRIGHT_PROHIBITED", "EXCLUDED", "Base ROM archive")
        if filename.lower() == "es.o2r":
            return ("GENERATED_REPRODUCIBLE", "CANONICAL", "runtime-template/mods/", "Canonical Spanish OTR resource")
        if filename.lower() == "soh.exe" or filename.lower() == "soh.o2r":
            return ("GENERATED_REPRODUCIBLE", "RELEASE_CANDIDATE", "EXCLUDED_FROM_GIT_TREE", "Built binary (distribute via GitHub Releases if legal)")
        return ("REQUIRED_FOR_RUNTIME", "CANONICAL", "runtime-template/", "Runtime template resource")

    # 10. Archivos en repositorio Git
    if norm_path.startswith(str(ROOT_GIT).replace("\\", "/").lower()):
        if "/.git/" in norm_path:
            return ("INTERNAL_GIT", "INTERNAL", "EXCLUDED", "Git repository metadata")
        return ("CANONICAL", "CANONICAL", rel_path_str, "Existing repository tracking file")

    return ("DEVELOPMENT_EVIDENCE", "WORKSPACE", "archive/", "Workspace auxiliary artifact")

def scan_roots():
    all_files = []
    sha_map = {} # sha256 -> list of file records
    
    roots = [ROOT_DEV, ROOT_GIT]
    
    for r in roots:
        print(f"Escaneando raiz: {r}")
        for dirpath, dirnames, filenames in os.walk(r):
            # Omitir .git internos para evitar millones de objetos git internos
            norm_dp = dirpath.replace("\\", "/")
            if "/.git" in norm_dp:
                continue
            for f in filenames:
                full_path = Path(dirpath) / f
                if full_path.is_symlink():
                    continue
                try:
                    stat = full_path.stat()
                    size = stat.st_size
                except Exception:
                    size = -1
                
                # Ignorar archivos temporales gigantes de compilación (>25MB) si son .obj, .pdb, .tlog
                ext = full_path.suffix.lower()
                if ext in [".pdb", ".obj", ".tlog", ".idb", ".ilk"] and size > 10 * 1024 * 1024:
                    continue
                
                sha = compute_sha256(full_path) if size < 100 * 1024 * 1024 else "SKIPPED_LARGE_FILE"
                try:
                    rel_to_root = str(full_path.relative_to(r))
                except Exception:
                    rel_to_root = str(full_path)
                
                cat, canon_status, git_dest, notes = classify_file(str(full_path), rel_to_root, size, f, ext)
                
                # Copyright status
                c_status = "PROHIBITED_COPYRIGHT" if cat == "COPYRIGHT_EXCLUDED" else "CLEAN_PROPRIETARY_OR_OPEN"
                
                # Secret status
                s_status = "CLEAN"
                if 0 < size < 1024 * 1024 and ext in [".txt", ".md", ".json", ".py", ".ps1", ".c", ".cpp", ".h", ".csv", ".xml", ".yml", ".yaml"]:
                    try:
                        with open(full_path, "r", encoding="utf-8", errors="ignore") as tf:
                            txt = tf.read()
                            for pat in SECRET_PATTERNS:
                                if pat.search(txt):
                                    s_status = "POTENTIAL_SECRET_DETECTED"
                                    break
                    except Exception:
                        pass
                
                rec = {
                    "absolute_path": str(full_path),
                    "relative_path": rel_to_root,
                    "size_bytes": size,
                    "sha256": sha,
                    "extension": ext,
                    "category": cat,
                    "canonical_status": canon_status,
                    "git_destination": git_dest,
                    "git_lfs": "TRUE" if (size > 10 * 1024 * 1024 and cat not in ["COPYRIGHT_EXCLUDED", "BUILD_CACHE"]) else "FALSE",
                    "copyright_status": c_status,
                    "secret_status": s_status,
                    "duplicate_group": "",
                    "notes": notes
                }
                all_files.append(rec)
                
                if sha not in sha_map:
                    sha_map[sha] = []
                sha_map[sha].append(rec)

    # Identificar duplicados
    dup_group_id = 1
    dup_map_records = []
    
    for sha, group in sha_map.items():
        if sha in ["ERROR", "SKIPPED_LARGE_FILE"]:
            continue
        if len(group) > 1:
            grp_name = f"DUP_GRP_{dup_group_id:04d}"
            def sort_key(item):
                p = item["absolute_path"].lower()
                if "zel-da-oft-esp" in p:
                    return 0
                if "patches" in p and "v03_1_1" in p:
                    return 1
                if "source/shipwright" in p:
                    return 2
                if "v03_1_1_runtime" in p:
                    return 3
                return 4
                
            sorted_group = sorted(group, key=sort_key)
            canonical_item = sorted_group[0]
            
            for item in group:
                item["duplicate_group"] = grp_name
                is_canon = (item["absolute_path"] == canonical_item["absolute_path"])
                dup_map_records.append({
                    "duplicate_group": grp_name,
                    "sha256": sha,
                    "size_bytes": item["size_bytes"],
                    "is_canonical": "YES" if is_canon else "NO",
                    "canonical_path": canonical_item["absolute_path"],
                    "duplicate_path": item["absolute_path"],
                    "action": "KEEP_IN_GIT" if is_canon else "EXCLUDE_OR_REFERENCE"
                })
            dup_group_id += 1

    # Separar archivos excluidos
    excluded_records = []
    for item in all_files:
        if item["category"] in ["COPYRIGHT_EXCLUDED", "BUILD_CACHE", "TEMPORARY", "PERSONAL", "INTERNAL_GIT"] or item["git_destination"] in ["EXCLUDED", "EXCLUDED_FROM_GIT_TREE"]:
            excluded_records.append({
                "path": item["absolute_path"],
                "relative_path": item["relative_path"],
                "size_bytes": item["size_bytes"],
                "reason": item["category"],
                "notes": item["notes"]
            })

    # Escribir MASTER_FILE_INVENTORY.csv
    csv_master = OUT_DIR / "MASTER_FILE_INVENTORY.csv"
    with open(csv_master, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=[
            "absolute_path", "relative_path", "size_bytes", "sha256", "extension",
            "category", "canonical_status", "git_destination", "git_lfs",
            "copyright_status", "secret_status", "duplicate_group", "notes"
        ])
        writer.writeheader()
        writer.writerows(all_files)
    print(f"Generado: {csv_master} ({len(all_files)} archivos)")

    # Escribir DUPLICATE_MAP.csv
    csv_dups = OUT_DIR / "DUPLICATE_MAP.csv"
    with open(csv_dups, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=[
            "duplicate_group", "sha256", "size_bytes", "is_canonical",
            "canonical_path", "duplicate_path", "action"
        ])
        writer.writeheader()
        writer.writerows(dup_map_records)
    print(f"Generado: {csv_dups} ({len(dup_map_records)} entradas duplicadas)")

    # Escribir EXCLUDED_FILES.csv
    csv_excl = OUT_DIR / "EXCLUDED_FILES.csv"
    with open(csv_excl, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=["path", "relative_path", "size_bytes", "reason", "notes"])
        writer.writeheader()
        writer.writerows(excluded_records)
    print(f"Generado: {csv_excl} ({len(excluded_records)} archivos excluidos)")

    return all_files, dup_map_records, excluded_records

if __name__ == "__main__":
    scan_roots()
