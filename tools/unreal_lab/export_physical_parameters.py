"""
OCZ_LightLab_UE58 - Physical Parameter Return Exporter (Unreal -> Couch Edition)
Compatible con Couch Edition RTX (D3D12/DXR)

Genera el contrato de retorno declarativo, versionado y reversible
con los parametros fisicos verificados en el laboratorio Unreal Engine 5.8.3.

No sobrescribe reglas de juego en tiempo de ejecucion directamente.
Genera un contrato estructurado en JSON listo para ingestion por MaterialRegistry / DxrSurfaceRenderer.
"""

import os
import sys
import json
from datetime import datetime


def export_physical_parameters_contract(output_path=None):
    if output_path is None:
        output_path = os.path.abspath(
            os.path.join(os.path.dirname(__file__), "output", "ocz_pbr_rules_return.json")
        )

    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    contract = {
        "schema_version": "1.0.0",
        "return_id": "OCZ_UE58_RETURN_CALIBRATION_R1",
        "timestamp": datetime.now().isoformat(),
        "origin": {
            "laboratory": "OCZ_LightLab_UE58",
            "engine": "Unreal Engine 5.8.3",
            "purpose": "PHYSICAL_REFERENCE_CALIBRATION",
            "source_game": "Ocarina Couch Edition RTX",
            "renderer_target": "D3D12/DXR (DxrSurfaceRenderer.cpp + dxr_shadow.hlsl)"
        },
        "optical_conventions": {
            "fresnel_model": "Schlick_GGX",
            "specular_f0_relation": "F0 = 0.08 * Specular_UE",
            "roughness_model": "PerceptualRoughness_Squared (alpha = roughness^2)",
            "diffuse_model": "Lambertian_EnergyConserving",
            "units": {
                "distance": "centimeters",
                "light_intensity": "Lumens / Candelas",
                "attenuation": "Inverse_Square_Physical"
            }
        },
        "surfaces": [
            {
                "material_id": "MAT_TEMPLE_MOSAIC_CERAMIC",
                "surface_type": "CERAMIC",
                "scene_id": "TOKINOMA_TEMPLE_OF_TIME",
                "base_color_rgb": [0.20, 0.45, 0.70],
                "roughness": 0.28,
                "f0": 0.036,
                "specular_ue": 0.45,
                "metallic": 0.0,
                "emission_nits": 0.0,
                "transmission": 0.0,
                "ior": 1.48,
                "provenance_tier": "CALIBRATED_LAB_VERIFIED",
                "validation_evidence": "UE5.8 Lumen / D3D12 GPU render confirms 3.6% F0 specular glaze without energy blowout",
                "risk_assessment": {
                    "regression_risk": "LOW",
                    "rollback_procedure": "Remove rule from MaterialRegistry JSON overlay; reverts to default fallback"
                }
            },
            {
                "material_id": "MAT_TEMPLE_WALL_MAIN",
                "surface_type": "STONE",
                "scene_id": "TOKINOMA_TEMPLE_OF_TIME",
                "base_color_rgb": [0.45, 0.45, 0.42],
                "roughness": 0.85,
                "f0": 0.010,
                "specular_ue": 0.125,
                "metallic": 0.0,
                "emission_nits": 0.0,
                "transmission": 0.0,
                "ior": 1.0,
                "provenance_tier": "CALIBRATED_LAB_VERIFIED",
                "validation_evidence": "Eliminates unnatural plastic sheen on temple masonry walls and octagonal columns",
                "risk_assessment": {
                    "regression_risk": "LOW",
                    "rollback_procedure": "Revert to default stone roughness 0.80"
                }
            },
            {
                "material_id": "MAT_TEMPLE_WINDOW_BEAM",
                "surface_type": "TRANSLUCENT_DECORATIVE",
                "scene_id": "TOKINOMA_TEMPLE_OF_TIME",
                "base_texture": "tokinoma_room_0Tex_0161D8",
                "semantic_role": "DECORATIVE_BEAM_TRANSLUCENT",
                "roughness": 0.50,
                "f0": 0.040,
                "metallic": 0.0,
                "provenance_tier": "GAME_ORIGINAL_CLASSIFIED",
                "validation_evidence": "Must remain additive / non-occluding; excluded from solid ray tracing shadow casters",
                "risk_assessment": {
                    "regression_risk": "NONE",
                    "rollback_procedure": "Preserve as unlit / additive blend quad"
                }
            },
            {
                "material_id": "MAT_HYLIA_WATER_SURFACE",
                "surface_type": "WATER",
                "scene_id": "LAKE_HYLIA",
                "base_color_rgb": [0.08, 0.25, 0.45],
                "roughness": 0.03,
                "f0": 0.020,
                "specular_ue": 0.25,
                "metallic": 0.0,
                "emission_nits": 0.0,
                "transmission": 0.95,
                "ior": 1.333,
                "provenance_tier": "CALIBRATED_LAB_VERIFIED",
                "validation_evidence": "Fresnel Schlick dielectric with n=1.333, preserving 98% water texture at normal angles",
                "risk_assessment": {
                    "regression_risk": "LOW",
                    "rollback_procedure": "Preserves water dual-layer composite"
                }
            }
        ],
        "lights": [
            {
                "light_id": "NAVI_ELECTRIC_BLUE_SOLE_LIGHT",
                "light_type": "POINT",
                "source_actor": "Navi",
                "color_rgb": [0.15, 0.55, 1.00],
                "intensity_lumens": 2500.0,
                "attenuation_radius_cm": 350.0,
                "position_relative_link": [0.0, 45.0, 15.0],
                "provenance_tier": "EXPERIMENTAL_TEST_ONLY",
                "can_auto_overwrite_production": False,
                "validation_evidence": "Calibrated in UE 5.8 lab. Production Couch Edition uses relative game intensity; do not auto-inject lumens to production DXR"
            },
            {
                "light_id": "TOKINOMA_SUNLIGHT_CLERESTORY",
                "light_type": "DIRECTIONAL",
                "source_actor": "Sun_Exterior",
                "color_rgb": [1.00, 0.96, 0.88],
                "intensity_lux": 15000.0,
                "direction_vector": [0.25, -0.65, -0.71],
                "provenance_tier": "EXPERIMENTAL_TEST_ONLY",
                "can_auto_overwrite_production": False,
                "validation_evidence": "Physical lab lighting for clerestory window rays; keep separate from native game environment lights"
            }
        ],
        "production_guardrails": {
            "can_auto_overwrite_production_registry": False,
            "requires_human_approval": True,
            "recompile_game_required": False,
            "status": "PROPOSED_REFERENCE_CONTRACT"
        },
        "couch_integration_guide": {
            "status": "PROPOSAL_VALIDATED_FOR_FUTURE_INTAKE",
            "consumer_component": "Fast::MaterialRegistry",
            "direct_injection_method": "MaterialRegistry::LoadCalibratedRulesJson (Deferred)",
            "modifications_to_original_game": "NONE (External declarative contract)"
        }
    }

    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(contract, f, indent=2)

    print(f"[PHYSICAL EXPORT] Contrato de retorno guardado en: {output_path}")

    # Tambien guardar una copia en reports/unreal_lab/
    report_copy = os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", "..", "reports", "unreal_lab", "ocz_pbr_physical_contract_return.json")
    )
    os.makedirs(os.path.dirname(report_copy), exist_ok=True)
    with open(report_copy, "w", encoding="utf-8") as f:
        json.dump(contract, f, indent=2)
    print(f"[PHYSICAL EXPORT] Copia de reporte guardada en: {report_copy}")

    return contract


if __name__ == "__main__":
    export_physical_parameters_contract()
