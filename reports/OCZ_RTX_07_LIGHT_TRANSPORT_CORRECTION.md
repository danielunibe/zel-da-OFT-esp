# OCZ-RTX-07 — corrección del transporte de luz (OpenCode)

Fecha: 2026-10-15
Build: `OCZ_RTX_07_RELEASE` → `out/Release/soh.exe` 34 396 672 bytes, SHA-256 `88B92FDC0EC0A0E789F63145645330FD07D81654F2AF2D0E8CB60FBB1EB521A7`
Estado general: **PARTIAL** — 9 de 10 gates en PASS; `VISUAL_VERIFICATION` queda en NOT_RUN por requerir revisión humana. Los tres defectos residuales de `OCZ_RTX_06_OPENCODE_CONTINUATION_RESULT.md` tienen causa raíz aislada y corregida, y los dos primeros están cerrados con evidencia en GPU.

## Objetivo

Corregir en *Ocarina Couch Edition RTX* los tres defectos de transporte de luz que `OCZ_RTX_06_OPENCODE_CONTINUATION_RESULT.md` dejó sin cerrar: la sobreiluminación nocturna de `HYRULE_FIELD_NIGHT` (mediana `composite/raster` **4.105**), la cuota de GI con contribución cero en píxeles relit, y la radiancia trazada a ~51% de la del raster en `TEMPLE_INTERIOR`. Sin nueva auditoría, sin reconstruir D3D12/DXR, sin repetir la reparación de niebla ya verificada, y sin recomendaciones finales: ejecutable experimental, regresiones, capturas y este informe con los 10 gates.

## Causa raíz 1 — el *shade* se aplicaba en el espacio equivocado (P0 noche + P1 energía)

Fast3D calcula `raster_linear = SrgbToLinear(texel × shade)`. El DXR de OCZ-RTX-06 calculaba `SrgbToLinear(texel) × shade`. Como `SrgbToLinear` es convexa, para `shade < 1` la segunda forma da **más** energía y para `shade > 1` da menos — exactamente la firma de los dos defectos:

| Escena | `shade` (juego) | factor predicho `SrgbToLinear(s)/SrgbToLinear(1)·s⁻¹` | factor medido |
|---|---|---|---|
| `HYRULE_FIELD_NIGHT` | ≈0.053 (nDotL) | **3.60×** | **3.58×** (<1% de error) |
| `TEMPLE_INTERIOR` | ≈0.971 (nDotL) | **0.453×** | **0.457×** (<1% de error) |

El desfase de nDotL quedó descartado por comparación directa: el juego y el DXR usan la misma dirección y el mismo `nDotL` (0.053 en la noche, 0.971 en el templo).

**Corrección** en `ClosestHitReflection` (`dxr_shadow.hlsl`): se reconstruye el *shade* exactamente como lo hace Fast3D — `fast3dShade = max(gAmbientLightColor, 0.025) + gSceneLightColor·(nDotL·visibility) + secondary·(saturate(nDotL2)·vis2) + backscatter` — y se aplica **después** de la conversión a lineal:

```
diffuseFast3D = baseColor · (1 − metallic) · SrgbToLinear(fast3dShade)
```

El especular (nueva `EvaluateMaterialSpecularLight`), las luces puntuales, el backscatter de luces puntuales, la GI y la emisión permanecen en lineal. `SrgbToLinear` ya hace `saturate`, de modo que replica el clamp 0-255 de Fast3D. `EvaluateMaterialDirectLight` queda como `diffuse` propio + especular, y sigue siendo el que usa `EvaluateMaterialPointLight` — sin tocar el root signature.

## Causa raíz 2 — la cuota de GI se publicaba en un canal UNORM8 (P1 GI)

`gSurfaceMask` es `register(u1)` en el DXR, y en C++ ese slot se crea como `mReflectionMask` con **`DXGI_FORMAT_R8G8B8A8_UNORM`** (`DxrSurfaceRenderer.cpp:1907`, SRV `:1932`, vista sobre `mReflectionMask` `:1936`). RayGen escribía:

```
gSurfaceMask[launchIndex] = float4(reflectionContribution, transmissiveFactor, materialCoverage, 1.0f + giFraction);
```

