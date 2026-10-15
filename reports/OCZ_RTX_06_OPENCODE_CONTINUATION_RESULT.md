# OCZ-RTX-06 — resultado de continuación (OpenCode)

Fecha: 2026-10-14
Estado general: **PARTIAL** — los seis gates separados se evalúan a continuación; la corrección de la causa raíz está aplicada, compilada y verificada empíricamente en GPU, y las cinco ejecuciones del harness terminaron con `failures=0`. Quedan tres defectos de transporte de luz cuantificados y la verificación visual humana.

## Objetivo

Cerrar los defectos restantes de **OCZ-RTX-06** en *Ocarina Couch Edition RTX*: eliminar las superficies artificialmente negras causadas por la sustitución destructiva del raster por una radiancia DXR insuficiente, corregir las ablaciones, regenerar DXIL y recompilar en directorio aislado, repetir el ensayo `TEMPLE_INTERIOR`, ejecutar las regresiones y entregar este recibo con los seis gates separados.

## Causa raíz encontrada y corregida

La radiancia DXR del suelo del templo era **~100× demasiado oscura**. El diagnóstico con `COUCH_DXR_MATERIAL_DEBUG=2` (que escribe `baseColor` en el buffer de reflexión) demostró que el albedo DXR del suelo era exactamente el **color de niebla**:

| | `baseColor` suelo (filas 250-435) | `pathResult` relit | `raster` |
|---|---|---|---|
| Antes (`20261014_210309_694`) | media **0.00317**, mediana **0.00303** | 0.00204 | 0.3274 |
| Después (`20261014_212614_855`) | media **0.34961**, mediana **0.40845** | — | 0.3274 |

`0.00303 = SrgbToLinear(10/255)`, y `10/255` es el color de niebla del suelo (`fogRGBA=10,10,10,0`).

La dirección de la niebla estaba **invertida** entre raster y DXR:

- Raster, `gfx_direct3d12.cpp:4080-4082`: `texel = lerp(texel.rgb, input.fog.rgb, input.fog.a)` → el factor es la **cantidad de niebla** (0 = sin niebla).
- DXR, `dxr_shadow.hlsl:767` (antes): `combined = lerp(fogColor, combined, fogFactor)` → con `fogFactor == 0` el shader **horneaba el color de niebla como albedo**.

El templo no fija niebla (`fog_mul = fog_offset = 0`, `interpreter.cpp:3278`), así que `fogFactor` era 0 en todo el suelo y `DecodeMaterialHit` devolvía el color de niebla en lugar del texel.

**Corrección aplicada** en `libultraship/src/fast/shaders/dxr_shadow.hlsl` (~L764-771): se invirtió la dirección del lerp para que coincida con el raster, `lerp(combinedEncoded, fogColor, fogFactor)`. No se movió la niebla a post-lighting (riesgo de regresión en escenas con niebla) y no se añadieron comentarios.

## Evidencia en GPU (cinco ejecuciones, todas `exit 0`, `failures=0`)

Relación `composite / raster` sobre píxeles relit (`indirect.a == 2`), frame 499, sobre píxeles con `raster > 1e-4`:

| Ejecución | p0.1 | p1 | p5 | mediana | %<0.50 | %<0.60 | %<0.75 | %>1.0 |
|---|---|---|---|---|---|---|---|---|
| `20261014_211508_756_TEMPLE_INTERIOR` | 0.638 | 0.735 | 0.741 | 0.748 | 0.02% | 0.06% | 66.91% | 3.03% |
| `20261014_213009_786_BASELINE_KOKIRI` | 0.512 | 0.570 | 0.742 | 0.915 | 0.06% | 1.70% | 6.13% | 37.19% |
| `20261014_213120_292_LAKE_HYLIA_DAY` | 0.499 | 0.571 | 0.752 | 0.781 | 0.12% | 1.11% | 4.04% | 8.40% |
| `20261014_213226_465_HYRULE_FIELD_DAY` | 0.516 | 0.729 | 0.738 | 0.773 | 0.02% | 0.33% | 19.35% | 1.34% |
| `20261014_213333_902_HYRULE_FIELD_NIGHT` | 0.625 | 0.750 | 0.983 | 4.105 | 0.01% | 0.07% | 0.88% | 94.76% |

