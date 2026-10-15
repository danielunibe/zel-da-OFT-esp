# Integración de la base canónica de desarrollo — 15 de octubre de 2026

## Autoridad elegida

La fuente de desarrollo más reciente es el checkout local de Shipwright dentro de `Ocarina_CouchEdition_DEV`, con su submódulo `libultraship`. El repositorio de GitHub `danielunibe/zel-da-OFT-esp` es el destino canónico para preservar el código y sus instrucciones de recuperación.

El snapshot integra el estado de trabajo disponible en ese checkout sobre las mismas bases upstream fijadas por V03.1.1:

- Shipwright `4aaad850bd5540cd77c2d83f3ad348d3b38605b2`, tag `9.1.1`.
- LibUltraShip `17a0b7939bd05f5e617cef89457ca43774fc9a9f`.

No se migró a una revisión upstream posterior. Esto conserva la procedencia y hace que las diferencias entre el snapshot estable y el de desarrollo se puedan reconstruir contra los commits fijados.

## Qué se consolidó

La integración conserva el overlay y los parches de V03.1.1 como material histórico de la última versión estable y añade un snapshot de desarrollo independiente:

- `source-overlay/shipwright/` y `source-overlay/libultraship/`: recuperación de V03.1.1.
- `source-overlay/development/`: 47 archivos modificados o añadidos de Shipwright y 62 de LibUltraShip, según `SNAPSHOT_FILES.json`.
- `patches/development/`: parches reproducibles contra los commits upstream indicados arriba.
- `docs/development/SNAPSHOT_FILES.json`: lista de archivos, tamaños y hashes SHA-256 del snapshot.
  Los textos se hashean tras normalizar CRLF a LF; los binarios se hashean sin modificar.
- `tools/export_development_snapshot.py`: vuelve a exportar un checkout que conserve exactamente esos commits base.
- `tools/apply_development_overlay.ps1`: aplica el overlay de desarrollo a un checkout limpio con esos commits.

No se detectaron eliminaciones de archivos upstream en el estado fuente exportado. Los dos parches pasaron `git apply --cached --check` contra índices limpios en sus commits base fijados. El exportador no incluye archivos ignorados, artefactos de compilación, informes de ejecución locales, partidas, ROMs ni `oot.o2r`. Las instalaciones del juego y los respaldos quedan fuera del repositorio. La escritura forense de G_SETTIMG quedó desactivada por defecto y ahora solo se habilita con la ruta elegida por el usuario en `COUCH_G_SETTIMG_LOG_PATH`; no se publica la ruta local original.

## Estado técnico

### Actualización de runtime — 13 de octubre de 2026 (OCZ-RTX-04)

Se localizó una copia compatible de `oot.o2r` y se copió a un runtime aislado; el original y las partidas de V03.1.1 no se modificaron. El binario Release existente se ejecutó en la RTX 3070 Ti Laptop con D3D12 y `HYBRID_RT (2)`. El harness registró DXR Tier 1.2, BLAS/TLAS, despachos y composición, ocho capturas válidas y presentación DXGI exitosa. La ejecución y composición DXR del checkpoint HDR están verificadas; `DXR_VISUAL_VERIFIED=PARTIAL` porque el control raster-only independiente no produjo una captura aceptada tras ACES/SDR y la aceptación visual humana sigue pendiente. Esto no certifica una nueva release ni cambia el estado estable de V03.1.1. Consulta el [informe OCZ-RTX-04](../../reports/OCZ_RTX_04_RUNTIME_UNBLOCK_AND_GPU_RESULT.md).

V03.1.1 sigue siendo la versión estable más reciente. El código más reciente se conserva como desarrollo experimental; su presencia en el repositorio no declara una nueva versión estable.

### Actualización de código Navi — 15 de octubre de 2026

El snapshot se reexportó desde los mismos commits base y ahora incorpora la corrección del material superior `objects/object_mjin/gWarpPadTopTex` como receptor de la contribución puntual de Navi en el compositor de producción. Los controles de posición, tinte azul y ganancia de 128× pertenecen exclusivamente al harness de diagnóstico; no calibran la intensidad nativa ni cambian la partida normal. `SNAPSHOT_FILES.json` y los parches reproducibles registran el contenido exacto exportado.

La ejecución A/B más reciente se capturó en el frame 425 dentro del proceso aislado y mostró diferencias en la tapa de la Trifuerza y la túnica de Link. No hubo `Present` al monitor ni revisión visual humana de esa ejecución; la aceptación artística y la certificación RTX global siguen pendientes. El repositorio conserva el código y la metadata reproducible, no el ejecutable, las partidas, las capturas ni los logs locales.

El snapshot incluye trabajo en D3D12/DXR, captura de geometría y escena, diagnósticos y el harness de escenas. Esa implementación no está certificada como RTX visible ni como release. El resultado documentado del 8 de octubre contiene un build Release de 32/32 pasos, pero la ejecución no pasó el verificador: faltaba `saveIntegrity` embebido en `evidence.json`. La comprobación independiente de hashes de partidas y recursos pasó. El gate de niebla sigue bloqueado: se confirmó que hay dos pasadas secuenciales, pero no se probó la causa visual ni el primer límite incorrecto. No se aplicó una corrección visual.

La publicación reexporta el checkout fijado y enlaza el informe de ejecución. No recompila el motor. El informe publicado omite la ruta absoluta del recurso; los archivos `oot.o2r`, ejecutables, guardados, capturas y logs completos no se incluyen. La certificación visual final permanece parcial.

## Recuperar el snapshot

1. Clona este repositorio.
2. Ejecuta `pwsh -File tools/setup_upstream.ps1` para obtener los commits fijados.
3. Ejecuta `pwsh -File tools/apply_development_overlay.ps1`.

También puedes aplicar los parches por separado:

```powershell
git -C source/shipwright apply ../../patches/development/shipwright_9_1_1_development.patch
git -C source/shipwright/libultraship apply ../../../patches/development/libultraship_1_3_1_development.patch
```

El overlay requiere un checkout limpio; el script verifica las dos revisiones base y se detiene si detecta cambios locales. Para generar un snapshot posterior, utiliza `tools/export_development_snapshot.py --source <ruta-a-shipwright>` y revisa su diff antes de publicarlo.