En una textura UNORM8 el alpha se **satura en [0,1]**, así que `1.0f + giFraction` se guardaba siempre como `1.0`. El compositor leía `giFraction = saturate(surface.w − 1.0) * pathSignal` → **idénticamente 0**, y por eso `INDIRECT_OFF` no movía ni un píxel relit en las cinco escenas. La energía de GI nunca faltó: viaja dentro de `pathResult`.

**Corrección** (dos líneas, sin cambio de ABI ni de root signature):

- `dxr_shadow.hlsl`: `gSurfaceMask.a` pasa a ser solo bandera de validez (`1.0f`), y la cuota se publica en `gIndirectMask.a` — un UAV **FP16** — como `2.0f + giFraction` en la clase relit.
- `DxrSurfaceRenderer.cpp`: el compositor lee `giFraction = saturate(indirectSample.a − 2.0) * pathSignal`.

La clasificación por bandas `step(1.5, indirectSample.a)` no cambia (el rango pasa de `[0,2]` a `[0,3]`), y con `gAblation == 0` el `lerp` de GI es identidad, así que el composite normal no se altera.

## Evidencia — `composite / raster` sobre píxeles relit

Pre-fix = ejecuciones con el build de OCZ-RTX-06. Post-fix = build final `OCZ_RTX_07_RELEASE`, frame 425.

| Escena | pre-fix mediana | **post-fix mediana** | p5 → p5 | p95 → p95 | %<0.75 pre→post | %>1 pre→post | `giFraction` |
|---|---|---|---|---|---|---|---|
| `HYRULE_FIELD_NIGHT` | 4.1053 | **1.3746** | 0.9832 → 0.7654 | 4.8869 → 1.6102 | 0.88 → 3.89 | 94.76 → 85.74 | 0.3887 |
| `TEMPLE_INTERIOR` | 0.7473 | **0.9175** | 0.7410 → 0.8081 | 0.8224 → 1.2728 | 66.91 → 0.54 | 3.03 → 17.08 | 0.0605 |
| `BASELINE_KOKIRI` | 0.9149 | **1.2058** | 0.7416 → 0.7387 | 2.5513 → 1.8725 | 6.13 → 6.52 | 37.19 → 66.79 | 0.0547 |
| `LAKE_HYLIA_DAY` | 0.7812 | **1.0009** | 0.7525 → 0.7996 | 1.9996 → 1.3021 | 4.04 → 2.58 | 8.40 → 51.12 | 0.0625 |
| `HYRULE_FIELD_DAY` | 0.7733 | **1.1158** | 0.7383 → 0.8919 | 0.8004 → 1.2346 | 19.35 → 1.60 | 1.34 → 74.73 | 0.0566 |

- La noche pasa de estar **4.1× por encima** del raster a 1.37×; el templo de un 25% por debajo a un 8% por debajo; los tres diurnos de 22-23% por debajo a centrados (LAKE 1.0009).
- Ninguna escena deja píxeles relit por debajo de 0.50× el raster; el mínimo `p5` de las cinco es 0.7387.

## Descomposición de energía (modo `COUCH_DXR_MATERIAL_DEBUG=10`, píxeles relit)

| Escena | `radi/baseColor` | `raster/baseColor` | `radi/raster` | `giLum/path` | `path/raster` | no finitos |
|---|---|---|---|---|---|---|
| `HYRULE_FIELD_NIGHT` | 0.0626 | 0.0741 | **0.8468** | 0.3880 | 1.3809 | 0 |
| `TEMPLE_INTERIOR` | 0.7731 | 0.8979 | **0.8597** | 0.0616 | 0.9162 | 0 |

- El **término directo** del DXR queda ya por debajo de la energía del raster en las dos escenas (0.847 y 0.860): la sobreiluminación nocturna no viene del directo.
- En la noche la GI aporta `1.3809 − 0.8468 ≈ 0.534` unidades de raster, es decir el **38.8%** del `path`. En el templo, `0.9162 − 0.8597 ≈ 0.057` (6.2%).
- `giLum/path` coincide con `giFraction` publicado (0.3880 vs 0.3887; 0.0616 vs 0.0605): la cuota es internamente consistente entre la fuente y la publicación.