Ninguna escena deja más del **0.12%** de los píxeles relit por debajo de 0.50× el raster; el estado previo documentado en `OCZ_RTX_LIGHT_ORIGIN_AUDIT.md` era **99.8% por debajo de 60%** y ~270× más oscuro.

Energía trazada en el templo (antes → después):

| Señal | Antes (`20261014_204659_904`) | Después (`20261014_211508_756`) | Factor |
|---|---|---|---|
| `pathResult` relit, media | 0.00204 | **0.20007** | 98× |
| `pathResult` relit, mediana | 0.00154 | 0.20679 | 134× |
| Ablación `relight_off` vs composite, relit | 0.001801 | **0.096069** | 53× |
| `baseColor` suelo, mediana | 0.00303 | **0.40845** | 135× |

`raster` y `composite` en el templo: 0.2947 y 0.3086 de media; `composite non-finite = 0`, máximo 2.6465.

### Ablaciones (misma escena, mismo fotograma, misma lista de comandos)

Registro del harness: `[COUCH-DXR-SURFACE][ABLATION] frame=… sameScene=1 sameSignals=1 sameCommandList=1 controls=SURFACE_OPTICS_OFF,INDIRECT_OFF,RELIGHT_OFF`.

| Control | TEMPLE | KOKIRI | LAKE | FIELD_DAY | FIELD_NIGHT |
|---|---|---|---|---|---|
| `relight_off` vs composite (relit) | 0.096069 | 0.014023 | 0.077515 | 0.080383 | 0.077332 |
| `surface_optics_off` vs composite (relit) | 0.001888 | 0.000643 | 0.001713 | 0.002144 | 0.000630 |
| `indirect_off` vs composite (relit) | 0.000000 | 0.000000 | 0.000000 | 0.000000 | 0.000000 |
| `indirect_off` vs composite (global) | 0.00467 | 0.00027 | 0.00110 | 0.00005 | 0.00002 |

Los tres controles viajan por el mismo root signature (`compositeParams.ablationMode = i + 1` para `i ∈ {0,1,2}` → `gAblation = 1,2,3`). `indirect_off` sí actúa: en los píxeles **no relit** del templo la media absoluta es 0.005585 (649 006 componentes distintas de cero), que coincide con `0.45 × indirectContribution` del término aditivo del compositor. Sobre los píxeles **relit** la diferencia es exactamente 0 — ver «Hallazgos residuales».

### Constantes de luz (idénticas en frames repetidos)

| Escena | `sceneLight` | `ambient` | `pointCount` | `pathTracedProfile` |
|---|---|---|---|---|
| TEMPLE_INTERIOR | (0.784314,…) | (0.196078,…) | 3 | 0 |
| BASELINE_KOKIRI | (1,1,1) | (0.313726×3) | 2 | 0 |
| LAKE_HYLIA_DAY | (1,1,0.941176) | (0.411765,0.352941,0.352941) | 2 | 0 |
| HYRULE_FIELD_DAY | (1,1,1) | (0.313726×3) | 2 | 0 |
| HYRULE_FIELD_NIGHT | (0.415686,0.247059,0.156863) | (0.274510,0.227451,0.168627) | 5 | 0 |

Las constantes CPU siguen siendo correctas y variables por escena; la hipótesis «constantes a cero» sigue descartada.

## Hallazgos residuales (cuantificados, no corregidos)

