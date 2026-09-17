# Material Registry Proposal

## 1. Architectural Placement

A future MaterialRegistry should be a singleton owned by the Interpreter (libultraship/src/fast/interpreter.cpp) or by GfxRenderingAPIDX11 (gfx_direct3d11.cpp). It must NOT modify the ColorCombiner system at runtime.

Relationships:
- Interpreter owns the RSP/RDP state and the texture cache (mTextureCache, mColorCombinerPool). The registry should observe draw calls from GfxSpTri1 without intercepting them.
- GfxRenderingAPIDX11 owns the shader program pool (mShaderProgramPool) and texture data (mTextures). The registry should query it for texture dimensions and sampler states.
- ColorCombinerKey (combine_mode + options) is the primary shader identifier. The registry should use it as a secondary key.
- OTR mod system: mMaskedTextures in Interpreter provides replacement data. The registry should respect existing masked/blend texture paths.
- Registry data (JSON) should live in the user config directory, not in source.

## 2. Stable Material Identification

Primary key: texture resource path from RawTexMetadata.resource->GetInitData()->Path, normalized via GetBaseTexturePath().

Secondary keys:
- ColorCombinerKey (combine_mode + options) 
- combine_mode uint64_t from mRdp->combine_mode
- first_tile_index + cms/cmt + fmt/siz from texture_tile[]
- Display list path from GfxExecStack::gfx_path (vector of F3DGfx*)

Composite key design: hash(path + combine_mode + options + first_tile_index)

HUD discrimination: 
- GfxDpTextureRectangle and GfxDpImageRectangle are 2D paths (interpreter.cpp:2319, :2385)
- GfxSpTri1 is the 3D path (interpreter.cpp:1356)
- Scissor rectangles and viewport coordinates can distinguish UI regions
- The registry should tag draws from GfxDpTextureRectangle as 2D/UI by default

## 3. Fallback Architecture

CLASSIC fallback chain:
1. If no registry entry exists for a material, use CLASSIC behavior (current rendering).
2. If an ENHANCED entry exists but fails validation, fall back to CLASSIC.
3. Runtime switching via CVAR (e.g., gMaterialMode: 0=CLASSIC, 1=ENHANCED, 2=PBR_LITE).

Every enhanced material must support instant CLASSIC fallback without texture reload.

## 4. Registry Entry Schema

material_type: enum { CLASSIC, ENHANCED, PBR_LITE, UNKNOWN }
albedo: string (path to original texture, must be preserved)
normal: string (optional, path to normal map)
roughness: string or float (optional)
metallic: string or float (optional)
emissive: string (optional)
normal_strength: float (default 1.0)
roughness_constant: float (default 1.0)
metallic_constant: float (default 0.0)
emissive_strength: float (default 1.0)
shadow_mode: enum { DEFAULT, SOLID, TRANSPARENT, BLEND }
reflection_mode: enum { NONE, SSR, RT, PLANAR }
rt_mode: enum { DISABLED, ENABLED }

## 5. HUD/Font/Billboard Protection

Draw call type discrimination:
- GfxDpTextureRectangle (interpreter.cpp:2319) = 2D
- GfxDpImageRectangle (interpreter.cpp:2385) = 2D
- GfxDrawRectangle (interpreter.cpp:2242) = 2D
- GfxSpTri1 (interpreter.cpp:1356) = 3D
- Gfxs2dexBgCopy, Gfxs2dexBg1cyc, Gfxs2dexRecyCopy = 2D background

These 2D paths must NEVER be treated as PBR candidates.

## 6. Integration with Existing Systems

Texture cache: The registry should query mTextureCache for texture IDs and sampler states.
Shader program pool: The registry should query mShaderProgramPool for shader variants.
CVAR integration: gMaterialMode, gMaterialRegistryPath, gEnableNormalMaps, gEnableRoughness, gEnableMetallic, gEnableEmissive.

## 7. Sample Registry JSON Schema

{
  "version": 1,
  "materials": {
    "path/to/texture.tex": {
      "material_type": "PBR_LITE",
      "albedo": "path/to/texture.tex",
      "normal": "path/to/normal.tex",
      "roughness": 0.7,
      "metallic": 0.0,
      "emissive_strength": 0.0,
      "normal_strength": 1.0,
      "shadow_mode": "DEFAULT",
      "reflection_mode": "NONE",
      "rt_mode": "DISABLED"
    }
  }
}