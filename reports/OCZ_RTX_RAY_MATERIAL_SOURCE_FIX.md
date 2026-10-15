# OCZ RTX — Correspondencia entre rayos, geometría y materiales

Fecha: 2026-10-13. Alcance solicitado: revisión y corrección de fuente, sin pruebas del juego.

Estado de esta revisión del 13 de octubre: **SOURCE_FIXED / STATIC_REVIEW_ONLY / BUILD_NOT_RUN / GAME_NOT_RUN / GPU_HITS_NOT_VERIFIED**.

Actualización posterior: el 14 de octubre se compiló esta fuente y se midieron impactos y respuesta de luz con un harness oculto. Los resultados posteriores están en [la prueba GPU en segundo plano](OCZ_RTX_BACKGROUND_RAY_GPU_RESULT.md); las afirmaciones estáticas de este informe se conservan como registro de la intervención original.

## Resultado principal

La ruta que construye la escena DXR tenía un error de índices capaz de producir triángulos degenerados después del primer lote. El raster dibuja sus propios vértices y puede verse correcto aunque el BLAS contenga esas primitivas degeneradas. Crear BLAS/TLAS y emitir `DispatchRays` no comprueba que esas primitivas representen las superficies visibles.

Se corrigió ese contrato junto con errores de momento de captura, transformación, UV, descriptor de textura, persistencia de atributos y constantes de sombras. La fuente ahora contiene controles para rechazar datos inválidos y discrepancias entre impacto y profundidad. Esta intervención no aporta evidencia de impactos ejecutados en GPU ni una certificación visual.

## Autoridad de fuente

- Shipwright: `source/shipwright`, base `4aaad850bd5540cd77c2d83f3ad348d3b38605b2`.
- LibUltraShip: `source/shipwright/libultraship`, base `17a0b7939bd05f5e617cef89457ca43774fc9a9f`.
- Los cambios de esta revisión están en LibUltraShip. Se preservaron los cambios previos de materiales, HDR/SDR y composición.
- El overlay y los parches de desarrollo deben exportarse desde estas fuentes; el exportador no compila shaders ni ejecuta el juego.

## Defectos y correcciones

### 1. Índices locales publicados como globales — causa directa de geometría incorrecta

`GeometryCapture::RecordTriangle` genera índices locales al lote. `FlushDraw` los agregaba sin su base a `mIndices`, un búfer global. `PersistentScene::IngestCapturedDraw` suponía que eran globales, restaba `draw.vertexStart` y convertía índices inferiores a la base en cero.

Ejemplo derivado del código anterior, sin ejecutar una prueba:

| Paso | Segundo lote con tres vértices |
|---|---|
| Base global de sus vértices | `vertexStart = 3` |
| Índices publicados antes | `[0, 1, 2]` |
| Conversión anterior a índices locales | `[0, 0, 0]` |
| Triángulo enviado a DXR | Tres referencias al mismo vértice; área cero |
| Índices globales publicados ahora | `[3, 4, 5]` |
| Conversión a índices locales ahora | `[0, 1, 2]` |

Corrección: `FlushDraw` suma `cd.vertexStart` antes de publicar cada índice. `PersistentScene` rechaza referencias fuera del lote y aumenta `danglingBufferReferences`; no las sustituye por el vértice cero. El hash de diagnóstico de captura también descuenta esa base para conservar una identidad local estable.

Fuentes: `src/fast/GeometryCapture.cpp`, `src/fast/PersistentScene.cpp`.

### 2. Un triángulo podía pertenecer al lote o al estado equivocado

El registro de DXR ocurría antes de las llamadas `Flush` que cambian textura, shader, profundidad, viewport o sampler en `GfxSpTri1`. El vaciado podía capturar el triángulo nuevo dentro del lote anterior. Además, `Flush` consultaba el RSP/RDP vivo, que podía contener el estado del siguiente dibujo.

Corrección: el registro ocurre después de preparar el estado raster y emitir las UV. El lote conserva `DrawCallInfo`, cámara y descriptor de textura desde su primer triángulo. En los perfiles con materiales/captura, un cambio de esos datos vacía el lote. La creación de un shader termina primero el lote que usa la disposición anterior de vértices.