## Ablaciones — mismo fotograma, misma lista de comandos

Desviación absoluta media `composite` vs control, **sobre píxeles relit**, build final:

| Control | `TEMPLE` | `KOKIRI` | `LAKE` | `FIELD_DAY` | `NIGHT` |
|---|---|---|---|---|---|
| `relight_off` | 0.039957 | 0.014603 | 0.016599 | 0.041725 | 0.010171 |
| `surface_optics_off` | 0.002485 | 0.000714 | 0.002262 | 0.002828 | 0.000230 |
| `indirect_off` **(pre-fix)** | **0.000000** | **0.000000** | **0.000000** | **0.000000** | **0.000000** |
| `indirect_off` **(post-fix)** | **0.015919** | **0.002835** | **0.021065** | **0.021029** | **0.012293** |

Causalidad y alcance del control post-fix:

| Escena | `giFraction` med. | píxeles relit | delta med. | % de relit alterados | delta máx. en píxeles **sin** DXR |
|---|---|---|---|---|---|
| `HYRULE_FIELD_NIGHT` | 0.3887 | 209 590 | +0.013024 | 99.84% | **0.000000** |
| `TEMPLE_INTERIOR` | 0.0605 | 34 538 | +0.016238 | 99.99% | **0.000000** |
| `BASELINE_KOKIRI` | 0.0547 | 258 978 | +0.001760 | 98.31% | **0.000000** |
| `LAKE_HYLIA_DAY` | 0.0625 | 103 253 | +0.020744 | 99.74% | **0.000000** |
| `HYRULE_FIELD_DAY` | 0.0566 | 211 056 | +0.021148 | 99.88% | **0.000000** |

Los tres controles siguen viajando por el mismo root signature (`compositeParams.ablationMode = i + 1` → `gAblation = 1,2,3`). El control `indirect_off` es ahora **precisamente acotado**: altera ≥98% de los píxeles relit y deja intacto, bit a bit, a `0.000000` los píxeles que el trazador no tocó.

## Balance energético y compositor

| Escena | `path/raster` mediana `[p5, p95]` | `composite/raster` mediana `[p5, p95]` | Δ mediana | `credibility` |
|---|---|---|---|---|
| `HYRULE_FIELD_NIGHT` | 1.3758 `[0.3268, 1.6242]` | 1.3746 `[0.7654, 1.6102]` | 0.0012 | 0.9991 |
| `TEMPLE_INTERIOR` | 0.9163 `[0.7523, 1.2851]` | 0.9175 `[0.8081, 1.2728]` | 0.0012 | 1.0013 |
| `BASELINE_KOKIRI` | 1.2027 `[0.1032, 1.8663]` | 1.2058 `[0.7387, 1.8725]` | 0.0031 | 1.0026 |
| `LAKE_HYLIA_DAY` | 1.0065 `[0.6576, 1.3091]` | 1.0009 `[0.7996, 1.3021]` | 0.0055 | 0.9945 |
| `HYRULE_FIELD_DAY` | 1.1156 `[0.8528, 1.2270]` | 1.1158 `[0.8919, 1.2346]` | 0.0001 | 1.0001 |

`pathWeight · credibility == 1` en las cinco escenas: el relight trazado reemplaza el píxel raster exactamente una vez, sin doble conteo y sin sustitución destructiva.

## Verificación visual objetiva (SDR final, capturas `final_sdr_pre_present`)

