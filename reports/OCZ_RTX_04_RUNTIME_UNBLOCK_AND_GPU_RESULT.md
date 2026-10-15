# OCZ-RTX-04 — Desbloqueo del runtime y resultado GPU

**Fecha:** 2026-10-13  
**Estado final:** `DXR_VISUAL_VERIFIED=PARTIAL`

## Resultado ejecutivo

Se localizó un `oot.o2r` local compatible y se copió al runtime aislado. El ejecutable Release con el hash exigido arrancó en D3D12 sobre la RTX 3070 Ti Laptop, perfil `HYBRID_RT (2)`. El harness acreditó capacidad DXR Tier 1.2, BLAS/TLAS válidas, dos dispatches, dos señales y dos composiciones en el mismo frame. Se generaron los ocho checkpoints del contrato y DXGI aceptó el frame de captura con `HRESULT 0x0`.

La contribución se ve en el control del mismo frame: `RASTER_HDR` se captura antes de ejecutar los efectos DXR y `COMPOSITE_PRE_TONEMAP` después. Ambas capturas comparten escena, vista, cámara, resolución y frame; el log del backend registra los dispatches, las señales y la composición entre esos puntos. El cambio no se atribuye únicamente a que dos archivos sean distintos.

La certificación completa permanece `PARTIAL` porque el verificador conserva `causal_same_frame_ablation_not_run`, y no se obtuvo una segunda salida SDR raster-only válida. La ejecución de control sin dispatch DXR alcanzó la escena, pero el harness no solicitó captura: `BackendDxrReady()` exige estructuras DXR habilitadas, un primer TLAS y un primer `DispatchRays` antes de permitirla. La imagen Hybrid RT final muestra la escena y el HUD, aunque presenta una apariencia muy lavada/iluminada; no se certifican calidad visual humana, controles ni cambio de perfil.

## 1. Recurso encontrado

- **Origen:** `C:\Users\danie\Desktop\Voyager 2027\Entretenimiento\gamesModificados\Zeldaroom\Ocarina of Time PC\oot.o2r`.
- **Instalación de origen:** Ship of Harkinian, versión de producto `9.1.1`.
- **Tamaño:** 33,482,668 bytes.
- **SHA-256:** `6FC0211FD1EA450FC41AA0991C0ABFD73FF61B81DB39039033EDFDB2653D9E4B`.
- **Validación:** archivo legible, O2R/ZIP válido, 39,032 entradas y `portVersion` que declara `9.1.1`; coincide con la versión de la instalación y del ejecutable de prueba.
- **Resultado de copia:** copiado, no movido, a `runtime/build-test/oot.o2r`; tamaño y SHA-256 coinciden. El archivo de origen conservó el mismo hash tras las ejecuciones.
- No se extrajo ni descargó ninguna ROM y no se renombró un `.otr`.

La búsqueda cubrió el proyecto, Escritorio y subcarpetas, Descargas y Documentos. En Descargas y Documentos no hubo candidatos. Al encontrar una copia compatible en la instalación local de Ship of Harkinian, no fue necesario rastrear otras unidades.

## 2. Runtime y configuración

Los siete componentes pedidos estaban presentes en `runtime/build-test/` al finalizar:

| Componente | Bytes | SHA-256 | Estado |
|---|---:|---|---|
| `soh.exe` | 34,278,400 | `FEB608390D9EE1436389C0B3E2328D6F661A660251598CDF0231DCDBFFD5A72B` | PASS; coincide con el hash exigido |
| `soh.o2r` | 1,184,426 | `CBF3227F7C52A595C211B60D4937C6A0CE0DF691511FEC526E4AD509E9A5EB17` | PASS |
| `oot.o2r` | 33,482,668 | `6FC0211FD1EA450FC41AA0991C0ABFD73FF61B81DB39039033EDFDB2653D9E4B` | PASS; copia validada |
| `es.o2r` | 99,610 | `D9037A8A4086D0073F90CC297FCBFE558A78AC3758F27B11A250537883FD990D` | PASS |
| `mods/es.o2r` | 99,610 | `D9037A8A4086D0073F90CC297FCBFE558A78AC3758F27B11A250537883FD990D` | PASS |
| `gamecontrollerdb.txt` | 584,016 | `8055F1EA5F2EB367476A57FD153429D1EC3C0AA2E6C93E88A5EA85C922245F2D` | PASS |
| `shipofharkinian.json` | 16,078 | `F86C62BCAE9DC421986F1D0CA5E57EDFFBB4771DC3ECD70D2F944F2EA200DED5` | PASS en valores requeridos |

La configuración efectiva al terminar conserva `CVars.gLanguage=Spanish`, `CVars.gEnhancements.Autosave=0`, `CVars.gEnhancements.Graphics.RendererBackend=1` y `CVars.gEnhancements.Graphics.VisualProfile=2`. El launcher pasó `COUCH_D3D12=1` y `COUCH_VISUAL_PROFILE=2`; el manifiesto y `harness.log` confirman backend D3D12 y perfil `HYBRID_RT (2)` activos.

