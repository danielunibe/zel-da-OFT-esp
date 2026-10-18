#!/usr/bin/env python3
"""
OCZ Unreal Light Lab — Contrato de Interoperabilidad de Escena
Couch Edition RTX (D3D12/DXR) <---> Unreal Engine 5.8.3
Fase 3: Definición del esquema JSON y conversor de coordenadas matemáticas.
"""

import json
from typing import Dict, List, Any, Optional

SCHEMA_VERSION = "1.0.0"

class CoordinateTransform:
    """
    Conversión matemática de coordenadas entre Ocarina of Time (Fast3D) y Unreal Engine.
    OoT:  +X Derecha, +Y Arriba, +Z Hacia observador (Mano Derecha, unidades OoT)
    UE5:  +X Adelante, +Y Derecha, +Z Arriba (Mano Izquierda, Centímetros)
    """
    def __init__(self, scale_factor: float = 1.0):
        self.scale_factor = scale_factor

    def oot_to_ue_position(self, pos: List[float]) -> List[float]:
        """(x_oot, y_oot, z_oot) -> (X_ue, Y_ue, Z_ue)"""
        x, y, z = pos[0], pos[1], pos[2]
        return [
            round(-z * self.scale_factor, 4),
            round(x * self.scale_factor, 4),
            round(y * self.scale_factor, 4)
        ]

    def ue_to_oot_position(self, pos: List[float]) -> List[float]:
        """(X_ue, Y_ue, Z_ue) -> (x_oot, y_oot, z_oot)"""
        X, Y, Z = pos[0], pos[1], pos[2]
        inv_scale = 1.0 / self.scale_factor if self.scale_factor != 0 else 1.0
        return [
            round(Y * inv_scale, 4),
            round(Z * inv_scale, 4),
            round(-X * inv_scale, 4)
        ]

    def oot_to_ue_direction(self, dir_vec: List[float]) -> List[float]:
        """Convierte vector normal o de dirección sin escala"""
        x, y, z = dir_vec[0], dir_vec[1], dir_vec[2]
        return [round(-z, 5), round(x, 5), round(y, 5)]

    def oot_to_ue_rotation(self, pitch_oot: float, yaw_oot: float, roll_oot: float) -> List[float]:
        """
        Conversión de ángulos de Euler (grados).
        Retorna [Pitch_ue, Yaw_ue, Roll_ue]
        """
        # Pitch en UE es rotación alrededor de Y (inclinación arriba/abajo)
        # Yaw en UE es rotación alrededor de Z (giro horizontal)
        # Roll en UE es rotación alrededor de X (inclinación lateral)
        return [round(pitch_oot, 3), round(yaw_oot - 90.0, 3), round(roll_oot, 3)]

def create_empty_scene_contract(scene_id: str, room_id: int, frame_id: int) -> Dict[str, Any]:
    """Crea una estructura de contrato inicial vacía y tipada."""
    return {
        "schema_version": SCHEMA_VERSION,
        "contract_id": f"OCZ_CONTRACT_{scene_id}_R{room_id}_F{frame_id}",
        "metadata": {
            "source_engine": "Ocarina Couch Edition RTX (D3D12/DXR)",
            "target_engine": "Unreal Engine 5.8.3",
            "scene_id": scene_id,
            "room_id": room_id,
            "frame_id": frame_id,
            "timestamp": None
        },
        "coordinate_system": {
            "source_space": "Fast3D_OoT (Right-handed, Y-up)",
            "target_space": "Unreal_Engine_5 (Left-handed, Z-up, cm)",
            "scale_factor": 1.0,
            "axis_mapping": {
                "X_ue": "-Z_oot",
                "Y_ue": "+X_oot",
                "Z_ue": "+Y_oot"
            }
        },
        "geometry_manifest": {
            "usd_file": None,
            "meshes": []
        },
        "materials": {},
        "lights": [],
        "cameras": []
    }

