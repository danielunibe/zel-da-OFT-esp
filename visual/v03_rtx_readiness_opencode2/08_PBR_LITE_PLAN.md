# 08 — PBR-LITE PLAN

## V03 Physically-Based Material Response System

---

### 1. Design Philosophy

**"Simple geometry + advanced light/material behavior."**

PBR-Lite does NOT require:
- New textures (normal, roughness, metallic maps)
- Replacing original geometry
- New UV coordinates
- Asset pipeline changes

PBR-Lite DOES use:
- Material classification from existing rendering state
- Constant-buffer parameters per draw call
- Simple PBR lighting model in shader
- Roughness/metallic/emissive metadata per material category

---

### 2. PBR-Lite Shader Model

**Minimal PBR lighting (GGX approximation):**

```hlsl
// Inputs from constant buffer (per draw call):
float materialRoughness;   // 0.0 - 1.0
float materialMetallic;    // 0.0 - 1.0
float materialEmissive;    // 0.0 - 1.0 (emissive strength)
float3 materialAlbedo;     // from original texture/color combiner

// Simple GGX specular:
float3 halfVec = normalize(lightDir + viewDir);
float NdotH = max(dot(normal, halfVec), 0.0);
float roughSq = materialRoughness * materialRoughness;
float spec = pow(NdotH, 2.0 / (roughSq * roughSq) - 2.0);

// Fresnel (Schlick approximation):
float3 F0 = lerp(0.04, materialAlbedo, materialMetallic);
float3 F = F0 + (1.0 - F0) * pow(1.0 - max(dot(normal, viewDir), 0.0), 5.0);

// Diffuse (Lambert with metalness subtraction):
float3 diffuse = materialAlbedo * (1.0 - materialMetallic) * max(dot(normal, lightDir), 0.0);

// Combine:
float3 result = diffuse + spec * F;

// Emissive:
result += materialAlbedo * materialEmissive;
```

---

### 3. Material Properties Per Category

| Category | Roughness | Metallic | Emissive | Notes |
|---|---|---|---|---|
| STONE | 0.80 | 0.00 | 0.00 | Matte, no specular highlight |
| WOOD | 0.85 | 0.00 | 0.00 | Very matte, subtle grain |
| METAL | 0.35 | 1.00 | 0.00 | Sharp specular, reflective |
| EARTH | 0.90 | 0.00 | 0.00 | Very matte, soil/rock |
| GRASS | 0.70 | 0.00 | 0.00 | Slight sheen when wet |
| FOLIAGE | 0.70 | 0.00 | 0.00 | Wax-like subsurface hint |
| CLOTH | 0.90 | 0.00 | 0.00 | Completely matte |
| SKIN | 0.70 | 0.00 | 0.00 | Subsurface scattering hint |
| WATER | 0.10 | 0.00 | 0.00 | Mirror-like, special path |
| LAVA | 0.30 | 0.00 | 0.80 | Glowing, molten appearance |
| GLASS | 0.05 | 0.00 | 0.00 | Near-perfect mirror |
| MAGIC | 0.50 | 0.00 | 0.30 | Glowing, mystical |
| EMISSIVE | 0.50 | 0.00 | 1.00 | Full self-illumination |
| UI_2D | N/A | N/A | N/A | EXCLUDED |
| SPRITE | 0.50 | 0.00 | 0.00 | Billboard default |
| UNKNOWN | 0.70 | 0.00 | 0.00 | Conservative default |

---

### 4. Constant Buffer Layout (Per Draw Call)

```hlsl
cbuffer MaterialParams : register(b2) {
    float4 materialAlbedo;      // from prim_color or texture sample
    float  materialRoughness;   // from category lookup
    float  materialMetallic;    // from category lookup
    float  materialEmissive;    // from category lookup
    float  materialAlpha;       // original alpha
    float4 materialFogParams;   // fog color + factor (pass-through)
    uint   materialFlags;       // bitfield: IS_2D, IS_TRANSPARENT, etc.
    float3 padding;             // alignment
};
```

---

### 5. Integration Points

| Integration | File | Change |
|---|---|---|
| Material property lookup | `MaterialRegistry.cpp` | `GetMaterialProperties(type)` returns struct |
| Constant buffer update | `gfx_direct3d11.cpp` | Update `b2` register before draw |
| Shader modification | `default.shader.hlsl` | Add PBR block, gated by `@if(PBR_ENABLED)` |
| Profile gate | `interpreter.cpp` | Only populate `b2` when ENHANCED |

---

### 6. Water Special Path

Water requires special handling because it's transparent and reflective:

```hlsl
// Water-specific PBR:
float3 waterColor = float3(0.0, 0.15, 0.3);  // deep blue
float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), 3.0);
float3 waterSpec = spec * lerp(waterColor, float3(1.0), fresnel);
// No diffuse for water — it's purely specular/transmissive
```

---

### 7. Emissive Handling

Emissive materials bypass standard lighting:

```hlsl
if (materialEmissive > 0.0) {
    // Emissive materials glow regardless of lighting
    result = materialAlbedo * materialEmissive;
    // Add minimal specular for glossy emissive surfaces
    result += spec * 0.1;
}
```

---

### 8. Performance Considerations

| Operation | GPU Cost | Optimization |
|---|---|---|
| Material lookup (CPU) | <0.01ms | Integer comparisons only |
| Constant buffer update | <0.01ms | Batched with existing VBO update |
| PBR lighting (per pixel) | +0.02-0.05ms | Simpler than full GGX |
| Emissive path | +0.01ms | Branch, minimal ALU |
| Water special path | +0.03ms | Branch, specular only |

**Total PBR-Lite overhead estimate: 0.05-0.10ms per frame at 3440×1440**

---

### 9. What PBR-Lite Does NOT Do

| Feature | Why Not |
|---|---|
| Normal mapping | No normal maps in original assets |
| Parallax occlusion mapping | No height maps in original assets |
| Image-based lighting | No environment maps in original assets |
| Shadow mapping enhancement | Separate system, future phase |
| Screen-space reflections | Separate system, future phase |
| Subsurface scattering | Approximated via material constant only |
| Anisotropic filtering | Would need material-specific anisotropy |

---

### 10. Visual Impact Estimation

| Material | Before (N64) | After (PBR-Lite) |
|---|---|---|
| Stone walls | Flat, uniform | Subtle specular, rough surface |
| Metal objects | Same as everything | Distinctive metallic sheen |
| Water | Simple transparency | Fresnel-based reflectivity |
| Lava | Red-tinted | Glowing with heat response |
| Wood | Flat | Matte with grain-like roughness |
| Character skin | Flat | Subtle subsurface warmth |
| Glass | Transparent | Mirror-like specular |

---

### 11. Fallback

When CLASSIC profile is active:
- Material constant buffer set to default values (roughness=0.7, metallic=0.0, emissive=0.0)
- PBR shader block disabled via `@if(PBR_ENABLED)` = 0
- Result: identical to current N64 rendering
