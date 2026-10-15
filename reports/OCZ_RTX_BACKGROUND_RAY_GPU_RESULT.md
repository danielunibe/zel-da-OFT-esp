# OCZ RTX — Prueba GPU de rayos y materiales en segundo plano

Fecha: 2026-10-14. Solicitud: comprobar impactos reales y corregir fallos, sin mostrar el juego ni interferir con el monitor.

## Resultado

**RAY_MATERIAL_GPU_OBSERVATION = PASS, limitado a Kokiri Forest, escena 0x55, cuarto 0, 640×480, HYBRID_RT.**

El binario final produjo impactos, normales, distancias e identificadores de instancia válidos. La respuesta difusa a la luz del juego coincide con la ecuación Lambert en 285,010 píxeles. En la ejecución normal, las señales de reflexión e iluminación indirecta contienen datos finitos y la composición modifica 249,707 píxeles del mismo fotograma antes del tonemapping.

**DXR_VISUAL_VERIFIED = PARTIAL. PHYSICAL_PRESENTATION = NOT_RUN.** Se omitió deliberadamente `Present`. El verificador general mantiene `PARTIAL` porque no se ejecutó una ablación controlada DXR OFF/ON ni se certificaron apariencia final, todas las escenas o presentación física. La diferencia entre etapas del mismo fotograma es una medición de composición, no una certificación de calidad visual.

## Autoridad y ejecutable

- Fuente: `source/shipwright`; HEAD `4aaad850bd5540cd77c2d83f3ad348d3b38605b2`.
- LibUltraShip: `source/shipwright/libultraship`; HEAD `17a0b7939bd05f5e617cef89457ca43774fc9a9f`.
- Compilación: `build/OCZ_RTX_HOTFIX_02_RELEASE/cmake`, Release, Visual Studio 2022, destino `soh`, paralelismo 2. `CMAKE_HOME_DIRECTORY` apunta al espejo `build/OCZ_RTX_HOTFIX_02_RELEASE/source_clean`.
- Las 42 entradas de Shipwright y 57 de LibUltraShip del manifiesto coinciden entre fuente canónica, overlay y espejo de compilación, usando normalización CRLF/LF. Evidencia: [paridad de fuente](OCZ_RTX_BACKGROUND_SOURCE_PARITY.json).
- Ejecutable comprobado: `runtime/background-ray-validation-20261013-213613/soh.exe`.
- Tamaño final: **34,308,608 bytes**.
- SHA-256 final: **F6A50D2978CD1031C0EB003FED404D3D8DC2D77C26C516142EA69F62A421B32C**.
- El ejecutable del runtime coincide byte por byte con el generado por la compilación.
- GPU: NVIDIA GeForce RTX 3070 Ti Laptop GPU, DXR Tier 1.2, D3D12; capa de depuración solicitada y confirmada por log.

La versión estable y los otros runtimes del usuario se preservaron. Estas correcciones y las pruebas actuales están en el desarrollo local; este informe no afirma una publicación nueva en GitHub.

## Fallos encontrados y correcciones

### 1. Dimensiones exteriores usadas como tamaño de render

La primera prueba oculta tenía un swap chain de 640×480, pero `GetDimensions` devolvía 656×519, incluyendo los bordes. El log mostraba `raw=(0,0,656,519) d3d12=(0,-39,656,519) renderH=480`. Al omitir `ShowWindow`, la inicialización conservaba esas medidas exteriores en lugar de recibir la actualización de tamaño de cliente.

`gfx_dxgi.cpp` ahora consulta `GetClientRect` después de crear el HWND y publica las dimensiones reales. La prueba posterior mostró `raw=(0,0,640,480) d3d12=(0,0,640,480)` y `clipXScale=1`.

| Medición de HIT_MISS | Impactos verificados | Porcentaje de la captura completa |
|---|---:|---:|
| Antes de corregir el tamaño de cliente | 6,995 | 2.2770% |
| Después de corregirlo, antes del arreglo del diagnóstico de luz | 285,010 | 92.7767% |
| Binario final | 285,289 | 92.8675% |

El denominador es la captura completa, incluido el fondo. No representa una eficiencia de impactos limitada a geometría. Las pruebas independientes pueden variar ligeramente por animación y estado temporal; no constituyen una comparación visual OFF/ON.

### 2. Constantes del compositor con un layout CPU/HLSL distinto

El CPU subía 10 dwords, 40 bytes. HLSL esperaba 48 bytes: después de `float4`, dos escalares dejan ocho bytes libres, insuficientes para un `float3`. La dirección empieza en el byte 32 y el indicador de luz válida en el 44. La subida anterior colocaba esos campos en 24 y 36 y no cubría el layout completo.

La captura fallida de `GAME_LIGHT_LAMBERT` contenía **285,022 píxeles cian**, el marcador de luz ausente, aunque el log del juego registraba una dirección válida. Esa captura queda conservada como fallo real del diagnóstico.