1. **Cuota de GI en píxeles relit = 0.** `giFraction` (publicado como `gSurfaceMask.a = 1 + giFraction`) queda por debajo del umbral de representación FP16 en todos los píxeles relit de las cinco escenas: `|composite − indirect_off| == 0` con `n = 34536` en el templo. El control sí funciona (ver arriba), y en los píxeles no relit la contribución indirecta media es 0.01481 en el templo, así que el bucle de GI no está roto en general. La contribución indirecta del suelo relit es `< 2e-4` de la energía del camino, es decir, la GI no aporta nada visible al relight. No se tocó porque el origen exacto no está aislado y corregirlo requiere otro ciclo de build/ensayo.
2. **La radiancia trazada sigue portando ~51% de la del raster.** En el templo `traced/raster` tiene mediana 0.5142 y media 0.6069, y el blend implícito del compositor (`credibility = traced/raster`) reproduce la mediana medida de 0.7510 frente a 0.7476 observada. El límite no destructivo se cumple exactamente, pero el relight DXR aún no iguala la energía del juego.
3. **`HYRULE_FIELD_NIGHT` se sobreilumina: mediana 4.105× el raster, 94.76% de los píxeles relit por encima de 1.0.** `rasterL` media 0.0296 frente a `pathL` media 0.10498. El relight usa `gAmbientLightColor = (0.2745, 0.2275, 0.1686)` a tamaño completo y neutraliza el `shadeColor`/luz de vértice con la que el juego oscurece el campo de noche. Es el defecto especular del apagado anterior: el relight descarta la oscuridad del juego en lugar de descartar su luz.
4. **Desfase baseline `raster_hdr` vs `composite` ~1%** en píxeles no relit (mediana 1.0176 en el templo, 28.8% idénticos al 0.01). Preexistente, no introducido en esta ronda.

## Gates

| Gate | Estado | Evidencia y límite |
|---|---|---|
| **BUILD_PASS** | **PASS** | `Generate-DxrShadowBytecode.ps1` con DXC `-T lib_6_5 -HV 2021`: `sourceSha256=ddac32fc6fb184afb9f435da908e8135b0199877f8ee9807bf42b25166d4d83f`, `dxilSha256=e7a550589160f7d3c2cc7abe229589a588b81dff8e9e2cea8e07d9227e4c3520`, `buildId=acd1cb46b42149f98e49d8b81f097a15ecef699e6666f6f6b2292d67c0185bb7`, ABI 3, `exports=6 bindings=17 rootSignatureContracts=10`, 121 988 bytes, DXC 1.9.2602.17. Build `OCZ_RTX_06_CONTINUATION` Release: 0 errores. `soh.exe` = 34 395 136 bytes, SHA-256 `9D40C61836B516BD6371C7B163771A6D593198877996F5869E3BBEE5B68E0599`. |
| **DXR_EXECUTION_PASS** | **PASS** | Cinco ejecuciones `exit 0`, `captureStatus=CAPTURE_COMPLETE`, frame 499, `dxrExecutionStatus=PASS`, `dxrCompositionStatus=PASS`, `gameLightSyncClassification=PASS`, `sameFrame=true`, `verifiedCaptureStages=8`, `verification.failures = 0` en las cinco. TLAS, DispatchRays y composite confirmados por el marcador `COMPOSITE_EXECUTED`. |
| **MATERIAL_CORRECTNESS** | **PASS** | `baseColor` del suelo corregido de 0.00303 (= color de niebla) a 0.40845, coherente con el raster 0.3274. Sustitución destructiva eliminada: el mínimo de `composite/raster` sobre relit es 0.735 (templo), 0.499/0.512/0.516/0.625 en p0.1 del resto; ≤0.12% de píxeles relit bajo 0.50× en cualquier escena; `composite non-finite = 0` en las cinco. |
| **LIGHT_TRANSPORT_CORRECTNESS** | **PARTIAL** | Ablaciones causalmente distintas en las cinco escenas (`relight_off` 0.014–0.096 en relit, `surface_optics_off` 0.0006–0.0021, `indirect_off` 0.0056 sobre no relit). Constantes de luz correctas y variables por escena. **No certificado** por los hallazgos 1 y 3: la GI no aporta a los píxeles relit y `HYRULE_FIELD_NIGHT` se sobreilumina 4.1×. |
| **COMPOSITOR_CORRECTNESS** | **PASS** | Los tres controles se graban en el mismo fotograma con la misma lista de comandos y se distinguen numéricamente. `pathWeight = pathSignal × clippedCoverage × surfaceValid` reproduce exactamente la relación medida `1 − r(1 − r)`. Cero píxeles no finitos; el espectro se mantiene en FP16 hasta el hombro SDR. |
| **VISUAL_VERIFICATION** | **NOT_RUN** | Requiere revisión humana: `comparison_dxr_off`, `comparison_dxr_on`, `comparison_relight_off`, `comparison_optics_off`, `comparison_indirect_off`, `comparison_shadows_only` con overlay/diferencia/blink están capturados en las cinco ejecuciones, y `dxrVisualVerified = PARTIAL`. El contrato `TEMPLE_WINDOW_MOSAIC_NAVI_V1` sigue `NOT_CERTIFIED` (`window_to_floor_radiance` = `NATIVE_DIRECTIONAL_THROUGH_EXACT_WINDOW_PORTAL_TRIANGLES`). |

