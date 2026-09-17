# 10 — V03 PILOT SCENES

## Test Scene Selection and Validation Criteria

---

### 1. Pilot Scene Criteria

| Criterion | Reason |
|---|---|
| Diverse material types | Test multiple PBR categories |
| Mixed indoor/outdoor | Test fog, lighting, exposure |
| Known performance baseline | Measure delta accurately |
| Easily reproducible | Same scene every test run |
| User familiarity | Visual changes immediately obvious |
| Multiple lighting conditions | Day/night, interior/exterior |

---

### 2. Scene 1: Temple of Time

**Why**: Diverse materials, controlled lighting, iconic visuals.

| Material | Present? | Test Focus |
|---|---|---|
| STONE | YES — walls, floor, pillars | PBR roughness response |
| METAL | YES — door handles, decorations | Metallic specular highlights |
| EMISSIVE | YES — magic circle, stained glass | Emissive glow |
| WOOD | YES — pews, altar details | Matte wood response |
| CLOTH | YES — Zelda's dress, curtains | Fabric material |
| SKIN | YES — NPCs, Link | Subsurface hint |
| UI_2D | YES — HUD elements | Protection verification |

**Test scenarios:**
- Enter temple from outside → test exposure transition
- Walk to altar → test light shaft interaction with materials
- Open inventory in temple → verify UI protection
- Day vs night exterior → test atmospheric fog settings

**Performance metrics:**
- Draw call count (baseline)
- Frame time delta
- Material classification accuracy

---

### 3. Scene 2: Kokiri Forest

**Why**: Nature scene, dense foliage, ambient fog, wood materials.

| Material | Present? | Test Focus |
|---|---|---|
| WOOD | YES — houses, fences, bridges | Matte wood PBR |
| FOLIAGE | YES — trees, bushes | Alpha-blended foliage response |
| GRASS | YES — ground cover | Semi-transparent grass |
| STONE | YES — path stones, deku tree face | Rock/stone PBR |
| WATER | YES —溪流, pond | Water special path |
| SKIN | YES — Kokiri characters | Skin material |
| MAGIC | YES — fairy particles | Emissive magic |

**Test scenarios:**
- Walk through forest → test foliage alpha with PBR
- Approach water → test water Fresnel response
- Night in forest → test atmospheric fog with fairy emissive
- Look at deku tree → test large-scale wood/stone materials

**Performance metrics:**
- Alpha-blended draw calls with PBR
- Water rendering cost
- Foliage depth complexity

---

### 4. Scene 3: Hyrule Field

**Why**: Wide open vista, distance rendering, sky, terrain, performance stress test.

| Material | Present? | Test Focus |
|---|---|---|
| EARTH | YES — terrain, cliffs | Terrain PBR |
| GRASS | YES — field grass | Grass response at distance |
| STONE | YES — castle walls, bridges | Distant stone PBR |
| WATER | YES — castle moat | Distant water |
| SKY | YES — skybox | Sky rendering |
| METAL | YES — gates, fences | Distant metallic highlights |

**Test scenarios:**
- Look toward Hyrule Castle → test distance atmospheric fog
- Run across field → test frame time under load
- Look at sun → test tonemapping on bright sky
- Enter castle → test indoor/outdoor transition
- Night → test star visibility with ACES tonemapping

**Performance metrics:**
- Max frame time (worst case)
- Atmospheric fog GPU cost
- Tonemapping cost on complex scene
- Draw call count for large open area

---

### 5. Test Protocol Per Scene

```
For each scene:
  1. Run with CLASSIC profile → capture baseline metrics
  2. Run with ENHANCED profile → capture enhanced metrics
  3. Compare:
     - Frame time delta
     - Visual difference (screenshot comparison)
     - Material classification log
     - Any rendering artifacts
  4. Verify:
     - No gameplay change
     - No UI corruption
     - Profile switch works mid-scene
     - No memory leaks over 5 minutes
```

---

### 6. Known Risk Areas

| Scene | Risk | Mitigation |
|---|---|---|
| Temple of Time | Emissive glass may over-bloom with ACES | Adjust emissive strength |
| Kokiri Forest | Alpha foliage + PBR = performance hit | Profile gate, fallback |
| Hyrule Field | Atmospheric fog on large vista = GPU cost | Density limits, quality levels |
| All scenes | Material misclassification | UNKNOWN fallback, logging |

---

### 7. Screenshot Comparison Points

For each scene, capture:

| View | Purpose |
|---|---|
| Default camera position | Overall scene impression |
| Close-up of key material | PBR response verification |
| Wide vista | Atmospheric fog verification |
| UI overlay | Protection verification |
| Night version | Lighting change handling |
| Profile switch mid-scene | Transition safety |

---

### 8. Pass Criteria

| Criterion | Requirement |
|---|---|
| No visual regression in CLASSIC | Pixel-identical (within float tolerance) |
| ENHANCED shows visible improvement | Subjective quality improvement noted |
| Frame time within budget | < +2ms at 3440×1440 |
| No UI corruption | All HUD elements unchanged |
| No gameplay change | All interactions identical |
| Profile switch safe | No crash, no resource leak |
| Material classification >80% correct | Manual verification |
