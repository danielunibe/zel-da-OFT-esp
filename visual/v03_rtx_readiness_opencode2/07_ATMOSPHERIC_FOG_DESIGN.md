# 07 — ATMOSPHERIC FOG DESIGN

## Modern Atmospheric Depth System for OoT

---

### 1. Current N64 Fog Behavior

**How it works:**
```
fog_factor = z * (1/w) * fog_mul + fog_offset    [clamped 0-255]
final_color = lerp(texel.rgb, fog_color, fog_factor)
```

**Characteristics:**
- Linear distance response (z/w is linear in screen space)
- Per-vertex interpolation (smooth across triangle)
- Game-controlled fog color and density
- Activated per draw call via `G_FOG` geometry mode bit
- Fog parameters set per-scene by game code via `G_MW_FOG`

**Limitations:**
- No height-based fog
- No scattering model
- No scene-reactive density
- Binary on/off (no gradual density control beyond mul/offset)
- Same fog color for all objects in scene

---

### 2. Design Goals for V03 Atmospheric Enhancement

| Goal | Constraint |
|---|---|
| Preserve original fog intent | Game-set fog_color and fog_mul/offset must still work |
| No gameplay visibility change | Fog must not be reduced below N64 intent |
| Classic fallback | ENHANCED profile only |
| Scene-aware | Different response for indoor vs outdoor |
| Avoid global blue haze | No uniform atmospheric tint |
| Depth-based | Use depth buffer, not just z/w |
| Configurable strength | User-adjustable enhancement factor |

---

### 3. Proposed Atmospheric System

**Layer approach: Layer modern atmospheric depth ON TOP of existing N64 fog.**

```
final_color = lerp(
    ACES_tonemapped_color,           // PBR-lit scene color
    atmospheric_fog_color,           // scene-appropriate fog color
    atmospheric_fog_factor           // depth-based fog factor
)
```

**Then the original N64 fog is STILL applied in the vertex shader** as part of the base pipeline. The atmospheric enhancement adds a second, depth-buffer-based fog layer in a post-process pass.

---

### 4. Depth-Based Fog Factor

```hlsl
// In post-process pixel shader:
float sceneDepth = depthBuffer.Sample(sampler, uv).r;

// Linearize depth
float linearDepth = nearPlane * farPlane / (farPlane - sceneDepth * (farPlane - nearPlane));

// Distance-based fog factor (0 = no fog, 1 = full fog)
float fogFactor = 1.0 - exp(-density * linearDepth);

// Height-based component (optional)
float heightFactor = saturate((cameraY - worldY) / heightFalloff);
fogFactor *= lerp(1.0, heightFactor, heightInfluence);

// Final fog blend
float3 foggedColor = lerp(sceneColor, fogColor, saturate(fogFactor * strength));
```

---

### 5. Distance Response

| Parameter | Range | Default | Purpose |
|---|---|---|---|
| `density` | 0.001 - 0.1 | 0.01 | Controls fog onset distance |
| `strength` | 0.0 - 1.0 | 0.5 | Enhancement factor over N64 fog |
| `startDistance` | 0.0 - 1000.0 | 50.0 | Distance before fog begins |
| `endDistance` | 100.0 - 5000.0 | 500.0 | Distance at full fog |

**Response curve:** Exponential falloff (not linear) for natural atmospheric appearance.

---

### 6. Color Response

| Scenario | Fog Color Source |
|---|---|
| Game sets fog color | Use game's fog_color (preserves artistic intent) |
| Outdoor scene | Slightly blue-shifted sky color blend |
| Indoor scene | Use ambient/indoor color, minimal atmospheric enhancement |
| Underground | Very dim, warm fog (torch light influence) |
| Water surface | Blue-green, density increases with depth |

**Rule: NEVER override game-set fog color with a hardcoded blue.**

---

### 7. Height Component

Optional height-based fog for outdoor scenes:

```hlsl
// Objects at ground level get more fog
// Objects at high altitude get less fog
float heightFade = saturate((worldY - groundY) / heightRange);
float heightFogFactor = fogFactor * lerp(1.0, 0.2, heightFade);
```

**Use cases:**
- Hyrule Field: mist at ground level, clear at castle height
- Kokiri Forest: low fog between trees
- Temple of Time: minimal height fog (interior)

---

### 8. Indoor vs Outdoor Rules

| Scene Type | Detection | Fog Behavior |
|---|---|---|
| Outdoor | Scene flags / no ceiling geometry | Full atmospheric enhancement |
| Indoor | Scene flags / ceiling present | Minimal or no atmospheric fog |
| Transition | Scene boundary | Blend over 1-2 seconds |
| Underwater | Scene state | Original N64 fog only, no enhancement |

**Detection method:** Use scene metadata or heuristic (ceiling detection via depth buffer analysis).

---

### 9. Per-Scene Strength Configuration

| Scene | Recommended Density | Recommended Strength | Notes |
|---|---|---|---|
| Temple of Time | 0.005 | 0.2 | Interior, minimal atmospheric |
| Kokiri Forest | 0.02 | 0.6 | Dappled light, moderate fog |
| Hyrule Field | 0.008 | 0.5 | Wide open, distance haze |
| Lost Woods | 0.03 | 0.7 | Dense, mystical atmosphere |
| Death Mountain | 0.01 | 0.4 | High altitude, thin atmosphere |
| Water Temple | 0.0 | 0.0 | Underwater, N64 fog only |
| Shadow Temple | 0.015 | 0.3 | Dark, minimal atmospheric |
| Gerudo Valley | 0.01 | 0.4 | Canyon, moderate distance haze |

---

### 10. Implementation Requirements

| Requirement | Source |
|---|---|
| Depth buffer access | `gfx_direct3d11.cpp` — DSV already exists with SRV for readback |
| Post-process shader | New HLSL pass (or extend tonemapping shader) |
| Camera parameters | Near/far plane, position, direction |
| Scene metadata | Scene ID for per-scene configuration |
| Profile gate | ENHANCED only |

---

### 11. What Must NOT Happen

| Anti-Pattern | Reason |
|---|---|
| Uniform global blue tint | Looks artificial, not atmospheric |
| Fog that reduces gameplay visibility | Unfair to player |
| Fog that affects UI elements | PROTECTED_2D exclusion |
| Fog that changes with camera angle | Must be world-space, not screen-space |
| Fog that flickers | Must be temporally stable |
| Fog that costs >0.5ms GPU | Performance budget |

---

### 12. Relationship to N64 Fog

```
V03 Enhanced Pipeline:
  1. N64 vertex fog applied in vertex shader (unchanged)
  2. N64 fog blended in pixel shader (unchanged)
  3. ACES tonemapping applied (new)
  4. Atmospheric depth fog applied (new, post-process)
  5. Final output to backbuffer
```

The N64 fog and atmospheric fog are **additive layers**, not replacements. This preserves the original game's artistic fog decisions while adding modern atmospheric depth on top.
