# TEMPLE OF TIME MASTER PILOT REPORT

**Scene Identifier:** `tokinoma` (Indoor Sacred Sanctuary)  
**Release Candidate:** Ocarina Couch Edition V03.1  
**Author:** Gemini 3.8 Master Integration Owner  
**Date:** 2026-09-17  
**Status:** VALIDATED IN PILOT  

---

## 1. Scene Profile & Scope

The Temple of Time (`tokinoma`) serves as the primary indoor visual benchmark for architectural stability, stone surface reproduction, and pristine interior lighting. It contains:
- **73 scene records** (33 scene textures, predominantly stone architecture and altars).
- **21 object group records** (`object_toki_objects`: Door of Time, Pedestal of Time, Spiritual Stone altars).
- Shared objects including Master Sword components (`gLinkChildMasterSword*Tex`, `gLinkAdult*`).

---

## 2. Material Classification & Rules

| Surface / Asset | Discovered Category | Material Rules Applied | Pilot Behavior |
|---|---|---|---|
| Master Sword Blade / Guard / Pommel | `METAL` | Roughness: 0.25, Metallic: 0.95, Specular: 0.90 | High specular response metadata; clean diffuse |
| Triforce Emblem | `METAL` / Emissive | Roughness: 0.20, Metallic: 0.90, Emissive: true | Tagged for sacred golden reflection |
| Pedestal of Time & Floor Tiles | `STONE` | Roughness: 0.70, Metallic: 0.05, Specular: 0.15 | Clean stone matte response, zero ACES clipping |
| Light Shafts (`ovl_Bg_Toki_Hikari`) | Alpha / FX | Additive blend pass | Bypasses opaque BRDF; preserved via alpha blend |
| Interior Fog / Ambient | F3DEX2 Fog | `fog_color` = (0, 0, 0, 0) (Indoor clear) | ACES linear passthrough, no artificial distance haze |

---

## 3. UI Isolation & Parity

- **In-Game HUD:** Hearts, Magic Meter, Rupees, and A/B/C action icons render strictly during 2D overlay (`ACES_UI_CONTAMINATION = 0`).
- **Color Fidelity:** Stone textures remain true to the original art direction with enhanced contrast and dynamic range roll-off in highlights.
- **Classic Parity:** When toggled to Classic mode, all post-processing shuts down, reproducing pixel-exact native N64 rendering.
