#!/usr/bin/env python3
"""
OCZ Unreal Light Lab — Generador de Escena Sintética Determinista en USD (.usda)
Fase 4: Geometría plana, bloque cerámico, materiales PBR, luz puntual, luz solar y cámara.
"""

import os
import sys
import json
from pathlib import Path
from scene_contract import (
    create_empty_scene_contract, add_mesh_entry, add_material_entry,
    add_light_entry, add_camera_entry, CoordinateTransform
)

def generate_synthetic_usda(output_usda_path: Path, contract_path: Path):
    """
    Genera una escena USD en formato ASCII (.usda 1.0) con:
    - 1 Plano de suelo (GroundPlane) mate
    - 1 Bloque cerámico (CeramicBlock) reflectante
    - 1 Luz puntual (Navi)
    - 1 Luz direccional (Sun)
    - 1 Cámara de referencia
    """
    scale = 1.0
    ct = CoordinateTransform(scale)

    # 1. Crear contrato JSON
    contract = create_empty_scene_contract("SYNTHETIC_TEST", room_id=0, frame_id=1)
    contract["geometry_manifest"]["usd_file"] = output_usda_path.name

    # Materiales
    add_material_entry(
        contract,
        material_id="MAT_GROUND_STONE_MATTE",
        material_type="STONE",
        base_color=[0.45, 0.45, 0.42],
        roughness=0.85,
        specular=0.25,
        f0=0.010,
        reflectivity=1.0,
        emissive_strength=0.0
    )
    add_material_entry(
        contract,
        material_id="MAT_BLOCK_CERAMIC",
        material_type="CERAMIC",
        base_color=[0.15, 0.55, 0.85],
        roughness=0.28,
        specular=0.90,
        f0=0.036,
        reflectivity=1.0,
        emissive_strength=0.0
    )

    # Geometría en espacio OoT
    # Suelo: Plano 400x400 en Y = 0 (en OoT: X de -200 a 200, Z de -200 a 200)
    add_mesh_entry(
        contract,
        mesh_id="GroundPlane",
        resource_path="synthetic/ground_plane",
        material_id="MAT_GROUND_STONE_MATTE",
        world_pos_oot=[0.0, 0.0, 0.0],
        vertex_count=4,
        index_count=6,
        is_static=True
    )
    # Bloque cerámico: Cubo 60x60x60 centrado en [0.0, 30.0, -80.0] en OoT
    add_mesh_entry(
        contract,
        mesh_id="CeramicBlock",
        resource_path="synthetic/ceramic_block",
        material_id="MAT_BLOCK_CERAMIC",
        world_pos_oot=[0.0, 30.0, -80.0],
        vertex_count=24,
        index_count=36,
        is_static=True
    )

    # Luces
    # Luz Direccional (Sol que baja desde ángulo 45 grados)
    add_light_entry(
        contract,
        light_id="SunDirectionalLight",
        light_type="DIRECTIONAL",
        color_rgb=[1.0, 0.98, 0.90],
        intensity=10000.0,
        pos_oot=[0.0, 500.0, 0.0],
        dir_oot=[0.0, -0.7071, -0.7071],
        source_name="Sun_Exterior"
    )
    # Luz Puntual (Navi flotando a [0.0, 90.0, -80.0] sobre el bloque)
    add_light_entry(
        contract,
        light_id="NaviPointLight",
        light_type="POINT",
        color_rgb=[0.15, 0.55, 1.0],
        intensity=2500.0,
        pos_oot=[0.0, 90.0, -80.0],
        radius=300.0,
        source_name="Navi_Actor"
    )

    # Cámara mirando al bloque desde [0.0, 100.0, 150.0]
    add_camera_entry(
        contract,
        cam_id="ReferenceCamera",
        pos_oot=[0.0, 100.0, 150.0],
        target_oot=[0.0, 30.0, -80.0],
        fov_degrees=60.0
    )

    # Guardar contrato JSON
    with open(contract_path, "w", encoding="utf-8") as f:
        json.dump(contract, f, indent=2)
    print(f"Contrato JSON guardado en: {contract_path}")

    # Coordenadas UE calculadas
    # Suelo (4 vértices): X_ue = -Z_oot, Y_ue = X_oot, Z_ue = Y_oot
    # OoT ground vertices (Y=0):
    # v0: [-200, 0,  200] -> UE: [-200, -200, 0]
    # v1: [ 200, 0,  200] -> UE: [-200,  200, 0]
    # v2: [ 200, 0, -200] -> UE: [ 200,  200, 0]
    # v3: [-200, 0, -200] -> UE: [ 200, -200, 0]

    # Cubo cerámico centrado en [0, 30, -80] OoT -> UE: [80, 0, 30], tamaño 60x60x60
    # X en UE: 50 a 110, Y en UE: -30 a 30, Z en UE: 0 a 60
    bx0, bx1 = 50.0, 110.0
    by0, by1 = -30.0, 30.0
    bz0, bz1 = 0.0, 60.0

    # Contenido USDA 1.0
    usda_content = f"""#usda 1.0
(
    defaultPrim = "OCZ_PhysicalReference"
    metersPerUnit = 0.01
    upAxis = "Z"
)

def Xform "OCZ_PhysicalReference" (
    kind = "assembly"
)
{{
    def Scope "Materials"
    {{
        def Material "Mat_GroundStoneMatte"
        {{
            token outputs:surface.connect = </OCZ_PhysicalReference/Materials/Mat_GroundStoneMatte/PBRShader.outputs:surface>
            def Shader "PBRShader"
            {{
                uniform token info:id = "UsdPreviewSurface"
                color3f inputs:diffuseColor = (0.45, 0.45, 0.42)
                float inputs:roughness = 0.85
                float inputs:metallic = 0.0
                float inputs:specular = 0.25
                token outputs:surface
            }}
        }}

        def Material "Mat_BlockCeramic"
        {{
            token outputs:surface.connect = </OCZ_PhysicalReference/Materials/Mat_BlockCeramic/PBRShader.outputs:surface>
            def Shader "PBRShader"
            {{
                uniform token info:id = "UsdPreviewSurface"
                color3f inputs:diffuseColor = (0.15, 0.55, 0.85)
                float inputs:roughness = 0.28
                float inputs:metallic = 0.0
                float inputs:specular = 0.90
                token outputs:surface
            }}
        }}
    }}

    def Scope "Geometry"
    {{
        def Mesh "GroundPlane" (
            prepend apiSchemas = ["MaterialBindingAPI"]
        )
        {{
            rel material:binding = </OCZ_PhysicalReference/Materials/Mat_GroundStoneMatte>
            int[] faceVertexCounts = [3, 3]
            int[] faceVertexIndices = [0, 1, 2, 0, 2, 3]
            point3f[] points = [
                (-200, -200, 0),
                (-200, 200, 0),
                (200, 200, 0),
                (200, -200, 0)
            ]
            normal3f[] normals = [
                (0, 0, 1),
                (0, 0, 1),
                (0, 0, 1),
                (0, 0, 1)
            ] (
                interpolation = "vertex"
            )
            texCoord2f[] primvars:st = [
                (0, 0),
                (1, 0),
                (1, 1),
                (0, 1)
            ] (
                interpolation = "vertex"
            )
        }}

        def Mesh "CeramicBlock" (
            prepend apiSchemas = ["MaterialBindingAPI"]
        )
        {{
            rel material:binding = </OCZ_PhysicalReference/Materials/Mat_BlockCeramic>
            int[] faceVertexCounts = [4, 4, 4, 4, 4, 4]
            int[] faceVertexIndices = [
                0, 1, 2, 3,     # Top (+Z)
                4, 5, 6, 7,     # Bottom (-Z)
                8, 9, 10, 11,   # Front (+X)
                12, 13, 14, 15, # Back (-X)
                16, 17, 18, 19, # Right (+Y)
                20, 21, 22, 23  # Left (-Y)
            ]
            point3f[] points = [
                # Top (+Z)
                ({bx0}, {by0}, {bz1}), ({bx1}, {by0}, {bz1}), ({bx1}, {by1}, {bz1}), ({bx0}, {by1}, {bz1}),
                # Bottom (-Z)
                ({bx0}, {by1}, {bz0}), ({bx1}, {by1}, {bz0}), ({bx1}, {by0}, {bz0}), ({bx0}, {by0}, {bz0}),
                # Front (+X)
                ({bx1}, {by0}, {bz0}), ({bx1}, {by1}, {bz0}), ({bx1}, {by1}, {bz1}), ({bx1}, {by0}, {bz1}),
                # Back (-X)
                ({bx0}, {by1}, {bz0}), ({bx0}, {by0}, {bz0}), ({bx0}, {by0}, {bz1}), ({bx0}, {by1}, {bz1}),
                # Right (+Y)
                ({bx0}, {by1}, {bz0}), ({bx1}, {by1}, {bz0}), ({bx1}, {by1}, {bz1}), ({bx0}, {by1}, {bz1}),
                # Left (-Y)
                ({bx1}, {by0}, {bz0}), ({bx0}, {by0}, {bz0}), ({bx0}, {by0}, {bz1}), ({bx1}, {by0}, {bz1})
            ]
            normal3f[] normals = [
                (0, 0, 1), (0, 0, 1), (0, 0, 1), (0, 0, 1),
                (0, 0, -1), (0, 0, -1), (0, 0, -1), (0, 0, -1),
                (1, 0, 0), (1, 0, 0), (1, 0, 0), (1, 0, 0),
                (-1, 0, 0), (-1, 0, 0), (-1, 0, 0), (-1, 0, 0),
                (0, 1, 0), (0, 1, 0), (0, 1, 0), (0, 1, 0),
                (0, -1, 0), (0, -1, 0), (0, -1, 0), (0, -1, 0)
            ] (
                interpolation = "vertex"
            )
        }}
    }}

    def Scope "Lights"
    {{
        def SphereLight "NaviPointLight"
        {{
            float inputs:intensity = 2500.0
            float inputs:radius = 15.0
            color3f inputs:color = (0.15, 0.55, 1.0)
            double3 xformOp:translate = (80.0, 0.0, 90.0)
            uniform token[] xformOpOrder = ["xformOp:translate"]
        }}

        def DistantLight "SunDirectionalLight"
        {{
            float inputs:intensity = 10000.0
            color3f inputs:color = (1.0, 0.98, 0.90)
            float inputs:angle = 0.53
            float3 xformOp:rotateXYZ = (-45.0, 0.0, 0.0)
            uniform token[] xformOpOrder = ["xformOp:rotateXYZ"]
        }}
    }}

    def Scope "Cameras"
    {{
        def Camera "ReferenceCamera"
        {{
            float focalLength = 35.0
            float horizontalAperture = 36.0
            float verticalAperture = 24.0
            float2 clippingRange = (10.0, 12800.0)
            double3 xformOp:translate = (-150.0, 0.0, 100.0)
            float3 xformOp:rotateXYZ = (-15.0, 0.0, 0.0)
            uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateXYZ"]
        }}
    }}
}}
"""
    with open(output_usda_path, "w", encoding="utf-8") as f:
        f.write(usda_content)
    print(f"Escena USDA generada exitosamente en: {output_usda_path}")
    return contract

if __name__ == "__main__":
    base_dir = Path(__file__).parent / "synthetic_test"
    base_dir.mkdir(parents=True, exist_ok=True)
    usda_p = base_dir / "synthetic_reference_scene.usda"
    json_p = base_dir / "synthetic_reference_contract.json"
    generate_synthetic_usda(usda_p, json_p)