Se añadió un tipo CPU de 48 bytes, padding explícito, 12 constantes raíz y aserciones de offsets. La inicialización usa `D3DReflect` para comprobar el layout del shader realmente compilado; falla si el contrato difiere. Ambos formatos, SDR y HDR, registran:

`result=PASS rootDwords=12 reflectedBytes=48 gameDirOffset=32 gameValidOffset=44`

La dirección del probe se transforma al mismo espacio TLAS de las normales. Los píxeles limpiados sin un registro de impacto se reconocen por su alfa y dejan de poder aparecer como impactos en Truth Mode. El analizador además comprueba el payload original de HIT_MISS.

Referencia del contrato: [reglas de empaquetado HLSL](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-packing-rules) y [constantes en la root signature](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-constants-directly-in-the-root-signature). Los offsets específicos se verificaron contra el shader compilado del proyecto.

### 3. Ejecución realmente oculta y acotada

El backend activa esta ruta exclusivamente con `COUCH_DXR_HARNESS=1` y `COUCH_DXR_BACKGROUND=1`. Crea un HWND sin `WS_VISIBLE`, con `WS_EX_NOACTIVATE` y `WS_EX_TOOLWINDOW`; omite `ShowWindow`, fullscreen, captura/movimiento de ratón y registro global de raw input. Usa audio Null e input bloqueado del harness existente.

Las command lists reales se envían a la GPU; `SwapBuffersBegin/End` omiten `Present` y las esperas dependientes del monitor. La prueba comprueba visibilidad y foco del HWND. Los registros de captura indican `window_visible=false`, `window_foreground=false`, `dxgi_present_call=SKIPPED_BACKGROUND`, sin inventar un HRESULT de presentación.

El launcher acepta un runtime aislado, conserva/restaura las variables de entorno y cierra únicamente su propio objeto `Process` al terminar. Un `finally` cubre fallos de verificación o escritura. Las pruebas exitosas declaran `OWN_TEST_PROCESS_STOPPED`.

## Mediciones del binario final

Cada captura regular tiene ocho etapas del mismo fotograma **277**, escena 0x55, cuarto 0, entrada 0xEE, cámara estabilizada durante al menos 30 fotogramas, perfil 2 y resolución 640×480.

| Prueba | Medición GPU | Resultado acotado |
|---|---|---|
| HIT_MISS | 285,289 impactos; cero píxeles verdes sin payload de impacto | PASS |
| WORLD_NORMAL | 285,001 normales válidas; cero errores de longitud fuera de tolerancia | PASS |
| HIT_DISTANCE | 284,996 distancias positivas, codificadas como `saturate(T/256)` | PASS |
| INSTANCE_IDS | 284,996 píxeles con ID; 132 colores hash de instancia distintos | PASS |
| GAME_LIGHT_LAMBERT | 285,010 respuestas válidas; 236,385 iluminadas y 48,625 oscuras | PASS |
| NORMAL | Reflexión: 163,439 píxeles no nulos; indirecta: 252,868; composición: 249,707 modificados | PASS |
| Control geométrico y de material del binario final | 297,895 impactos en 297,897 píxeles con profundidad; cero errores estructurales de índices, vértices, registros o descriptores | PASS |

Lambert se verifica con las normales leídas de la GPU en ese mismo fotograma y la dirección de luz subida por el juego. Error máximo medido: **0.0004884601**; tolerancia del analizador: 0.0015. No se sustituyó la luz por una constante artificial ni se forzó cobertura de materiales.

Los seis resultados anteriores contienen cero píxeles NaN/Inf. En NORMAL, todos los buffers RGBA16F usados en la comparación también son finitos. La diferencia máxima por canal del compositor de superficies es 0.6237793; su media del máximo por píxel es 0.0026820. La medición usa el color preservado antes del compositor y su salida antes de ACES, con dimensiones, formato y fotograma coincidentes.

### Separación entre impacto, profundidad y elegibilidad

El control de gates del binario final mide 9,303 píxeles de fondo y 297,897 con profundidad. De estos últimos, 297,895 llegan a ClosestHit y pasan los controles de IDs y buffers; hay dos misses geométricos. Todos los códigos estructurales de validación 2–10 tienen cuenta cero y los invariantes del readback pasan.

El guard de correspondencia con raster rechaza **13,740** impactos por diferencia de profundidad en este modo diagnóstico. Permanecen 284,155 con material y profundidad válidos. El diagnóstico omite el filtro AnyHit alfa: puede intersectar una parte recortada que el raster no escribió. Estas discrepancias se mantienen como rechazos y no se convierten artificialmente en cobertura válida; no se atribuye la totalidad de esa cuenta a una causa sin medirla.

De los materiales válidos, 30,919 se preservan mediante fallback raster por la política de blend/transparencia; 9,169 elegibles tienen cobertura alfa cero. Se escriben 244,067 píxeles de superficie en ese control. La prueba normal de HIT_MISS y la composición productiva se ejecutaron por separado con el filtro AnyHit habitual. No debe confundirse este control de intersecciones con una demostración de que todos los materiales se iluminan mediante DXR.

