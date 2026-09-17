# Ship of Harkinian 9.1.1 — Render Pipeline Audit
**Branch:** Copper Bravo (`4aaad850bd5540cd77c2d83f3ad348d3b38605b2`)
**Backend:** Windows / DirectX 11
**Source Root:** `C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV\source\shipwright`

---

## Pipeline Stage Overview

| # | Stage | Primary File(s) |
|---|-------|-----------------|
| 1 | N64 Display Lists → Fast3D interpretation | `libultraship/src/fast/interpreter.cpp` |
| 2 | Fast3D / libultraship Interpreter | `libultraship/src/fast/interpreter.cpp`, `libultraship/include/fast/interpreter.h` |
| 3 | RSP — matrix stack, lights, geometry modes | `libultraship/include/fast/types.h` (struct RSP) |
| 4 | RDP — texture tiles, combiner modes, other modes | `libultraship/include/fast/types.h` (struct RDP) |
| 5 | ColorCombiner system | `libultraship/src/fast/interpreter.cpp` (GenerateCC, LookupOrCreateColorCombiner) |
| 6 | Texture cache | `libultraship/include/fast/interpreter.h` (TextureCacheKey/Value), `libultraship/src/fast/interpreter.cpp` |
| 7 | Shader generation | `libultraship/src/fast/backends/gfx_direct3d11.cpp` (gfx_direct3d_common_build_shader), `libultraship/src/fast/shaders/directx/default.shader.hlsl` |
| 8 | DirectX 11 backend | `libultraship/src/fast/backends/gfx_direct3d11.cpp` |
| 9 | PerFrameCB / PerDrawCB | `libultraship/include/fast/backends/gfx_direct3d_common.h` |
| 10 | Vertex buffer assembly (GfxSpTri1) | `libultraship/src/fast/interpreter.cpp` |
| 11 | Draw call path (DrawTriangles) | `libultraship/src/fast/backends/gfx_direct3d11.cpp` |
| 12 | Framebuffer system | `libultraship/src/fast/backends/gfx_direct3d11.cpp` |
| 13 | Post-present path (EndFrame, SwapChain) | `libultraship/src/fast/interpreter.cpp` (EndFrame), `libultraship/src/fast/backends/gfx_dxgi.cpp` |

---## 1. N64 Display Lists → Fast3D Interpretation

**PATH:** `libultraship/src/fast/interpreter.cpp` — `Interpreter::Run()` (line 4304)

**CLASS:** `Interpreter` (namespace `Fast`)

**STRUCT:** `GfxExecStack` (interpreter.h:133)

**RESPONSIBILITY:** Entry point for processing a single frame of N64 Gfx display list commands. Resets RSP state, sets up framebuffer routing, dispatches the command loop via gfx_step(), then flushes remaining geometry.

**Code flow in `Run()` (lines 4304–4367):**
1. `SpReset()` (line 4305) — resets modelview stack size to 1, light count to 2, lookat vectors to defaults, recalculates lookat coefficients.
2. Clears pixel-depth pending/cached maps (lines 4307–4308).
3. Stores matrix replacement map pointer `mCurMtxReplacements` (line 4310).
4. `mRapi->UpdateFramebufferParameters(0, ...)` (line 4312) — configures window framebuffer as render target (with depth buffer) or without depth buffer if rendering to game FB.
5. `mRapi->StartFrame()` (line 4313) — DX11: binds PerFrameCB and PerDrawCB to PS constant buffer slots 0 and 1.
6. `mRapi->StartDrawToFramebuffer(mRendersToFb ? mGameFb : 0, ...)` (line 4314) — routes rendering to game FB or window.
7. `mRapi->ClearFramebuffer(false, true)` (line 4315) — clears depth buffer.
8. Resets viewport/scissor dirty flags (lines 4317–4319).
9. Command loop (lines 4322–4341): `g_exec_stack.start((F3DGfx*)commands)` then `gfx_step()` until stack empty.
10. `Flush()` (line 4343) — emits any buffered triangles.
11. Post-render resolve (lines 4347–4366): If rendering to FB, switches back to window FB and resolves MSAA if enabled.

**Function:** `gfx_step()` (line 4113) — Dispatches a single opcode to the appropriate handler table based on `ucode_handler_index`. Supports f3d, f3dex, f3dex2, s2dex, otr, and rdp handler tables.

**Handler dispatch tables** (lines 3912–4065):
- `rdpHandlers` — RDP opcodes (G_SETTARGETINTERPINDEX through G_SETCIMG)
- `otrHandlers` — OTR custom opcodes (G_SETTIMG_OTR_HASH through G_SETINTENSITY)
- `f3dex2Handlers` — F3DEX2 ucode (G_NOOP through G_SETOTHERMODE_H)
- `f3dexHandlers` — F3DEX ucode
- `f3dHandlers` — F3D ucode
- `s2dexHandlers` — S2DEX ucode (G_BG_COPY, G_BG_1CYC, G_OBJ_RECTANGLE)

**Key structures:**
- `struct F3DGfx` — dual 32-bit word command (`words.w0`, `words.w1`)
- `struct GfxExecStack` — `std::stack<F3DGfx*> cmd_stack` + `std::vector<const F3DGfx*> gfx_path` (debugging path tracking)

**CONSTANT:** `MAX_TRI_BUFFER = 256` (interpreter.cpp:107) — triangle buffer capacity before auto-flush in `Flush()` (line 127): `if (mBufVboLen > 0) { mRapi->DrawTriangles(mBufVbo, mBufVboLen, mBufVboNumTris); }`

---

## 2. Fast3D / libultraship Interpreter

**PATH:** `libultraship/src/fast/interpreter.cpp`, `libultraship/include/fast/interpreter.h`

