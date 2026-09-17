# PBR-Lite Foundation — V03

## MaterialDefinition Structure
```cpp
struct MaterialDefinition {
    MaterialType type;          // classification
    float roughness;            // 0..1
    float metallic;             // 0..1
    float specular;             // 0..1
    float emissiveStrength;     // 0..1
    float normalStrength;       // 0..1 (unused in V03)
    float wetness;              // 0..1 (unused in V03)
    float subsurfaceHint;       // 0..1 (unused in V03)
    uint32_t flags;             // reserved
};
```

## Material Types
18 types defined: CLASSIC, STONE, WOOD, METAL, WATER, GLASS, FABRIC, EARTH, GRASS, FOLIAGE, SAND, LAVA, ICE, MAGIC, EMISSIVE, SKY, CHARACTER, PROTECTED_2D, UNKNOWN

## Default Classification Logic
Based on N64 rendering state:
- `is2D` (no Z-buffer) → PROTECTED_2D
- Noise or 2-cycle mode → MAGIC
- Red env color (>200) → LAVA
- Blue env color (>200) → WATER
- Bright prim+env (>150) → EMISSIVE
- Everything else → CLASSIC

## PBR Defaults Assigned
Per-type defaults on first material encounter:
- STONE: roughness=0.85, metallic=0, specular=0.1
- WOOD: roughness=0.75, metallic=0, specular=0.05
- METAL: roughness=0.4, metallic=0.8, specular=0.6
- WATER: roughness=0.1, metallic=0, specular=0.4
- LAVA: roughness=0.3, emissive=0.8, specular=0.2
- EMISSIVE: roughness=0.5, emissive=0.6
- GRASS: roughness=0.9, metallic=0, specular=0.05

## What's NOT Implemented
- Normal mapping (not required for V03)
- Texture-based classification (heuristic only)
- Shader parameter injection (metadata collected, not yet applied to rendering)
- JSON save/load of material definitions

## Future Path
PBR-Lite values are collected and classified. Future work can:
1. Pass material parameters to shaders via constant buffer
2. Add specular/roughness response to fragment shader
3. Use texture analysis for more accurate classification
