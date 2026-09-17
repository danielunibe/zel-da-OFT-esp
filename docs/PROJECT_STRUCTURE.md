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

## Mapa de Directorios

### 1. Repositorio Canónico (`zel-da-OFT-esp/`)
| Directorio / Archivo | Estado | Propósito |
| :--- | :--- | :--- |
| `README.md` | CANONICAL | Documentación principal, instrucciones de construcción y configuración. |
| `LICENSE` / `NOTICE.md` | CANONICAL | Licenciamiento MIT, términos legales y atribuciones a Nintendo y Harbour Masters. |
| `.gitignore` / `.gitattributes` | CANONICAL | Reglas de exclusión rigurosas y normalización de fin de línea (LF forzoso en parches). |
| `patches/` | CANONICAL | Parches unificados de producción aplicables sobre los commits upstream fijados. |
| `source-overlay/` | CANONICAL | Árbol con todos los archivos propios modificados o añadidos para integración directa. |
| `localization/` | CANONICAL | Fuentes completas de traducción ES-419 (`spa_message_data_static`), corpus, herramientas y glifos. |
| `materials/` | CANONICAL | Conjunto de datos `material_intelligence_01` (1.407 reglas compiladas, CSVs y clasificadores PBR). |
| `visual/` | CANONICAL | Arquitectura visual, perfiles Enhanced, mapeo de tonos ACES y análisis de niebla/color. |
| `tools/` | CANONICAL | Herramientas de build (`agent_build_guard.ps1`, `build_shipwright_9_1_1.ps1`), exportadores y generadores. |
| `tests/` | CANONICAL | Suites automatizadas de pruebas de runtime, semántica de diálogos y validación de parches. |
| `reports/` | CANONICAL | Informes técnicos de consolidación, certificación de runtime y notas de lanzamiento. |
| `manifests/` | CANONICAL | `PROJECT_MASTER_MANIFEST.json`, listas de exclusión e inventarios de integridad SHA-256. |
| `runtime-template/` | CANONICAL | Plantilla limpia de distribución con estructura de carpetas y guía de provisión de `oot.o2r`. |
| `archive/legacy-patches/` | HISTORICAL | Parches de versiones anteriores archivados con directiva `DO_NOT_APPLY.md`. |

---

### 2. Entorno de Desarrollo Local (`Ocarina_CouchEdition_DEV/`)
| Directorio | Estado | Descripción y Destino |
| :--- | :--- | :--- |
| `source/shipwright/` | CANONICAL | Árbol fuente activo de desarrollo. Sus diferencias alimentan `source-overlay/` y `patches/`. |
| `source/shipwright/libultraship/` | CANONICAL | Submódulo LUS con MaterialRegistry, D3D11 Enhanced y extensiones gráficas. |
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
