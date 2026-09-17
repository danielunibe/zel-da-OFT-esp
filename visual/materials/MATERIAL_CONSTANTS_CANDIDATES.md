# Material Constants Candidates

> **Status:** PROPOSED - Not final values. Subject to change based on asset analysis and testing.
> **Sources:** libultraship/src/fast/interpreter.cpp, libultraship/include/fast/types.h, libultraship/src/fast/shaders/directx/default.shader.hlsl

---

## 1. Roughness Constant Candidates

Roughness in the N64 pipeline is not a native concept. It must be inferred from how the artist used vertex shading (SHADE), primitive color (PRIM), combiner configurations, and texture formats. The following candidates are rough estimates based on observed N64 material behavior.

| Surface Type | Proposed Roughness Range | Rationale |
|---|---|---|
| **Stone** | 0.75 - 0.95 | High diffuse scatter; N64 stone textures use flat IA8/IA4 with no specular highlights; combiners typically map PRIM as a flat color multiplier over TEXEL0 |
| **Metal** | 0.10 - 0.35 | Low roughness for polished weapons/tools; N64 metal uses PRIM color as a bright tint and often relies on G_LIGHTING with directional lights to simulate specular hotspots |
| **Wood** | 0.55 - 0.80 | Moderate roughness; wood surfaces use IA8 textures with subtle variation; PRIM color provides warm undertone; minimal specular response |
| **Cloth** | 0.60 - 0.90 | High roughness; fabric uses CI8/IA8 textures; diffuse-only with no metallic response; combiners often blend SHADE into TEXEL0 for soft shading |
| **Ceramic** | 0.20 - 0.50 | Low-to-moderate roughness; ceramic pots and jars use RGBA16 or IA8 with smooth highlights; PRIM color adds subtle specular tint in some setups |
| **Glass-like** | 0.05 - 0.20 | Very low roughness; glass surfaces use alpha-blended textures with G_BL_CLR_MEM blending; specular reflection is implied by alpha gradients and PRIM color |
| **Magic Surfaces** | 0.30 - 0.70 | Variable; enchanted objects use emissive combiners, noise, and two-cycle passes; roughness is mixed with emissive behavior |

### How Ranges Were Derived

- **interpreter.cpp:1701-1702** - G_CCMUX_PRIMITIVE maps to mRdp->prim_color; surfaces where PRIM is flat white (255,255,255) with no combiner complexity tend toward higher roughness (stone, wood).
- **interpreter.cpp:1704-1705** - G_CCMUX_SHADE maps to per-vertex color (v_arr[i]->color); surfaces using SHADE blending with low-prim INTENSITY tend toward metallic/ceramic behavior.
- **RENDER_PIPELINE.md:116-131** - RSP struct shows current_lights_coeffs (normalized light directions) and geometry_mode (G_LIGHTING flag); G_LIGHTING is the primary indicator of surfaces that can produce specular-like highlights.
- **types.h:299** - prim_color and env_color are RGBA structs; bright PRIM with simple combiners = high roughness, dark/colored PRIM with complex combiners = potential metallic or emissive.

---

## 2. Metallic Constant Candidates

The N64 has no native metallic property. Metallic appearance is simulated through PRIM color brightness, lighting configuration, and combiner selections.

### Metal Objects (Metallic = 0.8 - 1.0)

| Asset Category | Proposed Metallic | Evidence |
|---|---|---|
| Swords and Blades | 0.85 - 1.0 | PRIM color often set to bright silver/white; G_LIGHTING enabled; directional lights create hotspot-like response |
| Shields | 0.70 - 0.95 | Metallic sheen on Hylian Shield; Deku Shield is more dielectric |
| Tools (Boomerang, Hookshot) | 0.80 - 1.0 | Metal construction with PRIM tint simulating reflection |
| Bow and Arrow | 0.75 - 0.90 | Wood/metal hybrid; bow limbs may be lower metallic |
| Iron Boots, Hover Boots | 0.85 - 1.0 | Heavy metal equipment; PRIM color heavily influences appearance |

### Non-Metal Objects (Metallic = 0.0)