def add_mesh_entry(contract: Dict[str, Any], mesh_id: str, resource_path: str, material_id: str,
                   world_pos_oot: List[float], vertex_count: int, index_count: int, is_static: bool = True):
    ct = CoordinateTransform(contract["coordinate_system"]["scale_factor"])
    ue_pos = ct.oot_to_ue_position(world_pos_oot)
    contract["geometry_manifest"]["meshes"].append({
        "mesh_id": mesh_id,
        "resource_path": resource_path,
        "material_id": material_id,
        "is_static": is_static,
        "vertex_count": vertex_count,
        "index_count": index_count,
        "transform_oot": {
            "position": world_pos_oot,
            "rotation_deg": [0.0, 0.0, 0.0],
            "scale": [1.0, 1.0, 1.0]
        },
        "transform_ue": {
            "position": ue_pos,
            "rotation_deg": [0.0, 0.0, 0.0],
            "scale": [1.0, 1.0, 1.0]
        }
    })

def add_material_entry(contract: Dict[str, Any], material_id: str, material_type: str,
                       base_color: List[float], roughness: float, specular: float, f0: float,
                       reflectivity: float = 1.0, emissive_strength: float = 0.0,
                       ior: float = 1.0, alpha_mode: int = 0, texture_path: Optional[str] = None):
    contract["materials"][material_id] = {
        "material_id": material_id,
        "material_type": material_type,
        "base_color": base_color,
        "texture_path": texture_path,
        "roughness": roughness,
        "specular": specular,
        "f0": f0,
        "reflectivity": reflectivity,
        "emissive_strength": emissive_strength,
        "ior": ior,
        "alpha_mode": alpha_mode,
        "confidence": "PHYSICAL_VERIFIED",
        "optical_model": "Fresnel_Schlick_GGX"
    }

def add_light_entry(contract: Dict[str, Any], light_id: str, light_type: str,
                    color_rgb: List[float], intensity: float, pos_oot: List[float],
                    dir_oot: Optional[List[float]] = None, radius: float = 500.0,
                    source_name: str = "Native"):
    ct = CoordinateTransform(contract["coordinate_system"]["scale_factor"])
    ue_pos = ct.oot_to_ue_position(pos_oot)
    ue_dir = ct.oot_to_ue_direction(dir_oot) if dir_oot else None

    contract["lights"].append({
        "light_id": light_id,
        "type": light_type, # DIRECTIONAL, POINT, SPOT
        "source_name": source_name,
        "color_rgb": color_rgb,
        "intensity": intensity,
        "attenuation_radius": radius * contract["coordinate_system"]["scale_factor"],
        "position_oot": pos_oot,
        "direction_oot": dir_oot,
        "position_ue": ue_pos,
        "direction_ue": ue_dir
    })

def add_camera_entry(contract: Dict[str, Any], cam_id: str, pos_oot: List[float],
                     target_oot: List[float], fov_degrees: float = 60.0):
    ct = CoordinateTransform(contract["coordinate_system"]["scale_factor"])
    ue_pos = ct.oot_to_ue_position(pos_oot)
    ue_target = ct.oot_to_ue_position(target_oot)

    contract["cameras"].append({
        "camera_id": cam_id,
        "fov_degrees": fov_degrees,
        "aspect_ratio": 4.0 / 3.0,
        "near_clip": 10.0,
        "far_clip": 12800.0,
        "position_oot": pos_oot,
        "target_oot": target_oot,
        "position_ue": ue_pos,
        "target_ue": ue_target
    })

if __name__ == "__main__":
    # Test rápido de validación del conversor
    ct = CoordinateTransform(1.0)
    pos_oot = [100.0, 50.0, -200.0]
    pos_ue = ct.oot_to_ue_position(pos_oot)
    pos_back = ct.ue_to_oot_position(pos_ue)
    assert pos_oot == pos_back, f"Fallo de reversibilidad: {pos_oot} != {pos_back}"
    print(f"Test Coordenadas OoT {pos_oot} -> UE {pos_ue} -> Reversible OK.")