**CLASS:** `Interpreter` (namespace `Fast`)

**STRUCT:** `GfxExecStack` (interpreter.h:133)

**RESPONSIBILITY:** Core interpretation of N64 microcode commands (RSP and RDP), maintaining all rendering state, texture cache, combiner pool, and framebuffer management.

**Key member variables (interpreter.h:473–519):**
- `RSP* mRsp` — pointer to RSP state struct
- `RDP* mRdp` — pointer to RDP state struct
- `RenderingState mRenderingState` — current shader program, viewport, scissor, texture pointers (line 334–341)
- `GfxTextureCache mTextureCache` — LRU texture cache: `TextureCacheMap map` + `std::list<TextureCacheMapIter> lru` + `std::vector<uint32_t> free_texture_ids` (interpreter.h:320–324)
- `std::map<ColorCombinerKey, ColorCombiner> mColorCombinerPool` — combiner shader cache (line 478)
- `float* mBufVbo` — vertex buffer (MAX_TRI_BUFFER * 32 * 3 floats = 24,576 floats)
- `size_t mBufVboLen` — current bytes used in vertex buffer
- `size_t mBufVboNumTris` — triangle count in buffer
- `int mGameFb` — game framebuffer ID (line 508)
- `int mGameFbMsaaResolved` — MSAA-resolved framebuffer ID (line 509)
- `unsigned int mMsaaLevel` — current MSAA level (default 1, line 493)

**Key methods:**
- `Init()` (line 4181) — initializes wapi, rapi, framebuffers, tex upload buffer
- `Run()` (line 4304) — main frame execution
- `EndFrame()` (line 4369) — end frame sequence
- `Flush()` (line 127) — emits buffered triangles via `mRapi->DrawTriangles()`
- `GfxSpTri1()` (line 1356) — triangle primitive assembly + vertex buffer fill
- `LookupOrCreateColorCombiner()` (line 394) — combiner cache lookup/creation
- `GenerateCC()` (line 185) — shader ID generation from combiner key
- `TextureCacheLookup()` (line 416) — texture cache lookup with LRU eviction
- `ImportTexture()` (line 886) — dispatches to format-specific import functions
- `StartFrame()` (line 4251) — framebuffer dimension updates
- `CreateFrameBuffer()` (line 4392) — creates game framebuffers
- `SpReset()` (line 4163) — RSP state reset

**CONFIG/CVAR:** `CVAR_INTERNAL_RESOLUTION` (line 4188) — internal resolution multiplier  
**CONFIG/CVAR:** `CVAR_MSAA_VALUE` = `"gMSAAValue"` (line 4190, defined in cmake/cvars.cmake:4) — MSAA sample count
## 3. RSP — Matrix Stack, Lights, Geometry Modes

**PATH:** `libultraship/include/fast/types.h` (struct RSP, lines 230–256)

**STRUCT:** `RSP` (types.h:230)

**RESPONSIBILITY:** Holds all RSP (Reality Signal Processor) state: matrix stack, light data, geometry modes, and loaded vertex data.

**Fields (types.h:230–256):**
```cpp
float modelview_matrix_stack[11][4][4];    // 11-entry MV matrix stack (line 231)
uint8_t modelview_matrix_stack_size;        // current stack depth (line 232)
float MP_matrix[4][4];                      // ModelView * Projection (line 234)
float P_matrix[4][4];                        // Projection matrix (line 235)
F3DLight lookat[2];                          // lookat vectors (line 237)
F3DLight current_lights[MAX_LIGHTS + 1];    // lights array, index 0 = ambient (line 238)
float current_lights_coeffs[MAX_LIGHTS][3]; // normalized light directions (line 239)
float current_lookat_coeffs[2][3];           // lookat direction coeffs (line 240)
uint8_t current_num_lights;                  // includes ambient (line 241)
bool lights_changed;                          // flag for light recalculation (line 242)
uint32_t geometry_mode;                      // ZBUFFER, SHADE, FOG, LIGHTING, etc. (line 244)
int16_t fog_mul, fog_offset;                // fog scaling params (lines 245–246)
uint32_t extra_geometry_mode;               // G_EX_INVERT_CULLING etc. (line 248)
struct { uint16_t s, t; } texture_scaling_factor; // S10.5 tex coords (line 252)
struct LoadedVertex loaded_vertices[MAX_VERTICES + 4]; // 68 vertices (line 254)
ShaderMod current_shader;                    // custom shader ID (line 255)
```

**MAX_VERTICES = 64** (interpreter.h:228) — maximum vertex count per draw  
**MAX_LIGHTS = 32** (interpreter.h:227) — maximum light count

**Key RSP functions (interpreter.cpp):**
- `GfxSpMatrix()` (line 1065) — pushes/loads/multiplies modelview or projection matrices; handles `mCurMtxReplacements` for OTR matrix overrides (line 1068). Updates `MP_matrix` via `MatrixMul()` (line 1119): `MatrixMul(mRsp->MP_matrix, mRsp->modelview_matrix_stack[mRsp->modelview_matrix_stack_size - 1], mRsp->P_matrix);`
- `GfxSpPopMatrix()` (line 1122) — pops N matrices from stack, decrements `modelview_matrix_stack_size`.
- `GfxSpVertex()` (line 1157) — transforms vertices through `MP_matrix`, applies lighting (if `G_LIGHTING`), texture generation (if `G_TEXTURE_GEN`), fog factor calculation (line 1336: `fog_z = z * winv * mRsp->fog_mul + mRsp->fog_offset`), and clip rejection. Stores results in `mRsp->loaded_vertices[]`.
- `GfxSpTri1()` (line 1356) — triangle assembly: clip rejection test, culling test, state comparison (depth, decal, viewport, scissor, combiner), texture import, shader binding, and vertex buffer emission.
- `GfxSpGeometryMode()` (line 1779) — `mRsp->geometry_mode &= ~clear; mRsp->geometry_mode |= set;`
- `GfxSpExtraGeometryMode()` (line 1784) — `mRsp->extra_geometry_mode &= ~clear; mRsp->extra_geometry_mode |= set;`
- `GfxSpTexture()` (line 1929) — sets `mRsp->texture_scaling_factor.s/t`, marks `mRdp->textures_changed[0/1] = true`.
- `GfxSpMovememF3dex2()` (line 1838) — handles F3DEX2 movemem (F3DEX2_G_MV_VIEWPORT, F3DEX2_G_MV_LIGHT).
- `GfxSpMovewordF3dex2()` (line 1879) — handles G_MW_NUMLIGHT, G_MW_FOG, G_MW_SEGMENT, G_MW_SEGMENT_INTERP.
- `GfxSpModifyVertex()` (line 1345) — modifies vertex S/T coordinates at `vtx_idx`.

