# G02 — Fallback Validation

The fallback is intentionally the existing N64 Z glyph. `Font_LoadChar` only substitutes when `character == 0x84` and `GlyphResolver_GetZTexture()` returns a non-null resource path. A null result falls through to the unchanged `fontTbl[character]` copy.

| Condition | Resolver result | Render path | Evidence |
|---|---|---|---|
| `GlyphProfile = 0` | `NULL` | Original N64 Z | Static source review; no message data changed |
| Dynamic profile, no control deck | `NULL` | Original N64 Z | Explicit null guard |
| Dynamic profile, no Player 1 controller | `NULL` | Original N64 Z | Explicit null guard |
| Missing `BTN_Z` button | `NULL` | Original N64 Z | Explicit null guard |
| Digital or non-SDL mapping | `NULL` | Original N64 Z | Physical device type filter |
| SDL mapping that is not an axis trigger | `NULL` | Original N64 Z | `AxisIsTrigger()` guard |
| Trigger is not `LT` | `NULL` | Original N64 Z | Exact physical-name comparison |
| Generic SDL device name | `NULL` | Original N64 Z | Xbox detection requires explicit Xbox/XInput marker |
| Explicit Xbox/XInput plus Player 1 Z mapped to LT | LT OTR path | Dynamic LT asset | Compiled implementation; runtime hardware proof pending |

No fallback path changes controller mappings, input state, simulation, HUD, Ocarina, localization, or save data.