**Cambio observado en el archivo de configuración:** el informe OCZ-RTX-03 registró el archivo aislado antes de ejecutar con 12,084 bytes y SHA-256 `CFC067F5DB6423483C4EEA5835248327D0405741BE450A85A6C1CA65EA1E080B`; después de las ejecuciones, el archivo del runtime de prueba tiene 16,078 bytes y el hash de la tabla. Los cuatro valores requeridos permanecen iguales. No se reemplazó ni reconstruyó el resto del JSON al no existir una copia exacta de la versión previa en el workspace.

## 3. Preflight y partidas

- Runtime ejecutado desde `runtime/build-test`; el directorio base `runtime/build-test/Save` estaba vacío antes y después.
- `COUCH_DXR_SAVE_ROOT` resolvió a una carpeta nueva por ejecución bajo `reports/scene_harness/<runId>/sandbox/Save`; el harness confirmó `save_sandbox_used=true` y `temp_save=true`.
- La ejecución Hybrid RT creó `sandbox/Save/global.sav` (82 bytes) solo en su sandbox.
- `save-integrity.json`: `PASS`; hashes originales antes/después vacíos e idénticos. Los O2R del runtime no se generaron ni cambiaron.
- No se accedió a la carpeta `Save` de la instalación de origen. La versión estable V03.1.1 no se modificó.
- Las comprobaciones previas de rutas confirmaron que el sandbox no resolvía a una junction ni a un symlink de partidas.

## 4. Ejecución D3D12/DXR

**Ejecución principal:** `20261013_172207_328_BASELINE_KOKIRI_NORMAL`  
**Carpeta de evidencia:** `reports/scene_harness/20261013_172207_328_BASELINE_KOKIRI_NORMAL/`

- `soh.exe` abrió con ventana `Ship of Harkinian (Direct3D 12)`; el proceso terminó mediante cierre normal de la ventana.
- Adaptador: `NVIDIA GeForce RTX 3070 Ti Laptop GPU`.
- D3D12: dispositivo y swapchain creados; backend confirmado como D3D12.
- DXR: hardware Tier `TIER_1_2`; `ID3D12Device5` PASS.
- Perfil: `HYBRID_RT (2)` activo durante el juego y en el frame de captura.
- TLAS: `FIRST_TLAS = PASS` con 4 instancias; en el muestreo del frame 120, 212 instancias válidas; en frame 240, 212. Sin evicciones BLAS en los intervalos registrados.
- Ray dispatch: primera llamada DXR y primera reflexión reportadas como `SUCCESS`. En frame 285 el harness registra `dispatchCalls=2`, `signalOutputsWritten=2`, `compositeDraws=2`, `dispatchExecuted=1`, `signalsProduced=1` y `compositionExecuted=1`.
- Luz del juego: sincronización del frame y clasificación productiva `PASS`.
- Dibujos inválidos omitidos: 0. Los logs no contienen marcadores `DEVICE_REMOVED`, `DEVICE_HUNG` ni fallo DXGI. La capa Debug de D3D12 quedó apagada, por lo que no se declara validación con Debug Layer.

## 5. Captura y comparación A/B

Solicitud/frame coherentes: escenario `BASELINE_KOKIRI`, escena `0x55`, sala 0, entrada `0xEE`, vista `VIEW_00_DEFAULT_SPAWN_CAMERA`, frame de captura 285 y 30 frames estables. Los metadatos de las ocho capturas comparten `view_hash=4105440160196097632`, `projection_hash=4160948567508126386` y `player_state_hash=4634451203960158127`.

| Checkpoint | Captura | Formato | Estado |
|---|---|---|---|
| `RASTER_HDR` | `raster_hdr_frame000285.rgba16f` | 3440×1440, `R16G16B16A16_FLOAT` | PASS; imagen raster previa a los efectos DXR |
| `COMPOSITE_PRE_TONEMAP` | `composite_pre_tonemap_frame000285.rgba16f` | 3440×1440, `R16G16B16A16_FLOAT` | PASS; después de los compositores DXR |
| `RASTER_SCENE_COLOR_COPY` | `raster_scene_color_copy_frame000285.rgba16f` | 3440×1440, `R16G16B16A16_FLOAT` | PASS |
| `POST_ACES_PRE_PRESENT` | `post_aces_pre_present_frame285_raw.png` | 3440×1440, `R8G8B8A8_UNORM` | PASS |
| `FINAL_SDR_PRE_PRESENT` | `final_sdr_pre_present_frame285_raw.png` | 3440×1440, `R8G8B8A8_UNORM` | PASS; escena y HUD visibles |
| `DXR_REFLECTION_RGBA16F` | `dxr_reflection_frame000285.rgba16f` | 3440×1440, `R16G16B16A16_FLOAT` | PASS; finita y no vacía |
| `DXR_INDIRECT_RGBA16F` | `dxr_indirect_frame000285.rgba16f` | 3440×1440, `R16G16B16A16_FLOAT` | PASS; finita y no vacía |
| `EMISSIVE_MASK` | `emissive_mask_frame000285.r16f` | 3440×1440, `R16_FLOAT` | PASS en captura; todos los valores son cero en esta escena |