**STRUCT:** `MtxS` (types.h:4–11) — 4x4 fixed-point matrix (union of `Mtx_t m[4][4]`, `unsigned short int intPart[4][4]`, `unsigned short int fracPart[4][4]`)  
**STRUCT:** `MtxF` (types.h:13–19) — 4x4 float matrix (union of `MtxF_t mf[4][4]`, named float members xx..ww)  
**TYPEDEF:** `Mtx` = `MtxS` (default, when `GBI_FLOATS` not defined) or `MtxF` (when `GBI_FLOATS` defined) (lines 21–25)

---

## 4. RDP — Texture Tiles, Combiner Modes, Other Modes

**PATH:** `libultraship/include/fast/types.h` (struct RDP, lines 258–304)

**STRUCT:** `RDP` (types.h:258)

**RESPONSIBILITY:** Holds all RDP (Reality Drawing Processor) state: texture tiles, palette pointers, combine mode, other modes, colors, viewport, and framebuffer addresses.

**Fields (types.h:258–304):**
```cpp
const uint8_t* palettes[2];                              // TLUT pointers (line 259)
struct { const uint8_t* addr; uint8_t siz; uint32_t width; uint32_t tex_flags; RawTexMetadata raw_tex_metadata; } texture_to_load; // line 261
struct { const uint8_t* addr; uint32_t orig_size_bytes; uint32_t size_bytes; uint32_t full_image_line_size_bytes; uint32_t line_size_bytes; uint32_t tex_flags; RawTexMetadata raw_tex_metadata; bool masked; bool blended; } loaded_texture[2]; // lines 267-277
struct { uint8_t fmt; uint8_t siz; uint8_t cms, cmt; uint8_t shifts, shiftt; float uls, ult, lrs, lrt; uint16_t tmem; uint32_t line_size_bytes; uint8_t palette; uint8_t tmem_index; } texture_tile[8]; // lines 278-288
bool textures_changed[2];                                  // line 289
uint8_t first_tile_index;                                  // line 291
uint32_t other_mode_l;                                     // line 293 (blend, alpha compare, Z_UPD, ZMODE_DEC, etc.)
uint64_t combine_mode;                                     // line 294 (64-bit: rgb_cycle1 | alpha_cycle1<<16 | rgb_cycle2<<28 | alpha_cycle2<<44)
bool grayscale;                                            // line 295
ShaderMod current_shader;                                  // line 296
uint8_t prim_lod_fraction;                                 // line 298
struct RGBA env_color, prim_color, fog_color, fill_color, grayscale_color; // line 299
struct XYWidthHeight viewport, scissor;                    // line 300
bool viewport_or_scissor_changed;                         // line 301
void* z_buf_address;                                       // line 302
void* color_image_address;                                 // line 303
```

**Key RDP functions (interpreter.cpp):**
- `GfxDpSetCombineMode()` (line 2185) — sets `mRdp->combine_mode` as 64-bit value: `rgb | (alpha << 16) | (rgb_cyc2 << 28) | (alpha_cyc2 << 44)`.
- `GfxDpSetOtherMode()` (line 2485) — directly sets `mRdp->other_mode_h` and `mRdp->other_mode_l`.
- `GfxSpSetOtherMode()` (line 2477) — sets individual bit fields: `om = mRdp->other_mode_l | ((uint64_t)mRdp->other_mode_h << 32); om = (om & ~mask) | mode;`
- `GfxDpSetTile()` (line 1967) — configures `texture_tile[tile]`; auto-clamps wrap to clamp if NOMASK (line 1973–1978).
- `GfxDpLoadBlock()` (line 2021) — loads texture block data, sets `loaded_texture[tmem_index]` metadata (size_bytes, orig_size_bytes, line_size_bytes, etc.), marks `textures_changed`.
- `GfxDpLoadTile()` (line 2078) — similar to LoadBlock but handles offset calculations from uls/ult/lrs/lrt.
- `GfxDpLoadTlut()` (line 2008) — sets palette pointers from TLUT source.
- `GfxDpSetScissor()` (line 1940) — converts U10.2 to float, calls `AdjustVIewportOrScissor()`.
- `GfxDpTextureRectangle()` (line 2319) — texture mapping rectangle with vertex setup and `GfxDrawRectangle()`.
- `GfxDrawRectangle()` (line 2242) — draws screen-space rectangle using 4 cached vertices at `MAX_VERTICES + 0..3` with aspect ratio correction.
- `GfxDpFillRectangle()` (line 2433) — fills a rectangle with the fill color; skips if z_buf == color_image.

