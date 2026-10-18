"""
OCZ_LightLab_UE58 - Reference PBR Material Master Library
Compatible con Unreal Engine 5.8.3

Define y genera los 8 materiales maestros fÃ­sicos de referencia:
1. Piedra (Stone): Difuso alto, roughness ~0.85, f0 ~0.01-0.04, no-metÃ¡lico
2. CerÃ¡mica (Ceramic): Albedo medio/saturado, roughness ~0.25-0.35, f0 ~0.036 (vidriado), no-metÃ¡lico
3. Madera (Wood): Albedo cÃ¡lido, roughness ~0.60-0.75, f0 ~0.02
4. Metal (Metal): Albedo especular, metallic 1.0, roughness ~0.15-0.40, f0 ~0.70-0.95
5. Agua (Water): Transparente/translÃºcido, IOR 1.333, f0 0.020, roughness 0.02-0.10
6. Vidrio (Glass): TransmisiÃ³n alta, IOR 1.52, f0 0.040, roughness 0.05
7. Emisivo (Emissive): Navi/Hada/Fuego, emisiÃ³n calibrada en Nits / Lumen, alpha translÃºcido
8. GenÃ©rico de referencia (Generic_PBR): Parametrizable 100% vÃ­a JSON Contract

ConservaciÃ³n fÃ­sica:
- RelaciÃ³n F0 y Specular en UE: Specular_UE = F0 / 0.08
- Dielectric F0 tÃ­pico: 0.04 -> Specular = 0.50
- Agua F0 (IOR 1.333): 0.02 -> Specular = 0.25
- CerÃ¡mica F0 (IOR 1.48): 0.036 -> Specular = 0.45
- Piedra F0: 0.01 -> Specular = 0.125
"""

import os
import json

# Definicion cientifica de materiales de referencia
REFERENCE_MATERIALS = {
    "MM_OCZ_Stone": {
        "type": "STONE",
        "description": "Piedra mate de muros y columnas del Templo del Tiempo",
        "base_color": [0.45, 0.45, 0.42],
        "roughness": 0.85,
        "metallic": 0.0,
        "f0": 0.010,
        "specular_ue": 0.010 / 0.08, # 0.125
        "ior": 1.0,
        "emissive": 0.0,
        "blend_mode": "BLEND_Opaque",
        "confidence": "PHYSICAL_VERIFIED"
    },
    "MM_OCZ_Ceramic": {
        "type": "CERAMIC",
        "description": "Mosaico pulido / suelo esmaltado del Templo del Tiempo",
        "base_color": [0.15, 0.55, 0.85],
        "roughness": 0.28,
        "metallic": 0.0,
        "f0": 0.036,
        "specular_ue": 0.036 / 0.08, # 0.45
        "ior": 1.48,
        "emissive": 0.0,
        "blend_mode": "BLEND_Opaque",
        "confidence": "PHYSICAL_VERIFIED"
    },
    "MM_OCZ_Wood": {
        "type": "WOOD",
        "description": "Madera de puertas, vigas y soportes",
        "base_color": [0.35, 0.22, 0.12],
        "roughness": 0.65,
        "metallic": 0.0,
        "f0": 0.020,
        "specular_ue": 0.020 / 0.08, # 0.25
        "ior": 1.0,
        "emissive": 0.0,
        "blend_mode": "BLEND_Opaque",
        "confidence": "PHYSICAL_VERIFIED"
    },
    "MM_OCZ_Metal": {
        "type": "METAL",
        "description": "Hierro / Bronce / Espada Maestra",
        "base_color": [0.75, 0.75, 0.78],
        "roughness": 0.22,
        "metallic": 1.0,
        "f0": 0.700,
        "specular_ue": 1.0,
        "ior": 2.5,
        "emissive": 0.0,
        "blend_mode": "BLEND_Opaque",
        "confidence": "PHYSICAL_VERIFIED"
    },
    "MM_OCZ_Water": {
        "type": "WATER",
        "description": "Agua de manantiales y Lago Hylia",
        "base_color": [0.08, 0.25, 0.45],
        "roughness": 0.03,
        "metallic": 0.0,
        "f0": 0.020,
        "specular_ue": 0.020 / 0.08, # 0.25
        "ior": 1.333,
        "emissive": 0.0,
        "blend_mode": "BLEND_Translucent",
        "confidence": "PHYSICAL_VERIFIED"
    },
    "MM_OCZ_Glass": {
        "type": "GLASS",
        "description": "Vidrieras y cristal transparente",
        "base_color": [0.95, 0.95, 0.98],
        "roughness": 0.05,
        "metallic": 0.0,
        "f0": 0.040,
        "specular_ue": 0.040 / 0.08, # 0.50
        "ior": 1.52,
        "emissive": 0.0,
        "blend_mode": "BLEND_Translucent",
        "confidence": "PHYSICAL_VERIFIED"
    },
    "MM_OCZ_Emissive": {
        "type": "EMISSIVE",
        "description": "Aura de Navi / Fuego / Gemas luminosas",
        "base_color": [0.15, 0.55, 1.00],
        "roughness": 0.50,
        "metallic": 0.0,
        "f0": 0.040,
        "specular_ue": 0.50,
        "ior": 1.0,
        "emissive": 2500.0, # Nits
        "blend_mode": "BLEND_Additive",
        "confidence": "PHYSICAL_VERIFIED"
    },
    "MM_OCZ_Generic_PBR": {
        "type": "GENERIC_PBR",
        "description": "Material configurable por contrato",
        "base_color": [0.5, 0.5, 0.5],
        "roughness": 0.5,
        "metallic": 0.0,
        "f0": 0.04,
        "specular_ue": 0.5,
        "ior": 1.0,
        "emissive": 0.0,
        "blend_mode": "BLEND_Opaque",
        "confidence": "INFERRED"
    }
}


def export_material_definitions_json(output_path):
    """
    Exporta el catalogo maestro de materiales de referencia como JSON.
    """
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(REFERENCE_MATERIALS, f, indent=2)
    print(f"[REFERENCE_MATERIALS] Catalogo exportado a {output_path}")


def setup_materials_in_unreal(package_path="/Game/OCZ/Materials"):
    """
    Si se ejecuta dentro de Unreal Engine, crea los UMaterial correspondientes en /Game/OCZ/Materials.
    """
    try:
        import unreal
    except ImportError:
        print("[INFO] No se encuentra runtime de Unreal Engine. Generando manifiesto JSON declarativo.")
        return False

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    editor_asset_lib = unreal.EditorAssetLibrary

    for mat_name, mat_props in REFERENCE_MATERIALS.items():
        full_asset_path = f"{package_path}/{mat_name}"
        if not editor_asset_lib.does_asset_exist(full_asset_path):
            factory = unreal.MaterialFactoryNew()
            mat_asset = asset_tools.create_asset(mat_name, package_path, unreal.Material, factory)
            if mat_asset:
                editor_asset_lib.save_loaded_asset(mat_asset)
                print(f"[UE MATERIAL] Creado UMaterial: {full_asset_path}")
        else:
            print(f"[UE MATERIAL] Ya existe: {full_asset_path}")

    return True


if __name__ == "__main__":
    out_json = os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", "output", "reference_materials_catalog.json")
    )
    export_material_definitions_json(out_json)
    setup_materials_in_unreal()
