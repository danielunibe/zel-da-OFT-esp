# OCZ-RTX-03 — Activación real, prueba GPU y certificación visual

Fecha: 2026-10-13  
Estado general: **BLOCKED — runtime preparado; falta el recurso legal `oot.o2r` para iniciar el juego.**  
`DXR_VISUAL_VERIFIED=NOT_RUN`

## Resultado de esta intervención

Se preparó un runtime aislado para la compilación Release y se invocó el preflight de `run-scene.ps1` con `BASELINE_KOKIRI`, backend `D3D12` y perfil `HYBRID_RT` (2). El preflight terminó con código 31 y `HARNESS_OTR_NOT_FOUND` para `runtime/build-test/oot.o2r`. El launcher se detuvo antes de `Start-Process`: no se inició `soh.exe`, no se inicializó D3D12 y no se ejecutó código GPU del juego.

La búsqueda recursiva de `oot.o2r` en el paquete del proyecto no encontró ninguna copia. No se descargó, creó ni sustituyó ese archivo.

## Runtime preparado

Ruta: `runtime/build-test/`

| Archivo | Tamaño | SHA-256 | Origen/estado |
|---|---:|---|---|
| `soh.exe` | 34,278,400 | `FEB608390D9EE1436389C0B3E2328D6F661A660251598CDF0231DCDBFFD5A72B` | Release validado; coincide con el recibo OCZ-RTX-REPAIR |
| `soh.o2r` | 1,184,426 | `CBF3227F7C52A595C211B60D4937C6A0CE0DF691511FEC526E4AD509E9A5EB17` | Salida del mismo staging Release |
| `es.o2r` | 99,610 | `D9037A8A4086D0073F90CC297FCBFE558A78AC3758F27B11A250537883FD990D` | Recurso español del paquete; también copiado a `mods/es.o2r` |
| `mods/es.o2r` | 99,610 | `D9037A8A4086D0073F90CC297FCBFE558A78AC3758F27B11A250537883FD990D` | Copia de la localización incluida en el template |
| `gamecontrollerdb.txt` | 584,016 | `8055F1EA5F2EB367476A57FD153429D1EC3C0AA2E6C93E88A5EA85C922245F2D` | Template local |
| `shipofharkinian.json` | 12,084 | `CFC067F5DB6423483C4EEA5835248327D0405741BE450A85A6C1CA65EA1E080B` | Configuración independiente; JSON validado |

La configuración de prueba establece `gLanguage=Spanish`, `VisualProfile=2` (Hybrid RT) y `Autosave=0`. El launcher también imprime y fuerza `COUCH_D3D12=1` y `COUCH_VISUAL_PROFILE=2`; el preflight confirmó esos valores, pero el juego no llegó a leerlos.

`runtime/build-test/Save/` se creó vacío. No se encontró ningún `.sav` dentro de este runtime (inventario: **0 archivos**). `runtime/build-test/oot.o2r` está ausente; el archivo no se reemplazó con un marcador.

## Identidad del build

- Ejecutable verificado: `build/OCZ_RTX_HOTFIX_02_RELEASE/source_clean/x64/Release/soh.exe`.
- Su SHA-256 actual coincide con el registrado en `reports/OCZ_RTX_REPAIR_RESULT.md`.
- Checkout: `source/shipwright`, HEAD `4aaad850bd5540cd77c2d83f3ad348d3b38605b2`.
- Comparé SHA-256 del checkout actual contra `build/OCZ_RTX_HOTFIX_02_RELEASE/source_clean` para 15 archivos C++/HLSL clave de DXR, material, harness y SaveManager: **15/15 coinciden**.
- Se conservó el árbol fuente con cambios locales preexistentes. En esta fase no se editaron archivos de fuente ni la versión estable; la preparación quedó en el runtime nuevo y este informe.

## GPU y preflight

`nvidia-smi -L` detectó **NVIDIA GeForce RTX 3070 Ti Laptop GPU**. Al terminar el preflight no había ningún proceso `soh*.exe` ejecutándose.

