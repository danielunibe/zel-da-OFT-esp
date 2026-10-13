# Integración de la base canónica de desarrollo — 12 de octubre de 2026

## Autoridad elegida

La fuente de desarrollo más reciente es el checkout local de Shipwright dentro de `Ocarina_CouchEdition_DEV`, con su submódulo `libultraship`. El repositorio de GitHub `danielunibe/zel-da-OFT-esp` es el destino canónico para preservar el código y sus instrucciones de recuperación.

El snapshot integra el estado de trabajo disponible en ese checkout sobre las mismas bases upstream fijadas por V03.1.1:

- Shipwright `4aaad850bd5540cd77c2d83f3ad348d3b38605b2`, tag `9.1.1`.
- LibUltraShip `17a0b7939bd05f5e617cef89457ca43774fc9a9f`.

No se migró a una revisión upstream posterior. Esto conserva la procedencia y hace que las diferencias entre el snapshot estable y el de desarrollo se puedan reconstruir contra los commits fijados.

## Qué se consolidó

La integración conserva el overlay y los parches de V03.1.1 como material histórico de la última versión estable y añade un snapshot de desarrollo independiente:

- `source-overlay/shipwright/` y `source-overlay/libultraship/`: recuperación de V03.1.1.
- `source-overlay/development/`: 42 archivos modificados o añadidos de Shipwright y 55 de LibUltraShip.
- `patches/development/`: parches reproducibles contra los commits upstream indicados arriba.
- `docs/development/SNAPSHOT_FILES.json`: lista de archivos, tamaños y hashes SHA-256 del snapshot.
- `tools/export_development_snapshot.py`: vuelve a exportar un checkout que conserve exactamente esos commits base.
- `tools/apply_development_overlay.ps1`: aplica el overlay de desarrollo a un checkout limpio con esos commits.

No se detectaron eliminaciones de archivos upstream en el estado fuente exportado. Los dos parches pasaron `git apply --cached --check` contra índices limpios en sus commits base fijados. El exportador no incluye archivos ignorados, artefactos de compilación, informes de ejecución locales, partidas, ROMs ni `oot.o2r`. Las instalaciones del juego y los respaldos quedan fuera del repositorio. La escritura forense de G_SETTIMG quedó desactivada por defecto y ahora solo se habilita con la ruta elegida por el usuario en `COUCH_G_SETTIMG_LOG_PATH`; no se publica la ruta local original.

## Estado técnico

V03.1.1 sigue siendo la versión estable más reciente. El código más reciente se conserva como desarrollo experimental; su presencia en el repositorio no declara una nueva versión estable.

El snapshot incluye trabajo en D3D12/DXR, captura de geometría y escena, diagnósticos y el harness de escenas. Esa implementación no está certificada como RTX visible ni como release. El resultado documentado del 8 de octubre contiene un build Release de 32/32 pasos, pero la ejecución no pasó el verificador: faltaba `saveIntegrity` embebido en `evidence.json`. La comprobación independiente de hashes de partidas y recursos pasó. El gate de niebla sigue bloqueado: se confirmó que hay dos pasadas secuenciales, pero no se probó la causa visual ni el primer límite incorrecto. No se aplicó una corrección visual.

En esta integración a GitHub no se volvió a compilar ni a ejecutar el juego. El snapshot debe considerarse preservado y trazable, no certificado. Los informes locales completos permanecen en el entorno de desarrollo y no se copiaron, porque contienen rutas locales y hashes de datos de ejecución.

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