**CONSTANT:** `TEXTURE_CACHE_MAX_SIZE = 500` (interpreter.cpp:65) — max cached textures before LRU eviction.
## 5. ColorCombiner System

**PATH:** `libultraship/src/fast/interpreter.cpp` (lines 185, 394)  
**HEADER:** `libultraship/include/fast/interpreter.h` (lines 86–88, 326–332)

**FUNCTION:** `Interpreter::GenerateCC(ColorCombiner* comb, const ColorCombinerKey& key)` (interpreter.cpp:185)

**FUNCTION:** `Interpreter::LookupOrCreateColorCombiner(const ColorCombinerKey& key)` (interpreter.cpp:394)

**STRUCT:** `ColorCombiner` (interpreter.h:326–332) — stores `shader_id0`, `shader_id1`, `usedTextures[2]`, `prg[16]` (shader program array indexed by clamp combination), `shader_input_mapping[2][7]`.

**STRUCT:** `ColorCombinerKey` (interpreter.h:86–93) — `uint64_t combine_mode` + `uint64_t options` (SHADER_OPT bitflags).

**RESPONSIBILITY:** Generates and caches shader programs from N64 combine mode settings. Maps N64 G_CCMUX/G_ACMUX constants to shader input registers and options flags.

**GenerateCC() algorithm (lines 185–392):**
1. Iterates over 2 cycles (or 1 if single-cycle), extracting RGB A/B/C/D and alpha A/B/C/D from `combine_mode` (bits: RGB is 28 bits per cycle, Alpha is 12 bits per cycle).
2. Normalizes: if rgbA == rgbB or rgbC == G_CCMUX_0, sets all to G_CCMUX_0. Same for alpha.
3. For cycle 1 (first cycle), clears RGB if no cycle-1 RGB combiner is used; clears ALPHA if no cycle-1 alpha is used.
4. Maps CCMUX/ACMUX constants to `shader_input_mapping` and `shaderId0` bits. Each unique input gets a `SHADER_INPUT_1` through `SHADER_INPUT_7` number.
5. Sets `comb->usedTextures[0/1]` based on TEXEL0/TEXEL1 references.
6. `shaderId1 = key.options` (line 190).

**LookupOrCreateColorCombiner() algorithm (lines 394–406):**
1. Checks `mPrevCombiner` cache hit (line 395–396).
2. Falls back to `mColorCombinerPool.find(key)` (line 398).
3. On miss: `Flush()` (line 402), inserts new entry, calls `GenerateCC()` (line 404).

**Shader options flags (interpreter.h:62–84):**
```cpp
SHADER_OPT(ALPHA), SHADER_OPT(FOG), SHADER_OPT(TEXTURE_EDGE), SHADER_OPT(NOISE),
SHADER_OPT(_2CYC), SHADER_OPT(ALPHA_THRESHOLD), SHADER_OPT(INVISIBLE),
SHADER_OPT(GRAYSCALE), SHADER_OPT(TEXEL0_CLAMP_S), SHADER_OPT(TEXEL0_CLAMP_T),
SHADER_OPT(TEXEL1_CLAMP_S), SHADER_OPT(TEXEL1_CLAMP_T), SHADER_OPT(TEXEL0_MASK),
SHADER_OPT(TEXEL1_MASK), SHADER_OPT(TEXEL0_BLEND), SHADER_OPT(TEXEL1_BLEND),
SHADER_OPT(USE_SHADER)
```

**CCFeatures struct (interpreter.h:100–120):** Used by `gfx_cc_get_features()` (line 4559) to extract `c[2][2][4]`, `opt_*` booleans, `usedTextures[2]`, `used_masks[2]`, `used_blend[2]`, `clamp[2][2]`, `numInputs`, `do_single/multiply/mix[2][2]`, `color_alpha_same[2]`, `shader_id` from a shader ID pair.

---

## 6. Texture Cache

**PATH:** `libultraship/include/fast/interpreter.h` (TextureCacheKey/Value at lines 171–201, GfxTextureCache at 320–324), `libultraship/src/fast/interpreter.cpp` (lines 408–471, 886–1032)

**STRUCT:** `TextureCacheKey` (interpreter.h:171–186) — `texture_addr`, `palette_addrs[2]`, `fmt`, `siz`, `palette_index`, `size_bytes`. Hash uses `uintptr_t` XOR shift (line 183).

**STRUCT:** `TextureCacheValue` (interpreter.h:191–197) — `texture_id`, `cms`, `cmt`, `linear_filter`, `lru_location` (iterator into LRU list).

**STRUCT:** `GfxTextureCache` (interpreter.h:320–324) — `TextureCacheMap map` (unordered_map), `std::list<TextureCacheMapIter> lru`, `std::vector<uint32_t> free_texture_ids`.

**RESPONSIBILITY:** Caches decoded RGBA32 textures keyed by N64 address/fmt/siz/palette. Implements LRU eviction at 500 entries max.

**TextureCacheLookup()** (interpreter.cpp:416–453):
1. Hash-lookup in `mTextureCache.map` (line 417).
2. On hit: calls `mRapi->SelectTexture(i, texture_id)`, updates LRU (line 423–424), returns true.
3. On miss: LRU eviction if at capacity (lines 428–433), allocates from free IDs or `mRapi->NewTexture()` (line 441), inserts into map, sets defaults via `mRapi->SetSamplerParameters(i, false, 0, 0)` (line 450).

**TextureCacheClear()** (interpreter.cpp:408–413): Frees all texture IDs, clears map and LRU list. Called on `FB_CreateFramebuffers` and frame resets.

**TextureCacheDelete()** (interpreter.cpp:463–481): Deletes entries matching `origAddr` from the hash bucket. Used for `G_INVALTEXCACHE` opcode.