## Integridad

- `build/OCZ_RTX_06_RELEASE/out/Release/soh.exe` **intacto**: 34 392 064 bytes, SHA-256 `A514121C505A54AF723390EA64BBEA4FF7DED2A5D96EBF3DC220144F53FBD678` (verificado antes y después).
- No se tocó V03.1.1, ROM, recursos originales ni guardados: `ORIGINAL_SAVE_UNCHANGED=TRUE` en las cinco ejecuciones, save temporal aislado por el harness.
- `git status --porcelain` conserva exactamente los cambios locales preexistentes de Shipwright y LibUltraShip; el submódulo `libultraship` aparece como `dirty` por los fuentes nuevos ya existentes más la edición de `dxr_shadow.hlsl`. Sin `reset`, `checkout`, `commit`, `push` ni merge.
- El DXIL se regeneró en el directorio aislado `build/OCZ_RTX_06_CONTINUATION`; la salida estable y su build no se reescribieron.

## Archivos de esta ronda

- `source/shipwright/libultraship/src/fast/shaders/dxr_shadow.hlsl` — inversión del lerp de niebla en `DecodeMaterialHit` (~L764-771); DXIL regenerado en `include/fast/backends/dxr_shadow_cso.h`.
- `source/shipwright/libultraship/src/fast/backends/DxrSurfaceRenderer.cpp` — compositor no destructivo, `giFraction`, ablaciones (`relightEnabled` / `indirectEnabled` / `opticsEnabled`), `[ABLATION]` y `[COUCH-DXR-LIGHT-CONSTANTS]`.
- `source/shipwright/libultraship/src/fast/backends/gfx_direct3d12.cpp` — `kInFrameStages = 17`, nombres de etapa, `SceneHarnessAblationSlot`, capturas de ablación.
- `source/shipwright/scripts/scene-harness/scenarios/scenarios.v1.json`, `run-scene.ps1` — comparaciones y controles.
- Evidencia: `reports/scene_harness/20261014_211508_756_TEMPLE_INTERIOR_NORMAL` (referencia), `20261014_212614_855_TEMPLE_INTERIOR_NORMAL` (baseColor post-fix), `20261014_210309_694_TEMPLE_INTERIOR_NORMAL` (baseColor pre-fix), `20261014_204659_904_TEMPLE_INTERIOR_NORMAL` (energía pre-fix), y las cuatro regresiones `213009_786`, `213120_292`, `213226_465`, `213333_902`.

## Trabajo restante