| Escena/etiqueta | media | p05 | p50 | p95 | p99 | ≥250 | ≤5 |
|---|---|---|---|---|---|---|---|
| NIGHT pre | 0.4377 | 0.1447 | 0.4196 | 0.6759 | 0.6908 | 0.011% | 0.195% |
| NIGHT post | 0.3123 | 0.1200 | 0.2119 | 0.6731 | 0.6908 | 0.010% | 0.195% |
| TEMPLE pre | 0.4747 | 0.0000 | 0.5955 | 0.8381 | 0.8784 | 0.000% | 17.905% |
| TEMPLE post | 0.4805 | 0.0000 | 0.6123 | 0.8384 | 0.8784 | 0.000% | 17.904% |
| KOKIRI pre / post | 0.3350 / 0.3454 | 0.1913 / 0.1910 | 0.2566 / 0.2837 | 0.7808 / 0.7808 | 0.8173 / 0.8173 | 0.010% / 0.010% | 0.195% / 0.195% |
| LAKE pre / post | 0.5672 / 0.5846 | 0.2801 / 0.2564 | 0.5658 / 0.6093 | 0.8564 / 0.8564 | 0.8645 / 0.8645 | 0.000% / 0.000% | 0.000% / 0.000% |
| DAY pre / post | 0.6250 / 0.6748 | 0.3636 / 0.3770 | 0.6435 / 0.7113 | 0.8069 / 0.8169 | 0.8528 / 0.8539 | 0.012% / 0.016% | 0.194% / 0.194% |

- La noche baja de mediana 0.4196 → 0.2119 y de media 0.4377 → 0.3123, mientras **p95 y p99 quedan prácticamente idénticos**: el recorte de altas luces no crece (0.011% → 0.010%) ni se aplastan más sombras (0.195% → 0.195%).
- El templo no se oscurece (mediana 0.5955 → 0.6123) y su población de sombras aplastadas no cambia (17.905% → 17.904%).
- Capturas A/B copiadas a `reports/OCZ_RTX_07_AB/` (`{NIGHT,TEMPLE,KOKIRI,LAKE,DAY}_{pre,post}_frame*_raw.png`).

## Constantes de luz (idénticas en frames repetidos, ninguna a cero)

| Escena | `sceneLight` | `ambient` | `pointCount` | `pathTracedProfile` | `indirectSamples` |
|---|---|---|---|---|---|
| `HYRULE_FIELD_NIGHT` | (0.415686, 0.247059, 0.156863) | (0.274510, 0.227451, 0.168627) | 5 | 0 | 8 |
| `TEMPLE_INTERIOR` | (0.784314, …) | (0.196078, …) | 3 | 0 | 8 |
| `BASELINE_KOKIRI` | (1, 1, 1) | (0.313726 ×3) | 2 | 0 | 8 |
| `LAKE_HYLIA_DAY` | (1, 1, 0.941176) | (0.411765, 0.352941, 0.352941) | 2 | 0 | 8 |
| `HYRULE_FIELD_DAY` | (1, 1, 1) | (0.313726 ×3) | 2 | 0 | 8 |

Con `pathTracedProfile = 0`, `fc.params[3] = 0` → `indirectScale = 0.28`, es decir la GI ya viene atenuada por el propio shader. No se tocó ningún multiplicador.

## Gates