Los rectángulos y dibujos a framebuffers auxiliares no se incorporan a la TLAS principal. La condición adicional de separación por material se limita a perfiles mejorados o captura activa.

Fuente: `src/fast/interpreter.cpp`.

### 3. La transformación se tomaba demasiado tarde

Fast3D transforma al cargar cada vértice; la captura guardaba la posición local y usaba la matriz vigente al vaciar el lote. Cambiar o retirar una matriz entre ambos momentos podía colocar el BLAS en una posición distinta del triángulo raster.

Corrección: cada vértice capturado conserva su matriz de carga. Se separan lotes de transformaciones distintas. Un triángulo con vértices cargados bajo varias matrices se transforma por vértice a espacio de vista, usa instancia identidad y deriva una normal geométrica en ese espacio. Esta ruta conserva las posiciones, aunque su normal plana puede diferir del sombreado suave original.

La conversión existente de la matriz Fast3D a `D3D12_RAYTRACING_INSTANCE_DESC::Transform` se conserva: las instancias DXR están en espacio de vista. Se rechazan transformaciones no finitas o singulares antes de construir sus descriptores.

Fuentes: `src/fast/GeometryCapture.cpp`, `src/fast/backends/DxrAccelerationStructureManager.cpp`.

### 4. UV y textura de impacto no representaban necesariamente el dibujo

La captura usaba `v->tc / 32` antes de la escala de textura, generación de UV, `ModifyVertex`, desplazamientos del tile, shifts, medio texel y normalización por tamaño que aplica el raster. Además, buscar el SRV más reciente por hash del recurso podía asignar otra variante de tile/paleta; las importaciones desde caché tampoco equivalen a una nueva carga de textura.

Corrección: se copian las UV finales emitidas al VBO para la primera textura utilizada. La identidad del recurso viaja con los metadatos de carga del tile. El backend expone el descriptor actualmente ligado a ese tile y el lote lo guarda. La tabla de materiales DXR usa ese descriptor capturado.

Esta corrección vincula el impacto con la textura primaria del dibujo. El evaluador DXR sigue siendo una aproximación del combiner Fast3D: no reproduce todas las combinaciones de dos texturas, máscaras, reemplazos o colores primitivos/entorno. Sus samplers de materiales siguen siendo lineales con WRAP; la igualdad completa con los modos de filtro, espejo y clamp raster queda pendiente.

Fuentes: `src/fast/interpreter.cpp`, `src/fast/backends/gfx_direct3d12.cpp`, `src/fast/backends/DxrAccelerationStructureManager.cpp`.

### 5. La persistencia reutilizaba atributos de una captura anterior

La identidad de un asset consideraba posiciones/índices y material, pero omitía UV, normales, colores y metadatos de alpha. Dos dibujos con la misma topología podían compartir los atributos del primero.

Corrección: la identidad de superficie incluye esos atributos, el estado y el descriptor. La identidad del BLAS se calcula separadamente con posiciones, índices y clase alpha, que controla su flag OPAQUE. Cambiar únicamente el sombreado reutiliza el BLAS y actualiza los datos de impacto del frame. Se retiran variantes CPU anteriores de una topología cuando hay una variante actual activa, preservando todas las usadas en el frame.

La firma de reutilización de la TLAS se calcula sobre los descriptores realmente emitidos en su orden de `InstanceID`. Un frame sin escena activa invalida la TLAS anterior.

Fuentes: `src/fast/PersistentScene.cpp`, `src/fast/backends/DxrAccelerationStructureManager.cpp`.

### 6. La pasada de sombras exponía constantes incompletas

Su bloque CPU contenía únicamente el prefijo de `FrameConstants`, con slices de 256 bytes. `AnyHitShadow` llama al decodificador de materiales; el cálculo de LOD puede leer `gProjection`, situado mucho más adelante en el cbuffer HLSL. Esa lectura no estaba respaldada por el bloque CPU correspondiente.