| Asset Category | Proposed Metallic | Evidence |
|---|---|---|
| Wooden objects (doors, chests, buildings) | 0.0 | IA8/CI8 textures; no G_LIGHTING-dependent specular; PRIM acts as flat tint |
| Cloth (tunics, roofs, flags) | 0.0 | Diffuse-only; combiners use TEXEL0 * PRIM with no env mapping |
| Stone (walls, boulders, paths) | 0.0 | Flat lighting; G_LIGHTING often disabled or minimal |
| Ceramic (pots, jars, tiles) | 0.0 - 0.05 | May have slight dielectric reflection but no metallic response |
| Glass/Water | 0.0 | Dielectric material; reflection handled via alpha blending, not metallic |
| Vegetation | 0.0 | Two-sided textures; no specular model |
| UI elements | 0.0 | 2D paths (GfxDpTextureRectangle) are never PBR candidates |

---

## 3. Specular Behavior Analysis

### N64 Specular is Simulated, Not Native

The N64 RDP has no dedicated specular pipeline. The default.shader.hlsl implements the N64 combiner model which blends TEXEL, PRIM, SHADE, ENVIRONMENT, and COMBINED inputs. There is no Bidirectional Reflectance Distribution Function (BRDF) and no separate specular pass.

**Key observation from default.shader.hlsl:** The pixel shader (lines 183-332) implements combiner logic through append_formula() (line 275-283) which evaluates N64 CCMUX/ACMUX expressions.

### How Specular-Like Behavior Manifests

1. **PRIM as fake specular** (interpreter.cpp:1701-1702): When G_CCMUX_PRIMITIVE feeds into a combiner cycle with TEXEL0, the PRIM color modulates the surface uniformly. Bright PRIM on dark TEXEL0 creates a highlight-like effect on metal.

2. **SHADE + Lighting** (interpreter.cpp:1704-1705, 1245-1253): Per-vertex lighting via G_LIGHTING computes intensity from surface normals and light directions (interpreter.cpp:1225-1248). This produces gradient shading that approximates diffuse, not true specular.

3. **ENVIRONMENT color** (interpreter.cpp:1707-1708): The environment color (env_color in types.h:299) can be used as a fill light or ambient reflection proxy when mapped through G_CCMUX_ENVIRONMENT.

4. **Two-cycle combiners** (interpreter.cpp:185-192): Cycle 1 and Cycle 2 can chain operations, allowing pseudo-specular layering in complex materials.

5. **Prim LOD Fraction** (interpreter.cpp:1720-1724): Used for distance-based effects but can also modulate highlights on surfaces like water.

### Specular Proxy Mapping to PBR

| N64 Mechanism | PBR Equivalent | Default Roughness Impact |
|---|---|---|
| Bright PRIM + G_LIGHTING | Roughness = 0.1-0.3 (specular hotspot) | Low roughness |
| Flat PRIM + no G_LIGHTING | Roughness = 0.7-0.95 (uniform scatter) | High roughness |
| SHADE gradient + dim PRIM | Roughness = 0.4-0.6 (soft highlight) | Moderate roughness |
| ENVIRONMENT as fill | Roughness = 0.2-0.4 (env reflection proxy) | Low-moderate roughness |
| Two-cycle with ENVIRONMENT cycle 2 | Roughness = 0.1-0.3 (layered highlight) | Low roughness |

---

## 4. Proposed Ranges (Not Final Values)

> **All values are speculative proposals.** These MUST be validated against actual N64 assets and iterated based on visual comparison with original GameCube/Wii remaster references.

### Global Defaults


roughness_constant = 0.7  (default fallback for unknown PBR_LITE_CANDIDATE materials)
metallic_constant  = 0.0  (default fallback - N64 materials are overwhelmingly dielectric)

### Per-Category Ranges

| Category | roughness_constant Range | metallic_constant Range |
|---|---|---|
| Stone | 0.75 - 0.95 | 0.0 |
| Metal (polished) | 0.10 - 0.35 | 0.85 - 1.0 |
| Metal (tarnished) | 0.40 - 0.65 | 0.50 - 0.80 |
| Wood | 0.55 - 0.80 | 0.0 |
| Cloth | 0.60 - 0.90 | 0.0 |
| Ceramic | 0.20 - 0.50 | 0.0 |
| Glass-like | 0.05 - 0.20 | 0.0 |
| Magic / Emissive | 0.30 - 0.70 | 0.0 (emissive dominates) |
| Water | 0.02 - 0.15 | 0.0 |
| UI / 2D | N/A (CLASSIC fallback) | N/A |

### CVAR Integration Points

Per MATERIAL_REGISTRY_PROPOSAL.md:72, the following CVARs control material behavior:
- gMaterialMode: 0=CLASSIC, 1=ENHANCED, 2=PBR_LITE
- gEnableRoughness, gEnableMetallic: Toggle constant-based PBR on/off
- gMaterialRegistryPath: Path to user JSON registry