**ImportTexture dispatch chain (interpreter.cpp:886–971):**
1. Builds `TextureCacheKey` from fmt/siz/palette/address (lines 900–905).
2. Tries `TextureCacheLookup()` (line 907) — returns early if cached.
3. Dispatches by format to format-specific `ImportTexture*` functions:
   - `ImportTextureRgba16()` (line 483) — 16-bit RGBA → R5G5B5A1 → SCALE_5_8 RGB, alpha=1/0
   - `ImportTextureRgba32()` (line 525) — direct 32-bit upload (no conversion)
   - `ImportTextureIA4()` (line 542) — 4-bit IA → I4→8 and A1→8
   - `ImportTextureIA8()` (line 574) — 8-bit IA → I8 and A4→8
   - `ImportTextureIA16()` (line 604) — 16-bit IA → direct copy
   - `ImportTextureI4()` (line 646) — 4-bit I → all channels I4→8
   - `ImportTextureI8()` (line 690) — 8-bit I → all channels direct
   - `ImportTextureCi4()` (line 715) — CI4 with palette lookup (Big endian)
   - `ImportTextureCi8()` (line 761) — CI8 with palette lookup (Big endian)
   - `ImportTextureImg()` (line 799) — direct upload with OTR metadata dimensions
   - `ImportTextureRaw()` (line 811) — handles h_byte_scale/v_pixel_scale scaling, type-based dispatch (CI4/CI8 → paletted)

**ImportTextureMask()** (interpreter.cpp:973–1032): Creates black/transparent or transparent-only textures for masking.

**Key variables on RDP:**
- `mRdp->texture_tile[tile]` (8 tiles, types.h:278–288): fmt, siz, cms, cmt, shifts, shiftt, uls, ult, lrs, lrt, tmem, line_size_bytes, palette, tmem_index
- `mRdp->loaded_texture[2]` (types.h:267–277): addr, size_bytes, orig_size_bytes, full_image_line_size_bytes, line_size_bytes, tex_flags, raw_tex_metadata, masked, blended

---

## 7. Shader Generation

**PATH:** `libultraship/src/fast/backends/gfx_direct3d11.cpp` (lines 1280–1347)  
**SHADER:** `libultraship/src/fast/shaders/directx/default.shader.hlsl` (332 lines)

**FUNCTION:** `gfx_direct3d_common_build_shader(size_t& numFloats, const CCFeatures& cc_features, bool include_root_signature, bool three_point_filtering, bool use_srgb)` (gfx_direct3d11.cpp:1280, declared in gfx_direct3d_common.h:182)

**RESPONSIBILITY:** Uses the Prism processor to instantiate the `default.shader.hlsl` template with combiner-specific parameters, producing a complete HLSL vertex+pixel shader pair.

**gfx_direct3d_common_build_shader() algorithm (lines 1280–1347):**
1. `raw_numFloats = 4` (line 1282) — base float count for position + fog + grayscale.
2. Creates `prism::Processor` and populates `prism::ContextItems` (lines 1284–1324) with all CCFeatures, shader constants, and invocation functions:
   - `append_formula` → `prism_append_formula` (line 1237) — generates HLSL combiner expressions
   - `update_floats` → `update_raw_floats` (line 1260) — counts extra float inputs
3. Loads `shaders/directx/default.shader.hlsl` resource (lines 1330–1332).
4. `processor.load(*shader)` (line 1339) — loads template with `@prism` directives.
5. `processor.bind_include_loader(dx_include_fs)` (line 1340) — binds include loader for `default.shader.hlsl`.
6. `processor.process()` (line 1341) — processes template directives (`@if`, `@for`, `@end`).
7. Sets `numFloats = raw_numFloats` (line 1342).

**default.shader.hlsl pipeline:**
- **Prism directives** (lines 1–10): `@prism(type='hlsl', ...)` and `@if(o_root_signature)` for root signature generation.
- **PSInput struct** (lines 12–49): Position, UVs (o_textures[0/1]), TEXCLAMPS/TEXCLMPT (o_clamp), FOG (o_fog), GRAYSCALE (o_grayscale), INPUT1..N (o_inputs).
- **Texture declarations** (lines 51–64): g_texture0/1, g_textureMask0/1, g_textureBlend0/1 with register bindings t0–t5.
- **PerFrameCB** (lines 66–69): `noise_frame` (uint), `noise_scale` (float) — register b0.
- **PerDrawCB** (lines 81–87): `width`, `height`, `linear_filtering` for 2 textures — register b1 (only when three_point_filtering enabled).
- **VSMain** (lines 101–165): Passes through position, UVs, clamps, fog, grayscale, inputs.
- **PSMain** (lines 183–332): Texture sampling with 3-point filtering, combiner formula evaluation via `append_formula`, WRAP/clamp/fog/grayscale/noise/alpha_threshold/invisible/sRGB post-processing.

**Compilation** (gfx_direct3d11.cpp:327–343): Compiles VSMain as `vs_4_0` and PSMain as `ps_4_0` via `mD3dCompile()`. Creates `ID3D11VertexShader`, `ID3D11PixelShader`, `ID3D11InputLayout`, `ID3D11BlendState`.

---

## 8. DirectX 11 Backend

**PATH:** `libultraship/src/fast/backends/gfx_direct3d11.cpp` (1349 lines)  
**CLASS:** `GfxRenderingAPIDX11` (inherits `GfxRenderingAPI`, declared in gfx_direct3d_common.h:67)

**RESPONSIBILITY:** All DirectX 11 rendering: device/context management, shader compilation, resource creation, draw calls, framebuffer operations, and pixel depth queries.

