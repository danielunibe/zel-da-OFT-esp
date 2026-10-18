#!/usr/bin/env python3
"""
OCZ Unreal Light Lab — Detector de Entorno y Capacidades Unreal Engine 5.8
Fase 0: Detección exhaustiva de instalación, ejecutables, plugins y hardware.
"""

import os
import sys
import json
import subprocess
import winreg
from pathlib import Path

def get_registry_value(root_key, subkey, value_name):
    try:
        with winreg.OpenKey(root_key, subkey) as key:
            val, _ = winreg.QueryValueEx(key, value_name)
            return val
    except Exception:
        return None

def scan_candidate_paths():
    candidates = [
        r"C:\Program Files\Epic Games\UE_5.8",
        r"D:\Program Files\Epic Games\UE_5.8",
        r"E:\Program Files\Epic Games\UE_5.8",
        r"C:\Epic Games\UE_5.8",
        r"D:\Epic Games\UE_5.8"
    ]
    # También consultar registro
    reg_dir = get_registry_value(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\EpicGames\Unreal Engine\5.8", "InstalledDirectory")
    if reg_dir and reg_dir not in candidates:
        candidates.insert(0, reg_dir)

    for c in candidates:
        if os.path.exists(c) and os.path.isdir(c):
            return os.path.abspath(c)
    return None

def detect_unreal_engine():
    ue_root = scan_candidate_paths()
    if not ue_root:
        return {
            "status": "NOT_FOUND",
            "installed": False,
            "error": "No se encontró el directorio de instalación de Unreal Engine 5.8"
        }

    build_version_path = os.path.join(ue_root, "Engine", "Build", "Build.version")
    version_info = {}
    if os.path.exists(build_version_path):
        try:
            with open(build_version_path, "r", encoding="utf-8") as f:
                version_info = json.load(f)
        except Exception as e:
            version_info = {"read_error": str(e)}

    bin_dir = os.path.join(ue_root, "Engine", "Binaries", "Win64")
    editor_exe = os.path.join(bin_dir, "UnrealEditor.exe")
    cmd_exe = os.path.join(bin_dir, "UnrealEditor-Cmd.exe")
    embedded_python = os.path.join(ue_root, "Engine", "Binaries", "ThirdParty", "Python3", "Win64", "python.exe")

    # Plugins inspeccionados
    plugins_base = os.path.join(ue_root, "Engine", "Plugins")
    target_plugins = {
        "USDImporter": os.path.join(plugins_base, "Importers", "USDImporter", "USDImporter.uplugin"),
        "USDCore": os.path.join(plugins_base, "Runtime", "USDCore", "USDCore.uplugin"),
        "PythonScriptPlugin": os.path.join(plugins_base, "Experimental", "PythonScriptPlugin", "PythonScriptPlugin.uplugin"),
        "EditorScriptingUtilities": os.path.join(plugins_base, "Editor", "EditorScriptingUtilities", "EditorScriptingUtilities.uplugin")
    }

    plugins_status = {}
    for name, p_path in target_plugins.items():
        if os.path.exists(p_path):
            try:
                with open(p_path, "r", encoding="utf-8") as pf:
                    uplugin_data = json.load(pf)
                plugins_status[name] = {
                    "available": True,
                    "path": p_path,
                    "version": uplugin_data.get("VersionName", "unknown"),
                    "friendlyName": uplugin_data.get("FriendlyName", name),
                    "enabledByDefault": uplugin_data.get("EnabledByDefault", False)
                }
            except Exception as e:
                plugins_status[name] = {"available": True, "path": p_path, "error": str(e)}
        else:
            plugins_status[name] = {"available": False, "path": p_path}

    # Python version test
    python_ver_str = "NOT_AVAILABLE"
    if os.path.exists(embedded_python):
        try:
            out = subprocess.check_output([embedded_python, "--version"], text=True, stderr=subprocess.STDOUT)
            python_ver_str = out.strip()
        except Exception as e:
            python_ver_str = f"Error: {e}"

    # Verificación de completitud de instalación
    is_complete = (
        os.path.exists(editor_exe) and
        os.path.exists(cmd_exe) and
        plugins_status.get("USDImporter", {}).get("available", False) and
        plugins_status.get("PythonScriptPlugin", {}).get("available", False)
    )

    return {
        "status": "READY" if is_complete else "PARTIAL_OR_INSTALLING",
        "installed": True,
        "is_complete": is_complete,
        "engine_root": ue_root,
        "build_version": version_info,
        "version_string": f"{version_info.get('MajorVersion', 5)}.{version_info.get('MinorVersion', 8)}.{version_info.get('PatchVersion', 0)}",
        "changelist": version_info.get("Changelist", 0),
        "branch": version_info.get("BranchName", ""),
        "editor_exe": editor_exe if os.path.exists(editor_exe) else None,
        "editor_cmd_exe": cmd_exe if os.path.exists(cmd_exe) else None,
        "embedded_python_exe": embedded_python if os.path.exists(embedded_python) else None,
        "embedded_python_version": python_ver_str,
        "plugins": plugins_status
    }

def detect_system_hardware():
    hw = {}
    # Memoria y discos
    try:
        import ctypes
        class MEMORYSTATUSEX(ctypes.Structure):
            _fields_ = [
                ("dwLength", ctypes.c_ulong),
                ("dwMemoryLoad", ctypes.c_ulong),
                ("ullTotalPhys", ctypes.c_ulonglong),
                ("ullAvailPhys", ctypes.c_ulonglong),
                ("ullTotalPageFile", ctypes.c_ulonglong),
                ("ullAvailPageFile", ctypes.c_ulonglong),
                ("ullTotalVirtual", ctypes.c_ulonglong),
                ("ullAvailVirtual", ctypes.c_ulonglong),
                ("ullAvailExtendedVirtual", ctypes.c_ulonglong),
            ]
        mem = MEMORYSTATUSEX()
        mem.dwLength = ctypes.sizeof(MEMORYSTATUSEX)
        ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(mem))
        hw["ram_total_gb"] = round(mem.ullTotalPhys / (1024**3), 2)
        hw["ram_avail_gb"] = round(mem.ullAvailPhys / (1024**3), 2)
    except Exception as e:
        hw["ram_error"] = str(e)

    # Espacio en disco C:
    try:
        import shutil
        total, used, free = shutil.disk_usage("C:")
        hw["disk_c_total_gb"] = round(total / (1024**3), 2)
        hw["disk_c_free_gb"] = round(free / (1024**3), 2)
    except Exception as e:
        hw["disk_error"] = str(e)

    # GPU vía nvidia-smi
    gpu_info = {"available": False}
    try:
        res = subprocess.run(
            ["nvidia-smi", "--query-gpu=name,memory.total,driver_version", "--format=csv,noheader"],
            capture_output=True, text=True, check=True
        )
        line = res.stdout.strip().split(",")
        if len(line) >= 3:
            gpu_info = {
                "available": True,
                "name": line[0].strip(),
                "memory_total": line[1].strip(),
                "driver_version": line[2].strip()
            }
    except Exception as e:
        gpu_info["error"] = str(e)
    hw["gpu"] = gpu_info

    return hw

