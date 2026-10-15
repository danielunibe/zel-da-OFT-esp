# ARQUITECTURA Y ESTRUCTURA DEL PROYECTO (PROJECT STRUCTURE)

Este documento detalla la taxonomía y propósito de cada directorio dentro del ecosistema de *Ocarina of Time PC — Couch Edition*, clasificando su estado operativo para evitar confusiones entre entornos de desarrollo activos, artefactos históricos y componentes canónicos.

---

## Taxonomía de Estados
- **CANONICAL:** Fuente de verdad oficial y definitiva. Componentes obligatorios para la compilación, ejecución o distribución.
- **WORKSPACE:** Áreas de trabajo intermedias de desarrollo activo o generación de recursos.
- **GENERATED:** Recursos producidos a partir de generadores o compiladores propios (reproducibles).
- **ROLLBACK / HISTORICAL:** Versiones previas, parches obsoletos o respaldos archivados para trazabilidad histórica.
- **EXCLUDED:** Componentes locales protegidos por copyright o temporales de compilación que no deben incorporarse a Git.

---

## Autoridad vigente al 15 de octubre de 2026

- V03.1.1 y su overlay permanecen como el último runtime estable documentado.
- El checkout de desarrollo más reciente se preserva bajo `source-overlay/development/` y `patches/development/`, contra los mismos commits upstream fijados.
- D3D12/DXR y el harness del snapshot actual son experimentales; el código no certifica salida RTX visible ni una release.
- Los resultados, hashes locales de guardados/OTR y artefactos de build del diagnóstico de octubre no se importan al repositorio.

## Mapa de Directorios

### 1. Repositorio Canónico (`zel-da-OFT-esp/`)
| Directorio / Archivo | Estado | Propósito |
| :--- | :--- | :--- |
| `README.md` | CANONICAL | Documentación principal, instrucciones de construcción y configuración. |
| `LICENSE` / `NOTICE.md` | CANONICAL | Licenciamiento MIT, términos legales y atribuciones a Nintendo y Harbour Masters. |
| `.gitignore` / `.gitattributes` | CANONICAL | Reglas de exclusión rigurosas y normalización de fin de línea (LF forzoso en parches). |
| `patches/` | CANONICAL | Parches de producción V03.1.1 y snapshot experimental actual, ambos contra commits upstream fijados. |
| `source-overlay/` | CANONICAL | Overlay raíz de V03.1.1 más `development/` para la base más reciente. Cada snapshot se aplica por separado. |
| `localization/` | CANONICAL | Fuentes completas de traducción ES-419 (`spa_message_data_static`), corpus, herramientas y glifos. |
| `materials/` | CANONICAL | Conjunto de datos `material_intelligence_01` (1.407 reglas compiladas, CSVs y clasificadores PBR). |
| `visual/` | CANONICAL | Arquitectura visual, perfiles Enhanced, mapeo de tonos ACES y análisis de niebla/color. |
| `tools/` | CANONICAL | Herramientas de build, exportador del snapshot de desarrollo y aplicador de overlays estable/desarrollo. |
| `tests/` | CANONICAL | Suites automatizadas de pruebas de runtime, semántica de diálogos y validación de parches. |
| `reports/` | CANONICAL | Informes de consolidación y validación histórica; no contiene los hashes privados del diagnóstico local de octubre. |
| `manifests/` | CANONICAL | `PROJECT_MASTER_MANIFEST.json`, exclusiones e inventarios; el manifiesto distingue release estable y snapshot experimental. |
| `runtime-template/` | CANONICAL | Plantilla limpia de distribución con estructura de carpetas y guía de provisión de `oot.o2r`. |
| `archive/legacy-patches/` | HISTORICAL | Parches de versiones anteriores archivados con directiva `DO_NOT_APPLY.md`. |

---

### 2. Entorno de Desarrollo Local (`Ocarina_CouchEdition_DEV/`)
| Directorio | Estado | Descripción y Destino |
| :--- | :--- | :--- |
| `source/shipwright/` | CANONICAL | Autoridad de desarrollo en el checkout local. Sus diferencias actuales alimentan `source-overlay/development/` y `patches/development/`. |
| `source/shipwright/libultraship/` | CANONICAL | Submódulo fijado con MaterialRegistry y D3D11 Enhanced; D3D12/DXR del snapshot actual sigue experimental. |
| `localization_workspace/` | WORKSPACE | Entorno de desarrollo de traducción al español, tablas de mensajes y scripts auxiliares. |
| `glyph_workspace/` | WORKSPACE | Recursos gráficos para fuentes y glifos especiales (acentos, botones). |
| `visual_workspace/` | WORKSPACE | Investigaciones visuales, prototipos de materiales y compilación de perfiles. |
| `visual_workspace/material_intelligence_01/` | CANONICAL | Dataset canónico de clasificación PBR de texturas. Preservado en `materials/`. |
| `stability_workspace/` | WORKSPACE | Diagnósticos de concurrencia, profiling de rendimiento y pruebas de memoria. |
| `consolidation_workspace/` | WORKSPACE | Espacio temporal utilizado para integrar fases visuales y parches. |
| `couch_core_workspace/` / `audio_workspace/` | WORKSPACE | Investigaciones modulares de audio y soporte háptico. |
| `V03_1_1_RUNTIME/` | GENERATED | Binarios compilados y recursos listos para ejecución local. No van a Git salvo `es.o2r`. |
| `build/` | EXCLUDED | Archivos temporales de compilación de Visual Studio / CMake. Excluidos por `.gitignore`. |
| `.freebuff/` / `crash_dumps/` | EXCLUDED | Logs temporales y volcados de depuración. Excluidos. |