Corrección: sombras y superficies comparten `DxrFrameConstants` completo: 1408 bytes, slices alineados de 1536 bytes. Incluye proyección, viewport y límites lógicos de los búferes. Se añadieron `static_assert` de tamaño y offsets; estos controles se ejecutarán cuando se compile, no se ejecutaron en esta revisión.

El cambio de layout requiere regenerar DXIL junto con C++. El CMake de Shipwright ya exige `dxc` y genera el header desde `dxr_shadow.hlsl` en el directorio de build. No se regeneró bytecode en esta intervención.

Fuentes: `include/fast/backends/DxrAccelerationStructureManager.h`, `src/fast/backends/DxrShadowRenderer.cpp`, `src/fast/shaders/dxr_shadow.hlsl`.

### 7. Correspondencia entre rayo y superficie visible

Los candidatos de cámara ahora distinguen vista, proyección, escala horizontal y viewport. El shader convierte el centro del pixel de profundidad al viewport elegido. Sombras y superficies usan esa misma conversión; las consultas del entorno y de la sombra raster aplican su conversión inversa.

El rayo primario parte del plano cercano reconstruido y sigue la línea cercano-lejano en espacio de vista, compatible también con una proyección ortográfica finita. Para impactos de dibujos que escriben profundidad, se exige concordancia con la distancia reconstruida (`max(0.05, distancia * 0.002)`). Los dibujos transmisivos sin `Z_UPD` pueden estar legítimamente delante del fondo de profundidad y no se rechazan por esa diferencia.

Los rebotes de reflexión, transmisión e iluminación indirecta parten del punto `Origin + Direction * hitDistance`, seguido del desplazamiento normal existente. Las vistas de materiales usan el mismo rayo; se retiraron las bandas que ensayaban rayos en espacio de mundo, matrices transpuestas y dirección invertida.

`ValidatePrimaryHit` se aplica antes de leer datos en AnyHit productivo y en ClosestHit, además del modo diagnóstico. Comprueba `InstanceID`, primitiva, índices, vértices, metadatos y límites de textura. Los tamaños físicos se comprueban en CPU contra `ID3D12Resource::GetDesc().Width`, antes de publicar los límites lógicos y despachar. Los SRV raíz de D3D12 contienen una dirección GPU sin tamaño de búfer; consultar sus dimensiones desde HLSL no aporta ese contrato. Se contrastó esta elección con la [especificación de recursos de Microsoft](https://microsoft.github.io/DirectX-Specs/d3d/ResourceBinding.html#root-descriptors).

La discrepancia profundidad/impacto usa código 11, color cian en la vista de gates y contador reservado 488, incluido en las invariantes del readback.

Fuente: `src/fast/shaders/dxr_shadow.hlsl`, `src/fast/backends/DxrSurfaceRenderer.cpp`.

## Qué puede afirmarse al cerrar esta revisión

- Se localizaron defectos concretos en la ruta fuente que explica cómo el raster podía verse correcto con geometría DXR incorrecta.
- Se aplicaron correcciones a esos contratos y se revisaron sus llamadas, estructuras compartidas y diffs respecto del estado anterior a esta intervención.
- Las comprobaciones de impactos inválidos rechazan el dato y conservan el fallback; no fuerzan cobertura ni fabrican impactos.
- La fuente mantiene una cámara dominante por frame. Escenas con varias proyecciones/vistas requieren evidencia específica; esta revisión no certifica su cobertura completa.
- La ruta de triángulos con matrices mixtas puede aumentar el trabajo de BLAS para geometría animada. Su rendimiento no se midió.
- No se compilaron C++ ni HLSL, no se ejecutaron tests ni harness, y no se abrió el juego. El ejecutable anterior no incorpora estas correcciones.

Para confirmar en GPU que los rayos golpean los materiales esperados, falta un build coherente CPU/DXIL y evidencia de impacto, identidad de material, normal, concordancia de profundidad y composición en escenas reales. Los contadores de creación/dispatch anteriores no sustituyen esa evidencia.

La revisión previa de color y composición está en [OCZ_RTX_SOURCE_ONLY_REVIEW_AND_FIX.md](OCZ_RTX_SOURCE_ONLY_REVIEW_AND_FIX.md).