1. Aislar por qué la cuota de GI es 0 en los píxeles relit y restaurarla (hallazgo 1).
2. Cerrar el 2× de energía que falta entre radiancia trazada y raster sin aumentar brillo, exposición ni luz ambiente de forma artificial (hallazgo 2).
3. Hacer que el relight respete la oscuridad del juego en `HYRULE_FIELD_NIGHT` (hallazgo 3).
4. Revisión visual humana de las seis comparaciones capturadas para `VISUAL_VERIFICATION`.

## Navi: separación de luz puntual en Kokiri (15 oct 2026)

### Correcciones incorporadas

- Los dibujos estáticos de Kokiri usan `G_LIGHTING`; la pasada nativa del mundo no enlaza luces puntuales a `Lights_BindAll(..., NULL)`. El filtro previo excluía esos dibujos y dejaba el búfer puntual de Navi completamente negro.
- La geometría conserva ahora el tinte fuente de Fast3D separado del color de vértice ya iluminado. DXR reconstruye `naviPointAlbedo` desde el combiner y ese tinte, sin volver a aplicar el Shade precalculado ni el blend de niebla. El payload HLSL/C++ se sincronizó a 136 bytes.
- Se separó la luz puntual nativa de Navi de la señal agregada de relight para receptores estáticos opacos/cutout. Se excluye el punto identificado `owner=1, role=1` del término primario de punto/backscatter y se envía al canal HDR dedicado; eso evita doble conteo y conserva las sombras, el relight y las demás luces.
- La ablación `gAblation=4` ya apaga solo ese canal de Navi; `gAblation=3` sigue siendo el control de relight apagado. La pareja OFF/ON usa el mismo frame y el mismo compositor de producción.
- Se añadió una escena harness `NAVI_KOKIRI_DIRECT_PRODUCTION_NATURAL_ACTIVE` que conserva las coordenadas naturales y fuerza estado luminoso únicamente dentro del proceso aislado; `navi.pose.json` lo etiqueta `NATURAL_POSITION_ACTIVE_LIGHT_TEST`.

### Evidencia del build y GPU

Build aislado `build/OCZ_NAVI_09_RELEASE`, `Release`, `soh`: PASS. EXE SHA-256 `9B0B39CBC7AB55D13375D341B9010DF7A920E15B06464FADA305FF16B9BE83B9`; DXR HLSL SHA-256 `A4CB5360E35E86CA54F9799BFB4AD3683FEAF43553C93390A74FE191CDA133AA`; shader ABI 4. Las capturas se ejecutaron con D3D12, perfil `HYBRID_RT`, calidad RTX 3, background, sin `Present`; `originalSaveUnchanged=true` y `backendDeviceRemoved=false`.

| Variante | Estado/posición de Navi | Radiancia HDR directa no nula | Diferencia final SDR |
|---|---|---:|---:|
| Producción, pose de prueba controlada (`20261015_104854_904`) | estado 0; radio 100; posición de prueba `[-33,-55,971]` | 58 544 píxeles; luminancia máxima 0.08603 | 45 782 / 307 200 píxeles; media 0.1722 códigos RGB, máximo 26 |
| Posición natural y estado luminoso solo de prueba (`20261015_105709_680`) | estado 0; radio 100; posición natural `[-71.29,-42.34,944.57]` | 11 930 píxeles; luminancia máxima 0.01371 | 10 062 / 307 200 píxeles; media 0.04090 códigos RGB, máximo 6 |
| Posición/estado natural congelado (`20261015_105304_812`) | estado 8; radio 0; auxiliar −1 | 0, esperado en estado inactivo | 0; esperado en estado inactivo |

La prueba causal A/B/C/D se repitió después de estos cambios en `20261015_105908_845_NAVI_KOKIRI_POINT_NORMAL`: identidad/activación, contrato de albedo/normal, BRDF de color/distancia, luz puntual sobre suelo real, malla emisiva, aditividad lineal y oclusión visible/bloqueada pasan dentro del alcance del piloto (722 receptores de suelo, 251 con señal puntual). Esto no demuestra por sí solo que el arte final del juego esté visualmente aprobado.