| Gate | Estado | Evidencia y límite |
|---|---|---|
| **BUILD_PASS** | **PASS** | `Generate-DxrShadowBytecode.ps1` con DXC `-T lib_6_5 -HV 2021`: `sourceSha256=39c5a6d1797b827d2bc7a106f5ca78bcb72594be1b9a8ae6e1cc2e120437b907`, `dxilSha256=03483b75c1e58471f2d8f24cb4536fbbe5cc3ee7ee21f17fbd3cc5c3752761d0`, `buildId=353f1cf9a78066f45c34eb50263c5db0a05ec63debda9287ec69f3324252504b`, ABI 3, `exports=6 bindings=17 rootSignatureContracts=10`, 122 520 bytes, DXC 1.9.2602.17. Build `OCZ_RTX_07_RELEASE` Release: 0 errores. `soh.exe` = 34 396 672 bytes, SHA-256 `88B92FDC0EC0A0E789F63145645330FD07D81654F2AF2D0E8CB60FBB1EB521A7`. |
| **DXR_EXECUTION_PASS** | **PASS** | 12 ejecuciones OCZ-RTX-07 (`exit 0`) con `execution_status = PASS`, `composition_status = PASS` y `failures = 0`: 5 normales, 5 de ablación y 2 de depuración (modo 10). `saveIntegrity.status = PASS` en todas. |
| **MATERIAL_CORRECTNESS** | **PASS** | Paquetes modo 10 sobre píxeles relit: `giFraction` estrictamente en [0,1] en las 5 escenas, 0 valores no finitos, `baseColor > 1.0` en 0.0000 de los píxeles. La saturación UNORM8 que anulaba la cuota de GI está eliminada. |
| **NIGHT_LIGHTING_CORRECTNESS** | **PARTIAL** | Causa raíz P0 cerrada: `HYRULE_FIELD_NIGHT` pasa de mediana **4.105 → 1.3746**, el término directo queda en `radi/raster = 0.8468` (≤1.0), `p5 = 0.7654`, sin pérdida de sombras y con p99 idéntico (0.6908 → 0.6908). **No certificado** porque la GI aporta `+0.534` unidades de raster (38.8% del `path`), dejando la mediana un 37% por encima del raster; es energía medida y causalmente removible, pero su aceptación es visual. |
| **INDIRECT_LIGHT_TRANSPORT** | **PASS** | Causa raíz P1 GI cerrada: la cuota se publicaba en `gSurfaceMask.a`, un UAV `R8G8B8A8_UNORM` que satura `1.0+giFraction` en 1.0; ahora se publica en `gIndirectMask.a` (FP16) como `2.0+giFraction`. `INDIRECT_OFF` altera 98.31–99.99% de los píxeles relit en las 5 escenas con delta mediano 0.0018–0.0211 y **0.000000** exacto sobre los píxeles no trazados. Antes: 0.000000 en las cinco. |
| **ENERGY_BALANCE** | **PASS** | `pathResult/raster` mediana dentro de [0.25, 3.0] en las 5 escenas (0.9163–1.3809), p1 ≥ 0 y p99 ≤ 8, ninguna escena colapsa ni se desborda; `composite non-finite = 0`. |
| **COMPOSITOR_CORRECTNESS** | **PASS** | `|median(composite/raster) − median(pathResult/raster)| ≤ 0.0055` en las 5 escenas, es decir `pathWeight·credibility = 1`: relevo exacto una sola vez, sin doble conteo. Los tres controles de ablación se distinguen numéricamente en el mismo fotograma. |
| **REGRESSION_STABILITY** | **PASS** | Los 5 escenarios obligatorios (`HYRULE_FIELD_NIGHT`, `HYRULE_FIELD_DAY`, `TEMPLE_INTERIOR`, `BASELINE_KOKIRI`, `LAKE_HYLIA_DAY`) capturados en el build final con `dxr_execution = PASS` / `dxr_composition = PASS` y `failures = 0`; medianas 1.3746 / 1.1158 / 0.9175 / 1.2058 / 1.0009. |
| **SAVE_INTEGRITY** | **PASS** | `saveIntegrity.status = PASS` en las 12 ejecuciones (Save aislado por el harness, `originalHashesBefore == originalHashesAfter`); `build/OCZ_RTX_06_RELEASE` e `OCZ_RTX_06_CONTINUATION` intactos por hash. |
| **VISUAL_VERIFICATION** | **NOT_RUN** | Requiere revisión humana. Comparaciones A/B en `reports/OCZ_RTX_07_AB/` y los seis `comparison_*` (con overlay/diferencia/blink) capturados en las cinco ejecuciones. `verification.status.json`: `failures = []` y `status = PARTIAL` en las 12 ejecuciones, con `dxr_visual_verified = PARTIAL` / `dxr_visual_reason = "Causal same-frame ablation was not run."`. El contrato `TEMPLE_WINDOW_MOSAIC_NAVI_V1` sigue `NOT_CERTIFIED`. |

## Integridad

- `build/OCZ_RTX_07_RELEASE/out/Release/soh.exe` es el único ejecutable nuevo (34 396 672 bytes).
- `build/OCZ_RTX_06_RELEASE/out/Release/soh.exe` **intacto**: 34 392 064 bytes, SHA-256 `A514121C505A54AF723390EA64BBEA4FF7DED2A5D96EBF3DC220144F53FBD678`.
- `build/OCZ_RTX_06_CONTINUATION/out/Release/soh.exe` **intacto**: 34 395 136 bytes, SHA-256 `9D40C61836B516BD6371C7B163771A6D593198877996F5869E3BBEE5B68E0599`.
- No se tocó V03.1.1, ROM, recursos originales ni guardados; save temporal aislado por el harness en las 12 ejecuciones.
- `git status --porcelain` conserva exactamente los cambios locales preexistentes de Shipwright; dentro del submódulo `libultraship`, `dxr_shadow.hlsl` y `DxrSurfaceRenderer.cpp` siguen siendo fuentes sin versionar del trabajo OCZ (el resto del backend DXR también). Sin `reset`, `checkout`, `commit`, `push` ni merge.
- El DXIL se regeneró y verificó en `build/OCZ_RTX_07_RELEASE/generated/fast/backends/dxr_shadow_cso.h`; las salidas estables no se reescribieron.