**Initialization sequence (Init(), lines 122–284):**
1. Loads `d3d11.dll` and `D3DCompiler_47.dll` (lines 124–135).
2. Creates D3D11 device via `mWindowBackend->CreateFactoryAndDevice()` (line 140, feature levels: 11.0/10.1/10.0).
3. Creates swap chain via `mWindowBackend->CreateSwapChain()` (line 143).
4. Creates default framebuffer (fb_id=0) at line 170.
5. Queries multisample quality levels (lines 179–182).
6. Creates vertex buffer: `D3D11_BIND_VERTEX_BUFFER`, 256*32*3*`sizeof(float)` (line 190).
7. Creates PerFrameCB: `D3D11_BIND_CONSTANT_BUFFER`, `sizeof(PerFrameCB)` (line 204).
8. Creates PerDrawCB: `D3D11_BIND_CONSTANT_BUFFER`, `sizeof(PerDrawCB)` (line 215).
9. Creates depth/stencil compute shaders for pixel depth queries (lines 225–277).
10. Initializes ImGui (line 283).

**State objects managed:**
- `mRasterizerState` — D3D11_FILL_SOLID, D3D11_CULL_NONE, FrontCounterClockwise, depth bias for z-fighting modes.
- `mDepthStencilState` — depth test/mask controlled by geometry mode and ZMODE_DEC.
- `mVertexBuffer` — 256*32*3 floats upload buffer.
- `mPerFrameCb`, `mPerDrawCb` — constant buffers.
- `mComputeShader`, `mComputeShaderMsaa` — depth buffer readback.

---

## 9. PerFrameCB / PerDrawCB Constant Buffers

**PATH:** `libultraship/include/fast/backends/gfx_direct3d_common.h` (lines 15–28)

**STRUCT:** `PerFrameCB` (gfx_direct3d_common.h:15–19)
```cpp
struct PerFrameCB {
    uint32_t noise_frame;
    float noise_scale;
    uint32_t padding[2]; // constant buffers must be multiples of 16 bytes in size
};
```

**STRUCT:** `PerDrawCB` (gfx_direct3d_common.h:21–28)
```cpp
struct PerDrawCB {
    struct Texture {
        uint32_t width;
        uint32_t height;
        uint32_t linear_filtering;
        uint32_t padding;
    } mTextures[SHADER_MAX_TEXTURES];
};
```

**SHADER REGISTERS:** PerFrameCB at `register(b0)` (default.shader.hlsl:66), PerDrawCB at `register(b1)` (default.shader.hlsl:81).

**RESPONSIBILITY:** PerFrameCB carries noise parameters for stochastic rendering. PerDrawCB carries per-texture filtering parameters (used only when `FILTER_THREE_POINT` is active, lines 80–99 in shader).

**Binding sequence** (gfx_direct3d11.cpp:712–713): In `StartFrame()`: `mContext->PSSetConstantBuffers(0, 2, buffers)` binds both CBs to slots 0 and 1.

**Per-frame update** (gfx_direct3d11.cpp:715–719): `mPerFrameCbData.noise_frame++`, wraps at 150.

**Per-draw update** (gfx_direct3d11.cpp:662–668): Only mapped when `textures_changed` is true. Copies `mPerDrawCbData` to GPU via `D3D11_MAP_WRITE_DISCARD`.

**Per-frame upload** (gfx_direct3d11.cpp:834–838): In `StartDrawToFramebuffer()`: maps `mPerFrameCb` with `D3D11_MAP_WRITE_DISCARD`, copies `mPerFrameCbData`.

**RESOURCE:** `mPerFrameCb` (gfx_direct3d_common.h:135) — `ComPtr<ID3D11Buffer>`  
**RESOURCE:** `mPerDrawCb` (gfx_direct3d_common.h:136) — `ComPtr<ID3D11Buffer>`

---

## 10. Vertex Buffer Assembly (GfxSpTri1)

**PATH:** `libultraship/src/fast/interpreter.cpp` — `Interpreter::GfxSpTri1()` (line 1356)

**FUNCTION:** `GfxSpTri1(uint8_t vtx1_idx, uint8_t vtx2_idx, uint8_t vtx3_idx, bool is_rect)` (interpreter.h:416)

**RESPONSIBILITY:** Assembles 3 vertices into a triangle, performs all RDP state comparisons, imports textures, selects shaders, and fills `mBufVbo[]` with 32 floats per vertex.

**GfxSpTri1() algorithm (lines 1356–1777):**

**Phase 1 — Geometry tests (lines 1364–1406):**
1. Full triangle clip rejection: if all 3 vertices have matching clip_rej bits, triangle is culled.
2. Culling test: cross product of screen-space edges, with invert-culling support for f3dex2 via `G_EX_INVERT_CULLING`.

**Phase 2 — State comparison and flush (lines 1408–1606):**
1. Depth test/mask comparison (lines 1408–1415): If changed, `Flush()` then `mRapi->SetDepthTestAndMask()`.
2. Zmode decal comparison (lines 1417–1422): If changed, `Flush()` then `mRapi->SetZmodeDecal()`.
3. Viewport/scissor comparison (lines 1424–1436): If changed, `Flush()` then `mRapi->SetViewport()/SetScissor()`.
4. Combiner key construction (lines 1438–1510): combines `mRdp->combine_mode` + computed options (alpha, fog, texture_edge, noise, 2cyc, alpha_threshold, invisible, grayscale, mask, blend, shader).
5. `LookupOrCreateColorCombiner(key)` (line 1512).
6. Texture import (lines 1514–1594): For each texture slot (0,1): if combiner uses texture and textures changed, `Flush()` then `ImportTexture()`. Also imports mask and replacement textures.
7. Shader selection: `comb->prg[tm]` or `LookupOrCreateShaderProgram()` (lines 1596–1606). If changed, `Flush()`, `mRapi->UnloadShader()/LoadShader()`.
8. Alpha blend comparison (lines 1607–1611).

