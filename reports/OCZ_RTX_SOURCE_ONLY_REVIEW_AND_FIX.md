# DXR — Causas de imagen incorrecta y corrección en fuente

Fecha: 2026-10-13

Estado de esta revisión del 13 de octubre: `SOURCE_FIXED / BUILD_NOT_RUN / GAME_NOT_RUN / VISUAL_NOT_VERIFIED`.

Actualización posterior: el 14 de octubre se compiló esta fuente y se realizaron mediciones GPU en un runtime oculto. Véase [la prueba de rayos y materiales en segundo plano](OCZ_RTX_BACKGROUND_RAY_GPU_RESULT.md). Este informe conserva los hallazgos y el alcance de la intervención original.

## Conclusión

El pipeline podía registrar DXR solicitado, TLAS válida, rayos despachados, composiciones y presentación aceptada mientras procesaba incorrectamente los colores o seleccionaba un destino incompatible para la interfaz. Esas operaciones acreditan ejecución; no validan la codificación del contenido ni la calidad de la imagen.

La inconsistencia más directa con la apariencia lavada registrada en OCZ-RTX-04 está en la variante raster de materiales: trataba el RGB codificado del combinador Fast3D como radiancia lineal y lo enviaba así al framebuffer FP16. ACES y la transferencia de salida operaban sobre valores incorrectos. También aplicaba la iluminación de materiales después de mezclar la niebla, iluminando el color de niebla como si perteneciera a la superficie.

Los defectos siguientes se demostraron leyendo el código. Esta revisión no determina cuánto contribuyó cada uno al frame histórico ni declara que la imagen del juego esté ya validada.

## Defectos y cambios

### 1. Contrato de color de la variante física

Fuente: `source/shipwright/libultraship/src/fast/backends/gfx_direct3d12.cpp`, `CreateAndLoadNewShader()`.

- La ruta ordinaria usa texturas UNORM y `mSrgbMode` parte de `false`; no hay una llamada a `EnableSRGBMode()` en la fuente de SoH revisada.
- `CouchApplyPhysicalResponse()` recibía el RGB de Fast3D sin decodificarlo. La construcción de `physicalHdrHlsl` desactivaba expresamente la conversión final cuando la respuesta física estaba habilitada.
- Se añadieron conversiones explícitas de entrada y salida: entrada codificada a lineal antes de la iluminación; radiancia lineal para FP16; codificación de salida para la variante SDR. El retorno de materiales protegidos/desconocidos también respeta ese contrato.
- La respuesta se inserta antes de la mezcla de niebla. En la variante HDR, los colores de niebla y grayscale se convierten al mismo espacio lineal; no se modificaron sus intensidades.
- Se evita normalizar normales o posiciones inválidas/cero y se conserva el color base si el resultado de iluminación no es finito.

### 2. Interfaz SDR sobre un destino HDR pendiente

Fuentes: `gfx_direct3d12_gui.cpp::RenderGui()`, `gfx_direct3d12_framebuffer.cpp::RunTonemappingPass()` y `gfx_direct3d12.cpp::EndFrame()`.

- ImGui crea su pipeline para `R8G8B8A8_UNORM`. `StartDrawToFramebuffer(0)` podía seleccionar el destino `R16G16B16A16_FLOAT` mientras `mHdrSceneActive` siguiera activo.
- Los frames de título/2D no necesariamente pasan por el hook de postproceso del mundo. La ruta de framebuffer externo también mantenía activo el HDR principal aunque la escena ya se hubiera resuelto a una textura SDR.
- La interfaz podía encontrar un formato de destino incompatible y `EndFrame()` todavía intentaba resolver el HDR pendiente después de sus dibujos.
- `RenderGui()` ahora resuelve el HDR principal antes de dibujar ImGui. La resolución de la escena externa desactiva el destino HDR principal que no se utilizó, para que la interfaz use el swapchain SDR.

### 3. Variante de materiales ligada al perfil en el momento equivocado

Fuente: `gfx_direct3d12.cpp::CreateAndLoadNewShader()` y `DrawTrianglesPhysical()`.