La captura natural congelada documenta un estado real inactivo, no un defecto del trazador: `EnElf_UpdateLights` fija el radio en 0 cuando `unk_2A8 == 8`. No se cambió ese comportamiento de juego. La variante con estado activo prueba la posición natural sin reubicar a Navi.

### Estado de aceptación

- **Identidad y transporte de luz puntual al receptor en la pose activa probada:** `PASS_SCOPED`.
- **Separación A/B en el compositor normal:** `PASS_SCOPED`; la diferencia SDR medida es pequeña, en especial en la posición natural.
- **Sombras oclusivas del punto:** medidas como visible/bloqueada en el piloto aislado.
- **Aceptación visual humana, resultado en monitor y calidad de remaster:** `PENDING` / `NOT_CERTIFIED`. El harness no presentó estos frames en la ventana visible; las imágenes SDR normales muestran el cambio sutil y el visor ofrece overlay, parpadeo y diferencia ampliada para inspección.

El visor local `http://127.0.0.1:63558/` conserva el piloto causal, las capturas controladas y las dos variantes de posición natural. Para revisar la escena natural con la luz activa, seleccionar `NAVI_KOKIRI_DIRECT_PRODUCTION_NATURAL_ACTIVE`; para confirmar el estado nativo apagado, seleccionar `NAVI_KOKIRI_DIRECT_PRODUCTION_NATURAL`.

## Navi en la plataforma de la Trifuerza (15 oct 2026)

### Pose de prueba anclada a geometría real

Se añadió al scene harness una posición opcional `naviPilot.linkPosition`. Cuando está declarada, fija Link y su `floorHeight` en esa coordenada, y el encuadre `NAVI_RECEIVER_CLOSEUP` sigue la pose. El cambio solo se activa para el proceso del harness; las coordenadas nativas del juego y la pose natural de Link no cambian.

La primera ubicación dejó a Link sobre la alfombra frente al pedestal. Un barrido `GPU_FIRST_HIT` mostró que la tapa de la plataforma se renderiza como `objects/object_mjin/gWarpPadTopTex`, con normal `[0,1,0]` en `y=-20`. Se usó como ancla el impacto medido `[31.68,-20,2086.86]` (`pixel [500,133]` de aquella vista). La captura nueva confirma que Link queda físicamente sobre la superficie central con el símbolo de la Trifuerza en el encuadre.

Escenarios añadidos: `NAVI_TRIFORCE_LINK_32X` (fuente aislada y cuatro variantes A/B/C/D) y `NAVI_TRIFORCE_LINK_32X_PRODUCTION` (compositor de producción y pareja puntual Navi OFF/ON). Navi mantiene su radio nativo 100 y geometría visible; la ganancia de luz puntual 32× es `AUTHORED_HARNESS`, únicamente diagnóstica.

### Verificación de la superficie y del compositor

| Evidencia | Medición |
|---|---|
| Receptor | 73 impactos GPU en `objects/object_mjin/gWarpPadTopTex`; todas las normales muestreadas apuntan hacia arriba |
| Respuesta puntual | 36 muestras dentro del radio, visibles y con radiancia directa no nula; la puerta CPU de color, distancia y BRDF pasa tras corregir su modelo de verificación |
| Radiancia en tapa visible | Media RGB lineal `[0.601456,0.601456,1.017673]`; máximo `[1.45618,1.45618,2.46489]` entre las muestras visibles del material |
| Compositor de producción | Frame 425, 44 743 píxeles con radiancia HDR directa finita; 44 636 píxeles SDR cambiaron entre puntual Navi OFF/ON (14.53% del frame) |
| Integridad del run | `CAPTURE_COMPLETE`; proceso oculto detenido; sin `Present`; `ORIGINAL_SAVE_UNCHANGED=TRUE` |