**Phase 3 — Vertex buffer emission (lines 1619–1771):** For each of 3 vertices, emits exactly 32 floats per vertex (MAX_VERTICES stride):
1. **Position** (lines 1625–1628): x, y (inverted if `invertY`), z, w.
2. **Texture coordinates** (lines 1630–1677): u, v (normalized by tex_width/height with shifts/UL/VT adjustments), plus texClampS/T if applicable.
3. **Fog** (lines 1680–1685): r, g, b, fog_factor (if `use_fog`).
4. **Grayscale** (lines 1687–1692): r, g, b, lerp_factor (if `use_grayscale`).
5. **Combiner inputs** (lines 1694–1764): For each `numInputs` (from shader), emits 1 or 2 RGBA values based on `shader_input_mapping`. Sources: primitive, shade, environment, primitive_alpha, env_alpha, prim_lod_frac, lod_fraction, texel.

**Flush trigger:** If triangle count reaches `MAX_TRI_BUFFER` (256), `Flush()` is called (line 1773).

---

## 11. Draw Call Path (DrawTriangles)

**PATH:** `libultraship/src/fast/backends/gfx_direct3d11.cpp` — `GfxRenderingAPIDX11::DrawTriangles()` (line 574)  
**INTERFACE:** `GfxRenderingAPI::DrawTriangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris)` (gfx_rendering_api.h:50)

**RESPONSIBILITY:** Executes a single draw call: updates all DX11 pipeline state, uploads vertex data, binds shaders/input layout/samplers, and calls `mContext->Draw()`.

**DrawTriangles() algorithm (lines 574–704):**

**1. Depth state update** (lines 576–595): If `mLastDepthTest/mLastDepthMask` changed, recreates `mDepthStencilState` (DepthEnable, DepthWriteMask, DepthFunc = LESS_EQUAL for decal / LESS for non-decal / ALWAYS for no depth test).

**2. Rasterizer state update** (lines 597–635): If `mLastZmodeDecal` changed, recreates `mRasterizerState` with SlopeScaledDepthBias: -2 (default), -1*height/120 (scaled z-fighting mode 1), -1*height/100 (no vanishing mode 2). Controlled by `CVAR_Z_FIGHTING_MODE` (line 615).

**3. Texture/sampler binding** (lines 637–658): For each texture slot used by shader:
   - If resource view changed → `PSSetShaderResources(i, 1, ...)`.
   - If three-point filtering active → updates PerDrawCB texture dimensions/filtering (lines 646–649).
   - Updates sampler state.

**4. PerDrawCB update** (lines 662–668): Mapped with `D3D11_MAP_WRITE_DISCARD`, memcpy of `mPerDrawCbData`.

**5. Vertex buffer upload** (lines 670–676): Maps `mVertexBuffer` with `D3D11_MAP_WRITE_DISCARD`, memcpy of `buf_vbo`.

**6. Vertex buffer binding** (lines 678–684): Stride = `numFloats * sizeof(float)`, offset = 0.

**7. Shader/input layout binding** (lines 686–696): If shader changed → `IASetInputLayout`, `VSSetShader`, `PSSetShader`, `OMSetBlendState`.

**8. Topology** (lines 698–701): `D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST`.

**9. Draw call** (line 703): `mContext->Draw(buf_vbo_num_tris * 3, 0)`.

---

## 12. Framebuffer System

**PATH:** `libultraship/src/fast/backends/gfx_direct3d11.cpp`  
**STRUCT:** `FramebufferDX11` (gfx_direct3d_common.h:43–50)

**STRUCT:** `TextureData` (gfx_direct3d_common.h:34–41): `ComPtr<ID3D11Texture2D> texture`, `ComPtr<ID3D11ShaderResourceView> resource_view`, `ComPtr<ID3D11SamplerState> sampler_state`, `width`, `height`, `linear_filtering`.

**FUNCTION:** `CreateFramebuffer()` (line 729) — Creates a new `TextureData` via `NewTexture()`, allocates `FramebufferDX11`, sets up sampler with wrap/clamp defaults.

**FUNCTION:** `UpdateFramebufferParameters(int fb_id, uint32_t width, uint32_t height, uint32_t msaa_level, bool opengl_invertY, bool render_target, bool has_depth_buffer, bool can_extract_depth)` (line 747)

**UpdateFramebufferParameters() algorithm (lines 747–819):**
1. Clamps width/height to minimum 1 (line 753–754).
2. If `mFeatureLevel < D3D_FEATURE_LEVEL_10_1`, forces `msaa_level = 1` (line 756).
3. Reduces msaa_level while quality levels are 0 (lines 757–759).
4. If size or msaa changed, or render_target status changed:
   - **fb_id != 0**: Creates new `ID3D11Texture2D` with `DXGI_FORMAT_R8G8B8A8_UNORM`, sample count = msaa_level, bind flags = shader resource (if msaa<=1) + render target. Creates SRV if msaa<=1.
   - **fb_id == 0**: Gets swap chain desc, calls `ResizeBuffers()` if dimensions differ, gets back buffer `ID3D11Texture2D`.
   - Creates `ID3D11RenderTargetView` if `render_target`.
5. If depth buffer needed and changed: calls `CreateDepthStencilObjects()`.
6. If no depth buffer: resets depth stencil views.

**CreateDepthStencilObjects()** (line 57): Creates `ID3D11Texture2D` with depth format (`D32_FLOAT` for FL10.0+, `D24_UNORM_S8_UINT` for FL9.x), `ID3D11DepthStencilView` (TEXTURE2DMS if MSAA, TEXTURE2D otherwise), optional `ID3D11ShaderResourceView`.