Per MATERIAL_CLASSIFICATION_SPEC.md:52, the PBR_LITE_CANDIDATE classification is for "Simple albedo + roughness/metallic constants" - the most common target for these constants.

---

## 5. Pros/Cons: Constants-Only vs. Textures

### Constants-Only Approach

**Pros:**
- **Performance:** No extra texture fetches; roughness/metallic are uniform values passed in constant buffers (PerFrameCB at register(b0) and PerDrawCB at register(b1) per default.shader.hlsl:66-87). Minimal overhead over CLASSIC rendering.
- **Memory savings:** No additional VRAM for PBR maps; critical for maintaining N64-era asset budgets (N64 textures were 4KB-64KB each).
- **Deterministic:** Same look across all viewing angles and lighting conditions; predictable behavior for instant CLASSIC fallback.
- **Simplicity:** Easy to author in JSON registry entries; one float per property.
- **Fits N64 art style:** N64 games deliberately avoided per-pixel variation; constants match the original design intent.
- **Fast iteration:** Changing a material property requires no asset rebuild.

**Cons:**
- **No surface variation:** A stone wall has the same roughness across its entire surface; real stone has cracks, moss, and weathering that change roughness.
- **No directional dependence:** Cannot capture anisotropy or direction-dependent reflectance.
- **Over-simplification:** Glass and magic surfaces need per-pixel variation to look convincing; a single roughness value makes glass look like a flat transparent plane.
- **Combat interaction:** Metallic weapons in combat have dynamic lighting that reveals the lack of microsurface variation.
- **Edge cases:** Wood grain direction, cloth weave, and ceramic glaze variation are invisible with constants alone.

### Texture-Based Approach

**Pros:**
- **Per-pixel accuracy:** Roughness/metallic maps capture surface micro-detail (wood grain, metal scratches, ceramic glaze pooling).
- **Art-direction fidelity:** Closest match to modern PBR workflows; enables high-end visual targets.
- **Dynamic response:** Roughness/metallic varies with lighting direction, revealing form and material realism.
- **Modular:** Can be toggled per material via registry without affecting other assets.

**Cons:**
- **Memory cost:** Additional VRAM per texture (even a single channel R8 roughness map is 16KB-256KB).
- **Performance cost:** Extra texture sample in pixel shader (default.shader.hlsl line 231 already samples textures; adding more impacts fill rate).
- **Authoring burden:** Creating PBR maps for every N64 asset is a large manual effort; no automated extraction from N64 art exists.
- **Incompatibility with N64 art:** N64 textures are 4-bit to 16-bit indexed; deriving meaningful roughness/metallic from CI4/CI8 or IA4 textures is lossy.
- **Classification risk:** Per MATERIAL_CLASSIFICATION_SPEC.md:71, "No unknown material receives PBR automatically" - texture maps require correct classification first.
- **Masked/blended texture conflicts:** Per MATERIAL_REGISTRY_PROPOSAL.md:11, masked/blended textures must be respected; PBR maps interfere with existing OTR replacement systems.
- **CLASSIC fallback complexity:** Must ensure PBR texture reads can be bypassed without texture reload.

### Recommended Strategy

**Use constants for 80%+ of materials; textures only for hero assets** where visual fidelity justifies the cost. Specifically:
- Constants: Stone walls, wood doors/floors, cloth, vegetation, UI, all 2D paths
- Textures worth considering: Hero weapons (Master Sword, Hylian Shield), glass/water, magic effects, key character faces

---

## 6. Best Asset Candidates Per Material Type

### Stone Candidates
- **Castle walls and towers** (Hyrule Castle, Kakariko Village)
- **Death Mountain boulders and lava rock**
- **Path and courtyard stones**
- **Stone tablets and lore markers**
- **Goron body (rock-like skin)**
- **Best candidates:** Large flat surfaces with uniform color; simple combiners (TEXEL0 * PRIM); IA8 textures; G_LIGHTING often disabled.