El primer análisis CPU reportó 309 fallos falsos porque comprobaba el color lineal de Navi antes de la ganancia efectiva (32×) y usaba `-positionTrace` como dirección visual, suponiendo la cámara en el origen. Se corrigió para usar `effectiveDxrColor` y el `camera_eye` de `CAMERA_POSE_FROZEN`; tras repetir el análisis, `native_distance_color_brdf=PASS` y `point_to_target_surface=PASS`. Esta corrección cambia el verificador, no el trazador.

### Imágenes A/B de producción

Ambas imágenes son del mismo frame/cámara/tonemapping. La salida OFF elimina solo la contribución puntual de Navi; el resto de las luces continúa en ambas.

| Navi puntual apagada | Navi puntual activa · ganancia de prueba 32× |
|---|---|
| ![Link sobre la plataforma, Navi apagada](scene_harness/20261015_120334_563_NAVI_TRIFORCE_LINK_32X_PRODUCTION_NORMAL/captures/navi_point_off_sdr_frame425_opaque_preview.png) | ![Link sobre la plataforma, Navi activa](scene_harness/20261015_120334_563_NAVI_TRIFORCE_LINK_32X_PRODUCTION_NORMAL/captures/navi_point_on_sdr_frame425_opaque_preview.png) |

Runs aislados:

- Producción: `20261015_120334_563_NAVI_TRIFORCE_LINK_32X_PRODUCTION_NORMAL`.
- Receptor y cuatro controles causales: `20261015_120931_067_NAVI_TRIFORCE_LINK_32X_NORMAL`.
- Los dos registros están publicados en `reports/dxr_comparison/suite.status.json` y disponibles en `http://127.0.0.1:63558/`.

### Estado de aceptación

- **Pose de Link sobre la superficie de la Trifuerza:** `PASS_SCOPED`; ubicación confirmada por ancla GPU y captura.
- **Fuente puntual de Navi → tapa/material → radiancia HDR:** `PASS_SCOPED_POINT_CONTRACT` con la ganancia 32× de prueba.
- **Cambio visible en la salida final del compositor:** `PASS_SCOPED`; la inspección de estas previsualizaciones muestra el cambio en Link, la tapa y el piso próximo.
- **Oclusión de la fuente por geometría opaca en esta pose:** `NOT_DEMONSTRATED`.
- **Energía nativa de Navi, calidad artística de remaster y salida física del monitor:** `PENDING` / `NOT_CERTIFIED`. El 32× no representa energía nativa, el run oculto captura antes de `Present` y aún no acredita una revisión visual humana en el panel.

Identidad de este build: ejecutable SHA-256 `6C00F7F31497780701D6EE8CAE8DE55BC109D4E76ED375105B4BB94C06D8F864`; shader HLSL SHA-256 `75c9632cfb2554989bb548e8f915021b33a560b18bc852b958f66119c4b8654c`; ABI 4.

## Corrección de la respuesta de Navi en la tapa de la Trifuerza (15 oct 2026)

### Causa y reparación

La respuesta anterior de la sonda aislada no bastaba para afirmar que Navi iluminaba visiblemente el juego. La sonda GPU encontraba `objects/object_mjin/gWarpPadTopTex` y calculaba radiancia puntual, pero el registro de ese material tenía `materialFlags=149`; no incluía `MATERIAL_FLAG_NAVI_POINT_WORLD_RECEIVER` (bit 10). La ruta de producción solo añade la luz puntual HDR a receptores con esa marca. Por eso los impactos de la prueba aislada no aparecían sobre la cara superior en el compositor normal. **Un impacto de rayo no certifica por sí mismo un cambio visual**.

`DxrAccelerationStructureManager.cpp` ahora da una excepción de receptor únicamente al recurso comprobado `objects/object_mjin/gWarpPadTopTex`. Se conservan las comprobaciones de propietario de mundo, material reconocido, albedo reconstruible, cobertura y tipo de material; no se marca el resto de la escena. El shader y su ABI no cambiaron.