**StartDrawToFramebuffer()** (line 821): Sets render targets via `OMSetRenderTargets`, sets `mCurrentFramebuffer`, updates noise scale, uploads PerFrameCB.

**ClearFramebuffer()** (line 841): `ClearRenderTargetView` (color=black), `ClearDepthStencilView` (depth=1.0).

**ResolveMSAAColorBuffer()** (line 852): `ResolveSubresource` from MSAA texture to single-sample texture with `DXGI_FORMAT_R8G8B8A8_UNORM`.

**MSAA handling (interpreter.cpp):**
- `mMsaaLevel` read from `gMSAAValue` CVAR (line 4190).
- In `Run()`: if `mMsaaLevel > 1` and rendering to FB: `ResolveMSAAColorBuffer(mGameFbMsaaResolved, mGameFb)` (line 4352) when NOT viewport-matched, or `ResolveMSAAColorBuffer(0, mGameFb)` (line 4355) when viewport-matched.
- `mGameFbMsaaResolved` is only created when MSAA > 1 AND not viewport-matched (line 4292).

**Framebuffer create/resize in interpreter:**
- `CreateFrameBuffer()` (line 4392): calls `mRapi->CreateFramebuffer()` then `UpdateFramebufferParameters(fb, w, h, 1, ...)`.
- `FB_CreateFramebuffers()` (framebuffer_effects.c:17): creates gPauseFrameBuffer, gBlurFrameBuffer, gReusableFrameBuffer, gN64ResFrameBuffer via `gfx_create_framebuffer()`.

---

## 13. Post-Present Path (EndFrame, SwapChain)

**PATH:** `libultraship/src/fast/interpreter.cpp` (`Interpreter::EndFrame()`, line 4369)  
**PATH:** `libultraship/src/fast/backends/gfx_dxgi.cpp` (`GfxWindowBackendDXGI::SwapBuffersBegin()` line 869, `SwapBuffersEnd()` line 910)  
**PATH:** `libultraship/src/fast/backends/gfx_dxgi.cpp` (`GfxWindowBackendDXGI::CreateSwapChain()` line 1014)

**EndFrame()** (interpreter.cpp:4369–4374):
```cpp
void Interpreter::EndFrame() {
    mRapi->EndFrame();            // line 4370 — Flush()
    mWapi->SwapBuffersBegin();     // line 4371 — Present()
    mRapi->FinishRender();         // line 4372 — no-op for DX11
    mWapi->SwapBuffersEnd();        // line 4373 — frame latency wait
}
```

**SwapBuffersBegin()** (gfx_dxgi.cpp:869–908): Frame rate limiter using waitable timer + yield loop, then `swap_chain->Present(mVsyncEnabled, flags)`. Supports `DXGI_PRESENT_ALLOW_TEARING` when VSync is off and tearing is supported.

**SwapBuffersEnd()** (gfx_dxgi.cpp:910–953): Applies max frame latency adjustments, waits on `mWaitableObject` (if present), updates frame statistics.

**CreateSwapChain()** (gfx_dxgi.cpp:1014–1043):
- `DXGI_SWAP_CHAIN_DESC1`: BufferCount=3, Format=`R8G8B8A8_UNORM`, BufferUsage=`RENDER_TARGET_OUTPUT`, Scaling=`NONE`/`STRETCH`, SwapEffect=`FLIP_DISCARD` (DXGI 1.4+) / `FLIP_SEQUENTIAL`, Flags=`FRAME_LATENCY_WAITABLE_OBJECT` + `ALLOW_TEARING`.
- `CreateSwapChainForHwnd()` with `h_wnd`.
- `mSwapChainDevice` stores device/command queue reference.

**EndFrame() in D3D11 backend** (gfx_direct3d11.cpp:722–724): `mContext->Flush()`.

**CONFIG/CVAR:** `CVAR_VSYNC_ENABLED` (SwapBuffersBegin:873) — controls `mVsyncEnabled` for Present() flags.  
**CONFIG/CVAR:** `gMSAAValue` (cmake/cvars.cmake:4) — MSAA level.

**Frame rate limiting:**
- `FRAME_INTERVAL_NS_NUMERATOR = 1000000000` (gfx_dxgi.cpp:41)
- `FRAME_INTERVAL_NS_DENOMINATOR = mTargetFps` (line 42)
- `mMaxFrameLatency` default = 2 (gfx_dxgi.cpp:529), applied via `SetMaximumFrameLatency()`.

---

## Resource/Shader Reference

| Resource | Path | Purpose |
|----------|------|---------|
| default.shader.hlsl | `libultraship/src/fast/shaders/directx/default.shader.hlsl` | Main HLSL shader template (Prism-processed) |
| dx_include_fs | gfx_direct3d11.cpp:1265 | Shader include loader (loads from OTR resources) |
| Prism processor | `libultraship/src/fast/backends/gfx_direct3d11.cpp:1284` | Template processor for shader generation |

## CVAR Reference

| CVAR | Defined in | Default | Purpose |
|------|-----------|---------|---------|
| gMSAAValue | cmake/cvars.cmake:4 | 1 | MSAA sample count (2x–8x) |
| gInternalResolution | interpreter.cpp:4188 | 1 | Internal resolution multiplier |
| gVsyncEnabled | gfx_dxgi.cpp:873 | 1 | VSync enable flag |
| CVAR_Z_FIGHTING_MODE | gfx_direct3d11.cpp:615 | 0 | Z-fighting mode (0=off, 1=N64, 2=no vanish) |
| CVAR_TEXTURE_FILTER | Fast3dWindow.cpp:101 | 1 | Texture filter mode (0=none, 1=three-point, 2=linear) |

---