### Metal Candidates
- **Swords** (Master Sword, Kokiri Sword, Biggoron's Sword)
- **Shields** (Hylian Shield, Mirror Shield, Deku Shield)
- **Tools** (Boomerang, Hookshot, Bomber's Notebook frame)
- **Armor pieces** (Iron Boots, Hover Boots, Goron Mask)
- **Doors and mechanisms** (Temple of Time door, Skull Temple mechanisms)
- **Best candidates:** Bright PRIM color (R+G+B > 300 combined); G_LIGHTING enabled; directional lights; combiners using PRIM or ENVIRONMENT as a multiplier on TEXEL0.

### Wood Candidates
- **Trees and logs** (Kokiri Forest, Lost Woods)
- **Chests and wooden containers**
- **Building structures** (Kakariko Village houses, Lon Lon Ranch barns)
- **Bow and arrow shafts**
- **Best candidates:** IA8 textures with warm color tint; PRIM color matching wood hue; no G_LIGHTING or minimal lighting; simple TEXEL0 * PRIM combiner.

### Cloth Candidates
- **Zora Tunic, Goron Tunic, Kaftan**
- **Roof thatch and canvas** (Kakariko roofs, Romani Ranch barn)
- **Flags and banners** (Hyrule Field banner)
- **Best candidates:** CI8 or IA8 textures; two-sided rendering; SHADE blended with TEXEL0; no metallic response; diffuse-only appearance.

### Ceramic Candidates
- **Pots and jars** (Clock Town, Zora Domain, Goron City)
- **Tiles and flooring** (Zora Palace, Temple of Time)
- **Best candidates:** Smooth RGBA16 textures; subtle PRIM highlight; G_LIGHTING sometimes enabled; moderate combiner complexity.

### Glass-like Candidates
- **Zora swimming pool and water surfaces**
- **Lens of Truth (active)**
- **Best candidates:** Alpha-blended (G_BL_CLR_MEM or G_BL_1PRIM); PRIM color modulates alpha; combiners with ENVIRONMENT or COMBINED for reflection proxy.

### Magic Surface Candidates
- **Master Sword beam / energy trail**
- **Timeshift Stone effect**
- **Spell effects** (Din's Fire, Farore's Wind)
- **Best candidates:** Two-cycle combiners; noise/dither enabled (G_ACMUX_NOISE); emissive via G_CCMUX_COMBINED cycle 2; PRIM_LOD_FRAC modulation.

---

## 7. How to Determine Roughness/Metallic from Original N64 Art

### Step-by-Step Process

#### Step 1: Extract the Asset and Identify Combiner Mode
From interpreter.cpp:185 and GenerateCC(), the combiner configuration is encoded in the 64-bit combine_mode (types.h:294). Parse this to determine which inputs (TEXEL0, PRIM, SHADE, ENVIRONMENT, COMBINED) feed each cycle.

Decision tree (MATERIAL_CLASSIFICATION_SPEC.md:87-100):
  Draw call -> 2D path? -> CLASSIC (never PBR)
  Draw call -> 3D path (GfxSpTri1 at interpreter.cpp:1356)?
    -> Two-cycle combiner? -> CLASSIC_SPECIAL_COMBINER (avoid)
    -> Alpha blend? -> CLASSIC_ALPHA (avoid unless glass)
    -> Opaque with simple combiner?
      -> PRIM + TEXEL0 only? -> Likely PBR_LITE_CANDIDATE
      -> SHADE + TEXEL0? -> Check lighting

#### Step 2: Check Geometry Mode Flags
From types.h:244 and RENDER_PIPELINE.md:126, geometry_mode contains G_LIGHTING, G_ZBUFFER, etc.

| Geometry Mode Flag | Implication for Material |
|---|---|
| G_LIGHTING enabled | Surface has specular/directional response; possible metal or ceramic |
| G_LIGHTING disabled | Surface is diffuse-only; likely stone, wood, cloth |
| G_TEXTURE_GEN enabled | Surface uses environment mapping; glass-like or reflective |
| G_CULL_BACK/CULL_FRONT | Standard 3D geometry; proceed to material classification |

#### Step 3: Analyze Primitive (PRIM) Color Usage
From interpreter.cpp:2211-2216 (GfxDpSetPrimColor) and :1701-1702, check the PRIM color values used by the asset:

| PRIM Color Pattern | Roughness Estimate | Metallic Estimate |
|---|---|---|
| Bright white (255,255,255,255) + G_LIGHTING off | 0.7 - 0.95 | 0.0 |
| Bright white (255,255,255,255) + G_LIGHTING on | 0.1 - 0.3 | 0.7 - 1.0 (likely metal) |
| Colored (R+G+B < 400) + G_LIGHTING off | 0.5 - 0.8 | 0.0 |
| Colored (R+G+B < 400) + G_LIGHTING on | 0.3 - 0.5 | 0.3 - 0.6 |
| Very dark (R+G+B < 100) | 0.4 - 0.7 | 0.5 - 0.9 (dark metal or unlit) |

#### Step 4: Analyze SHADE (Vertex Color) Usage
From interpreter.cpp:1704-1705, check if SHADE is used in the combiner:

- **SHADE not used** -> Likely flat-shaded material (stone, metal, UI-like)
- **SHADE + TEXEL0** -> Vertex color modulates texture; likely wood, cloth, organic
- **SHADE + ENVIRONMENT** -> Two-stage lighting; likely ceramic or polished surface
- **SHADE_ALPHA** (1710-1713) -> Alpha driven by vertex color; possibly glass-like or gradient effect

#### Step 5: Check Texture Format and Content
From types.h:267-277 (loaded_texture) and :278-288 (texture_tile):

| Format | Typical Material | Roughness Bias |
|---|---|---|
| IA8 | Stone, wood, metal (grayscale) | Moderate-high (0.5-0.9) |
| RGBA16 | Ceramic, glass, detailed metal | Low-moderate (0.1-0.5) |
| CI8/CI4 | Cloth, UI, organic | High (0.6-0.9) |
| IA4 | Simple stone, wood | High (0.7-0.9) |
| I8 | Height maps, masks | N/A (data not color) |

#### Step 6: Check the G_SETINTENSITY Command (OTR Extension)
From interpreter.cpp:3697-3703 and handler table at :3975, OTR_G_SETINTENSITY (0x40) is a custom OTR command that calls GfxDpSetGrayscaleColor. This is used to set a grayscale tint intensity:

- **High intensity value** (R+G+B > 300) suggests a bright, diffuse material -> roughness 0.6-0.9
- **Low intensity value** (R+G+B < 100) suggests dark/metallic -> roughness 0.2-0.5, possibly metallic 0.5-0.9

#### Step 7: Cross-Reference with Display List Path
From MATERIAL_REGISTRY_PROPOSAL.md:22-24, the display list path (from GfxExecStack::gfx_path) combined with texture path forms the composite key. The same texture used in different display lists may have different material properties (e.g., the same stone texture on a wall vs. a floor).

### Quick Reference Formula


roughness = clamp(
  0.3 * (1.0 - prim_brightness / 255.0)     // dark PRIM -> low roughness (shiny metal)
  + 0.4 * (has_SHADE ? 0.5 : 0.8)           // SHADE blend -> softer response
  + 0.3 * (G_LIGHTING ? 0.2 : 0.6)          // Lighting -> specular potential
, 0.05, 0.95)

metallic = clamp(
  (prim_brightness > 200 and G_LIGHTING) ? 0.85 : 0.0
  + (prim_color saturation < 0.1 ? 0.1 : 0.0)  // neutral gray PRIM -> metallic
, 0.0, 1.0)

> **Caution:** This formula is a starting heuristic. Visual validation against reference renders is mandatory for every material type.

---

## Source File References

| File | Relevance |
|---|---|
| libultraship/src/fast/interpreter.cpp | ColorCombiner system (line 185), SHADE/PRIM/ENV mapping (lines 1701-1740), vertex lighting (lines 1205-1255), G_SETINTENSITY handler (line 3697), handler table (lines 3943-3977) |
| libultraship/include/fast/types.h | RSP struct (lines 230-256): geometry_mode, lights, matrices; RDP struct (lines 258-304): prim_color, env_color, combine_mode, texture_tile, loaded_texture |
| libultraship/src/fast/shaders/directx/default.shader.hlsl | Main pixel shader (lines 183-332): combiner formula evaluation, texture sampling, fog, grayscale, noise; PerFrameCB (line 66), PerDrawCB (line 81) |
| visual_workspace/architecture/RENDER_PIPELINE.md | Render pipeline documentation referencing all three source files above |
| visual_workspace/architecture/MATERIAL_REGISTRY_PROPOSAL.md | Registry schema with roughness_constant (default 1.0) and metallic_constant (default 0.0) fields |
| visual_workspace/materials/MATERIAL_CLASSIFICATION_SPEC.md | Classification taxonomy including PBR_LITE_CANDIDATE, METAL_CANDIDATE, STONE_CANDIDATE, WOOD_CANDIDATE, CLOTH_CANDIDATE |

---

*Generated for the Ocarina Couch Edition visual workspace material constants research task. All values are proposals and require validation against original N64 ROM assets.*