# Snapshot canónico de desarrollo — 14 de octubre de 2026

## Autoridad elegida

La autoridad de edición actual es `source/shipwright` dentro de este proyecto en `Zeldacouch`, con su submódulo `libultraship`. Los antecedentes de la integración proceden de `Ocarina_CouchEdition_DEV`. El repositorio de GitHub `danielunibe/zel-da-OFT-esp` es el destino para preservar el código y sus instrucciones de recuperación; la actualización y las pruebas del 14 de octubre están en el desarrollo local.

El snapshot integra el estado de trabajo disponible en ese checkout sobre las mismas bases upstream fijadas por V03.1.1:

- Shipwright `4aaad850bd5540cd77c2d83f3ad348d3b38605b2`, tag `9.1.1`.
- LibUltraShip `17a0b7939bd05f5e617cef89457ca43774fc9a9f`.

No se migró a una revisión upstream posterior. Esto conserva la procedencia y hace que las diferencias entre el snapshot estable y el de desarrollo se puedan reconstruir contra los commits fijados.

## Qué se consolidó

La integración conserva el overlay y los parches de V03.1.1 como material histórico de la última versión estable y añade un snapshot de desarrollo independiente:

- `source-overlay/shipwright/` y `source-overlay/libultraship/`: recuperación de V03.1.1.
- `source-overlay/development/`: 42 archivos modificados o añadidos de Shipwright y 57 de LibUltraShip.
- `patches/development/`: parches reproducibles contra los commits upstream indicados arriba.
- `docs/development/SNAPSHOT_FILES.json`: lista de archivos, tamaños y hashes SHA-256 del snapshot.
  Los textos se hashean tras normalizar CRLF a LF; los binarios se hashean sin modificar.
- `tools/export_development_snapshot.py`: vuelve a exportar un checkout que conserve exactamente esos commits base.
- `tools/apply_development_overlay.ps1`: aplica el overlay de desarrollo a un checkout limpio con esos commits.

No se detectaron eliminaciones de archivos upstream en el estado fuente exportado. Los parches se regeneraron contra los commits base fijados y el 14 de octubre pasaron la comprobación de aplicación en índices temporales aislados. El exportador no incluye archivos ignorados, artefactos de compilación, informes de ejecución locales, partidas, ROMs ni `oot.o2r`. Las instalaciones del juego y los respaldos quedan fuera del repositorio. La escritura forense de G_SETTIMG quedó desactivada por defecto y ahora solo se habilita con la ruta elegida por el usuario en `COUCH_G_SETTIMG_LOG_PATH`; no se publica la ruta local original.

## Estado técnico

Actualización del 14 de octubre: se compiló la fuente corregida y se ejecutó un harness GPU oculto a 640×480, sin `Present`. Se corrigieron el tamaño de cliente usado por la cámara y el layout de 48 bytes del compositor de luz, comprobado por reflexión de shader. El binario final registra impactos, normales, distancias, IDs y respuesta a la luz del juego válidos en Kokiri, y una composición normal que modifica píxeles del mismo fotograma. El [informe de prueba en segundo plano](../../reports/OCZ_RTX_BACKGROUND_RAY_GPU_RESULT.md) contiene los resultados y sus límites. La observación GPU de esa escena es PASS acotado; la certificación visual global sigue PARTIAL y la presentación física NOT_RUN.

Identidad del ejecutable aislado comprobado: 34,308,608 bytes, SHA-256 `F6A50D2978CD1031C0EB003FED404D3D8DC2D77C26C516142EA69F62A421B32C`. Fuente, overlay y espejo de compilación coinciden en las 99 entradas del manifiesto. La versión estable y los otros runtimes se preservaron.

Antecedentes de la revisión de fuente:

Actualización de fuente del 13 de octubre: se corrigieron contratos de color HDR/SDR, la integración del destino de ImGui y la generación/validez de las variantes de materiales. La revisión fue exclusivamente estática, sin compilación ni ejecución del juego. El [informe de causas y correcciones](../../reports/OCZ_RTX_SOURCE_ONLY_REVIEW_AND_FIX.md) documenta los defectos y los límites de esta intervención. El ejecutable anterior no incorpora estos cambios.

Continuación de la revisión de fuente: se corrigió la conversión de índices locales/globales que podía degenerar las mallas DXR, la sincronización de captura con el lote raster, las transformaciones de carga, UV y descriptor de textura, la identidad de atributos persistentes, el layout completo de constantes de sombras y la correspondencia viewport/profundidad/impacto. Las comprobaciones de límites físicos se realizan en CPU antes del dispatch y los índices se validan en HLSL. Se regeneró el snapshot de desarrollo sin compilar ni ejecutar pruebas. El [informe de rayos y materiales](../../reports/OCZ_RTX_RAY_MATERIAL_SOURCE_FIX.md) explica las causas, la implementación y las limitaciones pendientes; no constituye certificación de impactos GPU ni visual.

V03.1.1 sigue siendo la versión estable más reciente. El código más reciente se conserva como desarrollo experimental; su presencia en el repositorio no declara una nueva versión estable.

El snapshot incluye trabajo en D3D12/DXR, captura de geometría y escena, diagnósticos y el harness de escenas. Esa implementación no está certificada como RTX visible ni como release. El resultado documentado del 8 de octubre contiene un build Release de 32/32 pasos, pero la ejecución no pasó el verificador: faltaba `saveIntegrity` embebido en `evidence.json`. La comprobación independiente de hashes de partidas y recursos pasó. El gate de niebla sigue bloqueado: se confirmó que hay dos pasadas secuenciales, pero no se probó la causa visual ni el primer límite incorrecto. No se aplicó una corrección visual.

La integración anterior a GitHub se publicó sin repetir la compilación ni la ejecución del juego. Las pruebas del 14 de octubre y esta actualización del snapshot son locales; no declaran una nueva publicación o release. Los informes completos permanecen en el entorno de desarrollo porque contienen rutas locales y hashes de datos de ejecución.

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