Invocación probada:

```powershell
source/shipwright/scripts/scene-harness/run-scene.ps1 `
  -RunScenario BASELINE_KOKIRI `
  -RendererBackend D3D12 `
  -VisualProfile 2 `
  -Executable runtime/build-test/soh.exe
```

Resultado: `HARNESS_OTR_NOT_FOUND : ...\runtime\build-test\oot.o2r`, salida 31. El flujo valida `es.o2r`, `oot.o2r` y `soh.o2r` antes de consultar carga GPU, crear el sandbox de ejecución o iniciar el proceso. No se generó una carpeta de ejecución ni una captura.

## Aislamiento de guardados

- El Save de este runtime es una carpeta nueva, vacía y diferente de la ruta que usará el harness para `reports/scene_harness/<runId>/sandbox/Save`.
- Revisé los componentes de ruta existentes de `runtime/build-test`, `runtime/build-test/Save` y `reports/scene_harness`: **ninguno es un reparse point** (symlink/junction). El launcher no creó un runId porque se detuvo antes.
- En `SaveManager.cpp`, con `COUCH_DXR_HARNESS=1`, `GetSaveDirectory()` exige `COUCH_DXR_SAVE_ROOT`, normaliza la ruta y rechaza que resuelva al Save normal. Las funciones de guardado consultan esa ruta. `run-scene.ps1` construye el sandbox en el árbol de informes y asigna explícitamente `COUCH_DXR_SAVE_ROOT` al proceso.
- La comprobación de ruta y código es estática/preparativa: **el SaveManager no se ejecutó**, así que la selección efectiva de ruta aún no tiene recibo de runtime.
- No se abrieron ni modificaron partidas personales. Como el juego no se ejecutó, no existe hash “antes/después” de una partida personal; el inventario verificable del nuevo sandbox base es cero archivos `.sav`.

## Gates de certificación

| Gate | Estado | Evidencia/límite |
|---|---|---|
| Identidad del ejecutable | **PASS** | Tamaño y SHA-256 coinciden con el Release validado. |
| Correspondencia de fuentes DXR/harness | **PASS** | 15/15 fuentes clave coinciden entre checkout actual y staging compilado. |
| Preparación del runtime | **PARTIAL** | Ejecutable, `soh.o2r`, `es.o2r`, controlador, configuración y Save aislado listos; falta `oot.o2r`. |
| Detección de GPU | **PASS** | `nvidia-smi` identifica la RTX 3070 Ti Laptop. |
| D3D12/DXR en ejecución | **BLOCKED** | El preflight paró antes de iniciar el juego. Disponibilidad DXR no equivale a DXR activo. |
| BLAS/TLAS y ray dispatch | **NOT_RUN** | No hubo inicialización del backend ni fotogramas del juego. |
| Sombras, reflejos e iluminación indirecta | **NOT_RUN** | Sin dispatch ni señales de render del juego. |
| Compositor, HDR, ACES, SDR y Present | **NOT_RUN** | No existe evidencia de framebuffer ni de presentación. |
| Ocho capturas y comparación Raster/Hybrid RT | **NOT_RUN** | No se generó ninguna captura; no se calculó A/B. |
| SaveManager en runtime e integridad de saves | **PARTIAL** | Código y rutas separadas revisados; el ejecutable no llegó a abrirse. Save base vacío y sin enlaces. |
| Controles, HUD, español en pantalla, FPS, frametime, VRAM y estabilidad | **NOT_RUN** | Requieren ejecución; tampoco se probaron Classic/Hybrid ni guardar/recargar. |
| **DXR_VISUAL_VERIFIED** | **NOT_RUN** | No hay ejecución GPU ni evidencia visual concluyente. |

## Único dato necesario para continuar

Proporciona la **ruta exacta de una copia legítima de `oot.o2r`**. Puede estar ya en `runtime/build-test/oot.o2r` o en otra ubicación que autorices para copiarla al runtime independiente. Con el archivo presente, el launcher podrá pasar el preflight y continuar con la certificación solicitada.