### A — Raster sin contribución DXR / B — Hybrid RT

El control disponible es una comparación **in-frame**. A es `RASTER_HDR`, registrado antes de `DxrShadowRenderer::RenderShadows()` y `DxrSurfaceRenderer::RenderReflections()` en `gfx_direct3d12.cpp`; B es `COMPOSITE_PRE_TONEMAP`, registrado después de esas llamadas y de la composición, sobre el mismo color de escena. No hubo cambios de cámara, escena ni tiempo entre A y B.

- `RASTER_HDR` → `COMPOSITE_PRE_TONEMAP`: float MAE `0.0266404`, máximo `1.0`, `3,751,399 / 4,953,600` píxeles distintos (`75.730761%`); comparación sin valores no finitos.
- La secuencia de código coloca los dos dispatches, sus dos señales y las dos composiciones entre A y B en el mismo frame 285. Esa evidencia de ejecución, junto con el orden de las capturas, establece que el resultado HDR recibe trabajo DXR; la diferencia de píxeles por sí sola no se usa como prueba.
- La señal de reflexión tuvo media RGB `[0.167091, 0.177345, 0.198346]` y máximo `[0.634766, 0.672363, 0.769531]`; iluminación indirecta media `[0.0142889, 0.0135738, 0.00692417]` y máximo `[0.0714722, 0.0673828, 0.0481873]`. Ambas reportaron `finite_signals=true`, sin canales negativos, NaN ni Inf.
- La comparación `POST_ACES_PRE_PRESENT` → `FINAL_SDR_PRE_PRESENT` dio 0 píxeles distintos. Esto confirma consistencia entre esos dos checkpoints de la salida B; no sustituye una salida SDR raster-only comparable.
- El verificador dejó `causalAttribution=NOT_ESTABLISHED_BY_PIXEL_DIFFERENCE` y `dxrVisualVerified=PARTIAL`. No se cambió manualmente ese resultado.

Se intentó una salida raster-only independiente en el mismo D3D12/perfil 2 usando `COUCH_DXR_AS=0`, `COUCH_DXR_SHADOW=0` y `COUCH_DXR_SURFACES=0`, solo en el proceso de control `20261013_173004_380_BASELINE_KOKIRI_NORMAL`. El juego abrió y llegó a la escena, pero el harness no produjo `capture.request.json`: su gate `BackendDxrReady()` exige que el log de inicialización contenga DXR habilitado, primer TLAS y primer `DispatchRays`. El launcher terminó en `POLL_TIMEOUT_PROCESS_LEFT_OPEN`; el proceso se cerró normalmente y `ORIGINAL_SAVE_UNCHANGED=TRUE`. Esa ejecución no se contabiliza como captura A ni como PASS.

## 6. Presentación, imagen y rendimiento

- `presentation.evidence.jsonl`: frame forense 285 = frame presentado 285; `HRESULT=0x0`, `ACCEPTED_BY_DXGI`.
- La captura es previa al escaneo físico. El escaneo real del monitor no se puede verificar con estos datos.
- La captura SDR muestra Kokiri y el HUD. Se aprecia una imagen muy iluminada/lavada; por eso la aceptación visual humana de calidad permanece pendiente.
- HUD visible: PASS como presencia en captura. Menús, controles manuales, español visible en interfaz y cambio Classic/Hybrid RT: no verificados. El harness mantuvo el bloqueo de input para proteger la escena.
- El contador llegó a 1,158 frames presentados antes del cierre, pero no se guardó una duración de render medida con precisión; `FPS/frametime=NOT_MEASURED`.
- VRAM NVIDIA (8,192 MiB total): 1,341 MiB y 0% al lanzamiento; muestra durante la ventana activa de aproximadamente 2,653 MiB y 100% de utilización; después del cierre, 1,376 MiB y 20%. Son muestras puntuales, no promedio ni pico certificado.

## 7. Correcciones y pruebas

- No se encontró un fallo de inicialización o ejecución DXR que justificara cambiar el renderizador; no se modificó código fuente ni el binario Release certificado.
- Correcciones de código: `NOT_RUN` (sin fallo DXR demostrado).
- Regresiones después de una corrección: `NOT_RUN` (no hubo cambios de código).
- Scene harness principal: ocho capturas completas; ejecución DXR y composición `PASS`; verificación visual global `PARTIAL`.
- Control raster-only independiente: `PARTIAL/BLOCKED_BY_HARNESS_DXR_READY_GATE`, sin captura aceptada.
- Partidas de la ejecución principal: integridad `PASS`; solo se creó el guardado temporal dentro del sandbox.

## Estado final

`DXR_VISUAL_VERIFIED=PARTIAL`

La ejecución GPU real y la contribución DXR al checkpoint HDR están demostradas. La certificación visual final sigue parcial por la falta de una salida A raster-only aceptada por el harness para comparar después de ACES/SDR, y por la apariencia lavada de la captura B. DXGI sí aceptó la presentación; no se afirma escaneo físico ni aceptación humana.