El escenario de diagnóstico también se reforzó de forma explícita: Navi se coloca sobre el centro medido de la plataforma, del lado visible de Link (`positionOffset=[35,55,25]`), y la ganancia puntual se eleva a 128× solo en el harness. El radio nativo 100 se mantiene. La ganancia y el tinte azul son `AUTHORED_HARNESS`; **no son una calibración de la intensidad nativa ni quedan activos en el juego normal**.

### Evidencia A/B de producción

Build `OCZ_NAVI_09_RELEASE`, `Release`, `soh`: PASS; shader ABI 4, DXIL SHA-256 `4eb2b73c8dac4e049f4e9100199db5f60c68142dce8cb88e14d618aeecca6a10`; ejecutable SHA-256 `930FF34F771CF6BE1F08919DB54019ADE1F704757C853DD75F2954BCCB90864D`.

Run `20261015_131611_297_NAVI_TRIFORCE_LINK_BLUE_SPILL_PRODUCTION_NORMAL`: D3D12 `HYBRID_RT`, frame 425, mismo estado/cámara/compositor para Navi OFF y ON, ocho etapas de captura verificadas, buffer HDR finito y proceso de prueba cerrado. `ORIGINAL_SAVE_UNCHANGED=TRUE`; no se hizo `Present` ni captura del monitor. El lanzador informa `PARTIAL` porque el pase fue oculto y no puede probar presentación física ni aceptación visual humana; no registra fallos DXR.

| Receptor / métrica | Navi OFF → ON |
|---|---:|
| Tapa, píxel `[1684,710]` | radiancia directa Navi `[0,0,0]` antes → `[0.078,0.249,10.523]` HDR lineal después; delta SDR `[+4,+8,+37]` |
| Tapa, píxel `[1684,763]` | radiancia azul directa `12.883` HDR; delta SDR `[+3,+9,+13]` |
| Link, muestra de túnica `[1684,499]` | radiancia directa `[0.003,0.076,0.347]` HDR; delta SDR `[+4,+67,+172]` |
| Región central de tapa, rectángulo de revisión | 24 701 píxeles cambian más de 8 códigos RGB; media absoluta 11.10 |
| Fotograma 3440×1421 completo | 211 522 píxeles cambian entre la pareja puntual OFF/ON |

Estas mediciones confirman una respuesta del material superior dentro del compositor de producción, además de luz directa sobre la túnica de Link. El piso y el zócalo también reciben mucha luz en el perfil 128×; es una visualización diagnóstica exagerada, no un look artístico final.

![Navi OFF, frame de producción](scene_harness/20261015_131611_297_NAVI_TRIFORCE_LINK_BLUE_SPILL_PRODUCTION_NORMAL/captures/navi_point_off_sdr_frame425_opaque_preview.png)

![Navi ON, frame de producción](scene_harness/20261015_131611_297_NAVI_TRIFORCE_LINK_BLUE_SPILL_PRODUCTION_NORMAL/captures/navi_point_on_sdr_frame425_opaque_preview.png)

El resultado y el overlay A/B están publicados en `reports/dxr_comparison/suite.status.json`; el visor local `http://127.0.0.1:63558/` responde 200 y muestra esta ejecución como la primera variante.

### Estado de aceptación

- **Registro correcto del receptor superior en la ruta de producción:** `PASS_SCOPED`.
- **Radiancia puntual Navi sobre tapa y túnica de Link:** `PASS_SCOPED` en esta pose/ganancia de harness.
- **Diferencia causal OFF/ON en el compositor HDR/SDR:** `PASS_SCOPED`, frame común 425.
- **Sombras/oclusiones de esta fuente en todos los materiales de la escena:** `NOT_CERTIFIED`.
- **Energía nativa de Navi, balance artístico, calidad completa de remaster y salida física en monitor:** `PENDING`.
