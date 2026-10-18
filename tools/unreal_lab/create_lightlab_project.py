#!/usr/bin/env python3
"""
OCZ Unreal Light Lab — Creador del Proyecto Experimental OCZ_LightLab_UE58
Fase 1: Estructuración del proyecto .uproject, configs D3D12, plugins y carpetas.
"""

import os
import sys
import json
from pathlib import Path

def create_lightlab_project(target_dir=None):
    if not target_dir:
        docs_dir = os.path.join(os.environ.get("USERPROFILE", "C:\\Users\\danie"), "Documents", "Unreal Projects")
        target_dir = os.path.join(docs_dir, "OCZ_LightLab_UE58")

    project_dir = Path(target_dir).resolve()
    print(f"Configurando proyecto en: {project_dir}")

    # 1. Crear directorios base
    content_dir = project_dir / "Content"
    config_dir = project_dir / "Config"

    # Subcarpetas requeridas
    categories = [
        "Scenes",
        "Materials",
        "Textures",
        "Lights",
        "Cameras",
        "Tests"
    ]
    for cat in categories:
        (content_dir / "OCZ" / cat).mkdir(parents=True, exist_ok=True)

    config_dir.mkdir(parents=True, exist_ok=True)

    # 2. Generar OCZ_LightLab_UE58.uproject
    uproject_data = {
        "FileVersion": 3,
        "EngineAssociation": "5.8",
        "Category": "Graphics Lab",
        "Description": "OCZ Unreal Light Lab — Laboratorio de Referencia Física y Visual para Ocarina Couch Edition RTX",
        "Plugins": [
            {
                "Name": "PythonScriptPlugin",
                "Enabled": True
            },
            {
                "Name": "EditorScriptingUtilities",
                "Enabled": True
            },
            {
                "Name": "USDImporter",
                "Enabled": True
            },
            {
                "Name": "USDCore",
                "Enabled": True
            }
        ]
    }

    uproject_file = project_dir / "OCZ_LightLab_UE58.uproject"
    with open(uproject_file, "w", encoding="utf-8") as f:
        json.dump(uproject_data, f, indent=2)
    print(f"Generado: {uproject_file}")

    # 3. Config/DefaultEngine.ini
    default_engine_ini = """[URL]
GameName=OCZ_LightLab_UE58

[/Script/EngineSettings.GameMapsSettings]
EditorStartupMap=/Game/OCZ/Tests/LV_OCZ_PhysicalReference
GameDefaultMap=/Game/OCZ/Tests/LV_OCZ_PhysicalReference

[/Script/WindowsTargetPlatform.WindowsTargetSettings]
DefaultGraphicsRHI=DefaultGraphicsRHI_DX12

[/Script/Engine.RendererSettings]
r.DefaultFeature.AntiAliasing=2
r.Shadow.Virtual.Enable=1
r.Lumen.DiffuseIndirect.Allow=1
r.Lumen.Reflections.Allow=1
r.RayTracing=1
r.D3D12.TexturePool=1
"""
    with open(config_dir / "DefaultEngine.ini", "w", encoding="utf-8") as f:
        f.write(default_engine_ini)
    print(f"Generado: {config_dir / 'DefaultEngine.ini'}")

    # 4. Config/DefaultEditor.ini (Habilitar Python Scripting Developer Mode)
    default_editor_ini = """[/Script/PythonScriptPlugin.PythonScriptPluginSettings]
bDeveloperMode=True
bRemoteExecution=True

[/Script/UnrealEd.EditorEngine]
+EditPackages=PythonScriptPlugin
+EditPackages=EditorScriptingUtilities
+EditPackages=USDImporter
"""
    with open(config_dir / "DefaultEditor.ini", "w", encoding="utf-8") as f:
        f.write(default_editor_ini)
    print(f"Generado: {config_dir / 'DefaultEditor.ini'}")

    # 5. Guardar info del proyecto en tools/unreal_lab/project_info.json
    info = {
        "project_name": "OCZ_LightLab_UE58",
        "uproject_path": str(uproject_file),
        "project_dir": str(project_dir),
        "content_dir": str(content_dir),
        "engine_version": "5.8",
        "initial_map": "/Game/OCZ/Tests/LV_OCZ_PhysicalReference",
        "categories": [f"/Game/OCZ/{c}" for c in categories],
        "plugins": [p["Name"] for p in uproject_data["Plugins"]]
    }

    info_path = Path(__file__).parent / "project_info.json"
    with open(info_path, "w", encoding="utf-8") as f:
        json.dump(info, f, indent=2)
    print(f"Guardado project_info en: {info_path}")

    return info

if __name__ == "__main__":
    create_lightlab_project()