def main():
    print("=" * 70)
    print("OCZ-UE-01: DETECTOR DE ENTORNO UNREAL ENGINE 5.8 & HARDWARE")
    print("=" * 70)

    ue_env = detect_unreal_engine()
    hw_info = detect_system_hardware()

    consolidated = {
        "timestamp": str(subprocess.check_output(["powershell", "-NoProfile", "-Command", "Get-Date -Format s"], text=True).strip()),
        "unreal_engine": ue_env,
        "system_hardware": hw_info
    }

    out_file = os.path.join(os.path.dirname(__file__), "unreal_environment.json")
    with open(out_file, "w", encoding="utf-8") as f:
        json.dump(consolidated, f, indent=2)

    print(f"Estado de Unreal Engine: {ue_env.get('status')}")
    print(f"Versión: {ue_env.get('version_string')} (Changelist: {ue_env.get('changelist')})")
    print(f"Ruta Motor: {ue_env.get('engine_root')}")
    print(f"Editor Cmd: {ue_env.get('editor_cmd_exe')}")
    print(f"Python: {ue_env.get('embedded_python_version')}")
    print("\nPlugins verificados:")
    for p_name, p_data in ue_env.get("plugins", {}).items():
        avail = "PASS" if p_data.get("available") else "FAIL"
        print(f"  - {p_name:<25}: [{avail}] ({p_data.get('friendlyName', '')})")

    print(f"\nHardware del Sistema:")
    print(f"  - RAM: {hw_info.get('ram_total_gb')} GB (Libre: {hw_info.get('ram_avail_gb')} GB)")
    print(f"  - Disco C: Libre {hw_info.get('disk_c_free_gb')} GB de {hw_info.get('disk_c_total_gb')} GB")
    gpu = hw_info.get("gpu", {})
    if gpu.get("available"):
        print(f"  - GPU: {gpu.get('name')} ({gpu.get('memory_total')}, Driver {gpu.get('driver_version')})")

    print(f"\nReporte guardado en: {out_file}")
    print("=" * 70)

if __name__ == "__main__":
    main()