## Evidencia conservada

Cada carpeta contiene `manifest.launcher.json`, `evidence.json`, logs, ocho capturas y `primary-ray-analysis.json`. Los manifiestos actuales también registran identidad y hash del ejecutable.

- Primer HIT_MISS: `scene_harness/20261013_215530_708_BASELINE_KOKIRI_HIT_MISS`.
- HIT_MISS con viewport corregido: `scene_harness/20261014_010240_155_BASELINE_KOKIRI_HIT_MISS`.
- Diagnóstico de luz fallido: `scene_harness/20261014_011302_831_BASELINE_KOKIRI_GAME_LIGHT_LAMBERT`.
- Luz final: `scene_harness/20261014_012213_907_BASELINE_KOKIRI_GAME_LIGHT_LAMBERT`.
- HIT_MISS final: `scene_harness/20261014_012312_754_BASELINE_KOKIRI_HIT_MISS`.
- Normales finales: `scene_harness/20261014_012403_110_BASELINE_KOKIRI_WORLD_NORMAL`.
- Distancias finales: `scene_harness/20261014_012500_221_BASELINE_KOKIRI_HIT_DISTANCE`.
- IDs finales: `scene_harness/20261014_012634_914_BASELINE_KOKIRI_INSTANCE_IDS`.
- Composición normal final: `scene_harness/20261014_012815_249_BASELINE_KOKIRI_NORMAL`.
- Gates finales: `scene_harness/20261014_013103_318_BASELINE_KOKIRI_NORMAL`.

El primer intento con deadline de 60 segundos quedó incompleto durante la estabilización, se cerró y no se utilizó como prueba de impactos. El límite de fondo es ahora 180 segundos. Los builds completaron con exit code 0; los logs son `build-background-ray-validation.log`, `build-hidden-viewport-fix.log` y `build-composite-light-layout-fix.log` en el directorio de build. Hay warnings upstream; estos resultados no afirman una compilación libre de warnings.

## Reproducción

Desde la raíz del proyecto, con los recursos locales ya preparados:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\source\shipwright\scripts\scene-harness\run-scene.ps1 `
  -Executable .\runtime\background-ray-validation-20261013-213613\soh.exe `
  -RuntimeDirectory .\runtime\background-ray-validation-20261013-213613 `
  -Background -RunScenario BASELINE_KOKIRI -TruthMode HIT_MISS `
  -VisualProfile 2 -EnableD3D12DebugLayer -PollSeconds 180
```

`tools/analyze_background_ray_capture.py <carpeta-del-run>` mide los payloads. `-PrimaryGateCounters` se usa exclusivamente con `-TruthMode NORMAL`: el diagnóstico de gates omite AnyHit alfa para contar intersecciones estructurales y debe interpretarse aparte de la cobertura del camino normal. `COUCH_DXR_FORCE_ANY_HIT_COVERAGE` permanece en 0.

## Límites de esta comprobación

- La geometría DXR, constantes completas de sombras, UV, descriptores, color y materiales corregidos previamente se compilaron; sus informes de revisión estática se conservan como antecedentes.
- Se comprobó una escena y una resolución. El registro de escenarios todavía marca otras categorías como `UNMAPPED`; no se inventaron escenarios ni se certificó todo el juego.
- La iluminación Lambert es un diagnóstico difuso directo. No certifica fidelidad de todos los combinadores Fast3D, calidad de sombras, reflejos, agua, lava, GI o rendimiento jugable.
- No se ejecutaron controles humanos, presentación al monitor, nuevas ROM/extracciones, instaladores ni una publicación estable.
- El runtime de prueba es independiente. [Cierre e integridad](OCZ_RTX_BACKGROUND_CLOSURE.json): siete mediciones GPU acotadas completas, ocho capturas por ejecución, HWND oculto y sin foco en las evidencias, todos sus procesos cerrados y cero procesos propios restantes. `oot.o2r`, `soh.o2r`, `es.o2r` y `gamecontrollerdb.txt` conservan los hashes iniciales; `mods/es.o2r` coincide con el recurso español. La carpeta `Save` del runtime aislado comenzó vacía: sus checks no equivalen a haber cargado o probado las partidas personales del usuario.

## Snapshot de desarrollo

Se reexportaron 42 archivos de Shipwright y 57 de LibUltraShip. Ambos parches pasaron `git apply --cached --check` contra sus commits base mediante índices temporales, sin modificar el índice ni el árbol de trabajo canónico.

| Parche | Bytes | SHA-256 |
|---|---:|---|
| Shipwright | 375,268 | `735e56c7487635790fca59560013987adeb49855586e99fe17db53fa513eb56b` |
| LibUltraShip | 1,641,693 | `a4506a2cf8cf748e7174ad098bd7f6b14874ae100d52fd829e4bcd511fb0a388` |