- El programa se guarda en `mShaderProgramPool` con la pareja `(shaderId0, shaderId1)`, sin el perfil visual.
- Antes, la generación de la respuesta de materiales dependía del perfil seleccionado al crear el shader. Un shader creado en Classic podía reutilizarse en Enhanced/Hybrid sin esa respuesta, mientras shaders creados después sí la incluían.
- La variante se genera independientemente del perfil inicial. La selección de uso continúa en `DrawTrianglesPhysical()`, que admite el transporte físico en los perfiles de mejora.

### 4. Estado de shader válido que omitía el pixel shader HDR

Fuente: `gfx_direct3d12.cpp`, asignación de `physicalShaderValid`.

- El estado validaba el vertex shader y el pixel shader SDR, pero omitía el resultado de `physicalHdrPsHr`.
- Si esa compilación fallaba, se podía anunciar un contrato físico válido y después omitir el dibujo al no poder crear su PSO HDR. Es un fallo condicional demostrado en la lógica; no se afirma que esa compilación fallara en la ejecución anterior.
- La validez y el log ahora incluyen `HDR_PS`; una variante incompleta usa el fallback raster existente. El log previo a la generación describe una variante solicitada, no una compilación completada.

### 5. Conversiones y composición inconsistentes

Fuentes: `DxrSurfaceRenderer.cpp::CreateCompositePipeline()`, `RenderReflections()` y `dxr_shadow.hlsl::MissReflection()`.

- El fondo HDR se decodificaba de sRGB otra vez, aunque ya era lineal. El entorno de reflexiones hacía la misma conversión incondicional.
- El fallback por señal inválida devolvía RGB lineal directamente al destino SDR. Los materiales sin cobertura válida tampoco tenían un retorno exacto del píxel raster.
- Se conserva la muestra original, incluida su codificación y alpha, en fondo, señales inválidas y ausencia de cobertura. El entorno recibe un indicador de formato en el componente libre `gFrameInfo.w`; solo se decodifica cuando procede de UNORM. Se aplica también el escalado horizontal de la cámara al muestreo del entorno.
- La ruta SDR aplicaba ACES por separado a reflexión, GI y bloom, y posteriormente `RunTonemappingPass()` volvía a aplicarlo al framebuffer. La composición conserva ahora señales lineales y deja ACES al postproceso final.
- La radiancia de reflexión ya contenía la emisión, pero se volvía a multiplicar por su intensidad al añadirla y producir bloom. La máscara se usa ahora como elegibilidad, eliminando ese segundo escalado de energía.

## Por qué los estados anteriores podían parecer suficientes

`dispatchCalls`, `signalOutputsWritten` y `compositeDraws` contabilizan trabajo registrado. Un formato FP16 no demuestra que sus píxeles representen luz lineal. El log de color ahora distingue un contrato declarado por el shader de una medición del contenido, y limita sus contadores al pase de postproceso. No se elevó `DXR_VISUAL_VERIFIED`.

## Procedencia y alcance

- Se editaron cinco archivos de la fuente funcional anidada; sus contenidos anteriores coincidían con los del snapshot `build/OCZ_RTX_HOTFIX_02_RELEASE/source_clean` revisado. No se trabajó sobre un informe antiguo como sustituto del código.
- Se sincronizaron `source-overlay/development`, los parches de desarrollo y `docs/development/SNAPSHOT_FILES.json`; se conservaron las bases Shipwright `4aaad850bd5540cd77c2d83f3ad348d3b38605b2` y LibUltraShip `17a0b7939bd05f5e617cef89457ca43774fc9a9f`.
- No se lanzó el juego, no se compiló C++/HLSL ni se ejecutaron suites de pruebas o comprobaciones de aplicación de los parches.
- El ejecutable y el bytecode ya compilados no incorporan esta intervención. El CMake existente genera el header DXIL desde `dxr_shadow.hlsl` durante una futura compilación y prioriza ese header generado; no se regeneró aquí.
- Los framebuffers externos continúan siendo SDR de 8 bits: pueden recortar radiancia superior a su rango antes de ACES. Esta limitación no se convierte en una certificación HDR ni se ocultó mediante un cambio de exposición.

La corrección está implementada en la fuente. Compilación, ejecución y aceptación visual de esta versión quedan sin verificar conforme al alcance solicitado.