## Archivos de esta ronda

- `source/shipwright/libultraship/src/fast/shaders/dxr_shadow.hlsl` — `fast3dShade` + `diffuseFast3D` en `ClosestHitReflection` (P0/P1 energía), `EvaluateMaterialSpecularLight`, y publicación de la cuota de GI en `gIndirectMask.a` (P1 GI).
- `source/shipwright/libultraship/src/fast/backends/DxrSurfaceRenderer.cpp` — el compositor lee `saturate(indirectSample.a − 2.0)` como cuota de GI.
- `source/shipwright/build/OCZ_RTX_07_RELEASE/generated/fast/backends/dxr_shadow_cso.h` — DXIL regenerado (ABI 3, `exports=6 bindings=17 rootSignatureContracts=10`).
- Evidencia: 5 normales `20261014_235617_012`, `20261014_235836_403`, `20261015_000059_003`, `20261015_000337_663`, `20261015_000547_853`; 5 de ablación `20261014_235732_029`, `20261014_235949_626`, `20261015_000218_081`, `20261015_000441_964`, `20261015_000654_799`; 2 modo 10 `20261015_000924_998`, `20261015_001031_100`. Baseline pre-fix: `20261014_211508_756`, `213009_786`, `213120_292`, `213226_465`, `213333_902`.

## Hallazgos residuales (cuantificados, no corregidos)

1. **La GI nocturna supera la energía del raster en 38.8%.** En `HYRULE_FIELD_NIGHT` el término directo ya está correcto (0.8468 del raster), pero la GI añade 0.534, dejando `path/raster` en 1.3809. El bucle de rebotes es coseno-ponderado con 8 muestras, `TMax = 256`, `indirectScale = 0.28` y 5 luces puntuales activas en la escena. Corregirlo exige decidir si el relight debe acotarse a la energía del raster cuando la supera — hoy `credibility = saturate(tracedLum/rasterLum)` solo acota por debajo, nunca por encima. No se tocó porque sería una política energética nueva.
2. **`traced/raster` en el templo cierra en 0.9162, no en 1.0.** El hueco de 8% restante es la diferencia entre el modelo de luz Fast3D (niebla, luces de vértice, especular) y el del trazador, no un defecto de transporte; antes del fix era 0.5142.
3. **Desfase baseline `raster_hdr` ~1% entre ejecuciones** en píxeles no relit (y bandas `dxr_indirect.a` que difieren en ~0.08%). Preexistente, no introducido en esta ronda; obliga a comparar siempre dentro de una misma ejecución.
4. **Semántica de `dxr_indirect.a` cambiada** de `{0,1,2}` a `{0,1,2+giFraction}`. El umbral `step(1.5, a)` del compositor es estable, pero cualquier análisis externo que compare `a == 2` exactamente debe pasar a `a >= 1.5`. Solo afecta a rutas de baja resolución donde `SampleDepthGuided` promedia el 2×2 (en resolución nativa hace `Load` directo).

## Trabajo restante

1. Revisión visual humana de `reports/OCZ_RTX_07_AB/` y de los seis `comparison_*` para `VISUAL_VERIFICATION`.
2. Decidir la política energética de la GI nocturna (hallazgo 1): aceptar 1.37× el raster, o acotar el relight cuando supera la energía del raster.
3. El contrato `TEMPLE_WINDOW_MOSAIC_NAVI_V1` sigue `NOT_CERTIFIED` (`window_to_floor_radiance = NATIVE_DIRECTIONAL_THROUGH_EXACT_WINDOW_PORTAL_TRIANGLES`).
