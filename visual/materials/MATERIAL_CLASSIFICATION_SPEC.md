# Material Classification Specification

## 1. Fast3D/N64 Material Model

### Texture Formats
- RGBA16 (5-5-5-1), RGBA32, IA8, IA4, I8, I4, CI8, CI4 (palette-based)
- Texture cache: TextureCacheKey {texture_addr, palette_addrs, fmt, siz, palette_index, size_bytes}
- Import functions: ImportTextureRgba16/32, IA4/8/16, I4/8, Ci4/Ci8, Img, Raw

### Color Combiner System
- Single-cycle and two-cycle modes
- Inputs: TEXEL0, TEXEL1, PRIMITIVE, SHADE, ENVIRONMENT, COMBINED, NOISE, LOD_FRACTION
- Alpha inputs: TEXEL0_ALPHA, TEXEL1_ALPHA, PRIMITIVE_ALPHA, SHADE_ALPHA, ENV_ALPHA
- GenerateCC() builds shader_id0 from combiner inputs
- ColorCombinerKey = {combine_mode, options}

### Geometry Modes
G_LIGHTING, G_ZBUFFER, G_CULL_FRONT, G_CULL_BACK, G_CULL_BOTH, G_TEXTURE_GEN, G_TEXTURE_GEN_LINEAR, G_LIGHTING_POSITIONAL

### Render Modes
G_CYC_1CYCLE, G_CYC_2CYCLE, G_CYC_COPY, G_CYC_FILL

### Alpha & Blend
G_BL_CLR_MEM, G_BL_1MA, G_BL_CLR_PRIM, G_BL_1PRIM, G_AC_THRESHOLD, G_AC_COMPARE, CVG_X_ALPHA (texture edge)

### Fog
G_BL_CLR_FOG, per-vertex fog factor stored in alpha channel

### Lighting
N64 lights: directional (lookat), positional, ambient. F3DLight_t struct. GfxSpMovememF3dex2 handles light loading.

### UV Coordinates
S10.5 format. Texture scaling via GfxSpTexture. Shifts via GfxDpSetTile.

### Texture Wrapping
G_TX_WRAP, G_TX_CLAMP, G_TX_MIRROR. gfx_cm_to_d3d11() converts to D3D11_TEXTURE_ADDRESS modes.

### Filtering
G_TF_POINT, G_TF_BILERP. FILTER_THREE_POINT, FILTER_LINEAR, FILTER_NONE.

### Decals
ZMODE_DEC flag. SlopeScaledDepthBias for z-fighting prevention.

### 2D Paths
GfxDpTextureRectangle, GfxDpImageRectangle, GfxDrawRectangle, Gfxs2dexBgCopy, Gfxs2dexBg1cyc, Gfxs2dexRecyCopy

## 2. Material Classification Taxonomy

CLASSIC_OPAQUE - Standard opaque 3D geometry with textures
CLASSIC_ALPHA - Alpha-blended 3D geometry
CLASSIC_SPECIAL_COMBINER - Two-cycle combiners, noise, LOD, grayscale
PBR_LITE_CANDIDATE - Simple albedo + roughness/metallic constants
METAL_CANDIDATE - Metallic surfaces
STONE_CANDIDATE - Stone/rock surfaces
WOOD_CANDIDATE - Wooden surfaces
CLOTH_CANDIDATE - Fabric surfaces
WATER - Water surfaces
EMISSIVE - Glowing surfaces
DECAL - Decal textures
BILLBOARD - Billboarded sprites
SPRITE - 2D sprites
PARTICLE - Particle effects
UI - User interface elements
FONT - Text glyphs
SKYBOX - Background skyboxes
SPECIAL_FX - Special effects
UNKNOWN - Everything else

## 3. UNKNOWN = CLASSIC FALLBACK

Absolute rule. No unknown material receives PBR automatically.

## 4. Dangerous Features to NOT Replace

- Two-cycle combiners (complex color interactions)
- Texture edge alpha (CVG_X_ALPHA)
- Noise/dither (G_AC_DITHER)
- LOD fraction (distance-based effects)
- Grayscale (per-actor grayscale override)
- Vertex-color-driven effects
- Palette-based textures (CI4/CI8 with TLUT)
- Masked textures (alpha mask from separate texture)
- Blended textures (replacement texture blend)

## 5. Decision Tree

Draw call type → 2D path? → UI/FONT/BILLBOARD/SPRITE → CLASSIC
Draw call type → 3D path (GfxSpTri1)?
  → Has two-cycle combiner? → CLASSIC_SPECIAL_COMBINER
  → Has alpha blend? → CLASSIC_ALPHA
  → Has texture edge? → CLASSIC_ALPHA
  → Has fog? → Check material type
  → Opaque with simple combiner?
    → Metal appearance? → METAL_CANDIDATE
    → Stone appearance? → STONE_CANDIDATE
    → Wood appearance? → WOOD_CANDIDATE
    → Cloth appearance? → CLOTH_CANDIDATE
    → Water? → WATER
    → Emissive? → EMISSIVE
    → Otherwise → PBR_LITE_CANDIDATE or CLASSIC_OPAQUE