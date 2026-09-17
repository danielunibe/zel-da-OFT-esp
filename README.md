# The Legend of Zelda: Ocarina of Time (PC) — Couch Edition V03.1.1

[![Estado de Preservación](https://img.shields.io/badge/Preservación-100%25_Canónica-brightgreen.svg)](#)
[![Licencia](https://img.shields.io/badge/Licencia-MIT-blue.svg)](LICENSE)
[![Perfil Visual](https://img.shields.io/badge/Visual_Profile-Classic_%7C_Enhanced-orange.svg)](#)
[![Localización](https://img.shields.io/badge/Idioma-Español_Latino_(ES--419)-green.svg)](#)
[![Copyright Notice](https://img.shields.io/badge/ROM_redistribution-PROHIBITED-red.svg)](NOTICE.md)

**Ocarina of Time PC — Couch Edition V03.1.1** es la adaptación canónica de preservación y juego en sala ("Couch Edition") construida sobre la plataforma de decompilación *Shipwright* (Ship of Harkinian) y *LibUltraShip* de Harbour Masters. 

Este repositorio constituye el **respaldo maestro completo, auditable y reproducible** de todo el desarrollo propio del proyecto: arquitectura visual Direct3D 11 con mapeo de tonos fílmico ACES, niebla atmosférica F3DEX2, dataset de inteligencia de materiales (1.407 reglas compiladas), aislamiento de interfaz, localización completa al español latinoamericano (ES-419) con glifos auténticos, herramientas de compilación con guardián de procesos y suites de validación automatizadas de tiempo de ejecución.

---

## ⚠️ AVISO CRÍTICO DE DERECHOS DE AUTOR Y POLÍTICA DE ROMS

> [!IMPORTANT]
> **ESTE REPOSITORIO NO CONTIENE, NO DISTRIBUYE Y NUNCA CONTENDRÁ MATERIAL PROTEGIDO POR COPYRIGHT DE NINTENDO CO., LTD.**
>
> Quedan estrictamente excluidos de este repositorio:
> - Archivos `oot.o2r` (recurso de activos de la ROM base).
> - Archivos de ROM de Nintendo 64 (`.z64`, `.n64`, `.v64`).
> - Volcados de cartuchos originales o assets extraídos directamente de la ROM no autorizados.
> - Archivos de guardado personal (`*.sav`).
>
> Para compilar o ejecutar el juego, **cada usuario final debe proporcionar legalmente su propia copia de *The Legend of Zelda: Ocarina of Time*** y extraer sus propios recursos mediante las herramientas oficiales de Harbour Masters. Consulte [`NOTICE.md`](NOTICE.md) para más detalles.

---

## 🌟 Características de Couch Edition V03.1.1

### 🎨 1. Arquitectura Visual y Perfiles en Tiempo de Ejecución
- **Perfil Classic (`0`):** Renderizado bit-a-bit idéntico a Nintendo 64 / upstream puro (`tonemapPassCount = 0`).
- **Perfil Enhanced (`1`):** Pipeline cinematográfico completo con:
  - **Mapeo de tonos ACES Fitted:** Curva de luminancia fílmica preservando saturación en altas luces sin quemar blancos.
  - **Niebla Atmosférica Linealizada F3DEX2:** Inyección y cálculo dinámico de niebla RSP/RDP en espacio HDR antes del tonemapping.
  - **Aislamiento Total de la Interfaz (HUD & Text):** El texto de diálogos, corazones, magia, rupias y botones se dibujan tras el pase de post-procesado para evitar distorsiones cromáticas y garantizar legibilidad perfecta en pantalla grande.
- **Inteligencia de Materiales (`material_intelligence_01`):**
  - Base de datos exhaustiva y 1.407 reglas compiladas (`MaterialRulesPilot.h`) integradas en `MaterialRegistry`.
  - **Estado técnico honesto:** **`METADATA_ONLY`** (clasificación activa en memoria; no se declara sombreado PBR activo en pantalla en esta fase).
  - **RTX / DXR:** **`NOT IMPLEMENTED`** (planificado para etapas arquitectónicas posteriores).

### 🌎 2. Localización Canónica Español Latino (ES-419)
- **2.116 mensajes traducidos y verificados:** Textos de historia, misiones, diálogos y descripciones.
- **Hotfix Canónico de Enrutamiento (`z_message_PAL.c`):** Enrutamiento determinista de mensajes en español vía `sSpaMessageEntryTablePtr` con fallback seguro a inglés (`sNesMessageEntryTablePtr`). Validación certificada con Mido (`0x1042`).
- **Tipografía y Glifos Auténticos:** 11 glifos extendidos en formato I4 con antialiasing nativo y mapeo directo para mandos de consola.

---

## 🛠️ Mecanismos de Recuperación y Desarrollo

Este repositorio proporciona **dos mecanismos independientes y complementarios** para reconstruir el entorno de desarrollo:

### Mecanismo A: Source-Overlay (Desarrollo Directo)
El directorio [`source-overlay/`](source-overlay/) contiene la totalidad del árbol de código fuente modificado y nuevo:
```powershell
pwsh -File tools/apply_source_overlay.ps1
```

### Mecanismo B: Reproducible Patch Set (Parches Canónicos)
Los parches de producción validados contra upstream limpio residen en [`patches/`](patches/):
- `patches/shipwright_couch_edition_v03_1_1.patch` (Target: Shipwright commit `4aaad850bd5540cd77c2d83f3ad348d3b38605b2`)
- `patches/libultraship_couch_edition_v03_1_1.patch` (Target: LibUltraShip commit `17a0b7939bd05f5e617cef89457ca43774fc9a9f`)

Los parches antiguos están aislados en [`archive/legacy-patches/`](archive/legacy-patches/) acompañados de su directiva `DO_NOT_APPLY.md`.

---

## 🚀 Reconstrucción Rápida ("Mi PC desapareció y sólo tengo GitHub")

Consulte la guía completa y detallada en [`docs/RECOVERY_FROM_ZERO.md`](docs/RECOVERY_FROM_ZERO.md).

### Resumen de Pasos:
1. **Clonar repositorio:**
   ```powershell
   git clone https://github.com/danielunibe/zel-da-OFT-esp.git
   cd zel-da-OFT-esp
   ```
2. **Descargar upstream fijado y aplicar overlay:**
   ```powershell
   pwsh -File tools/setup_upstream.ps1
   pwsh -File tools/apply_source_overlay.ps1
   ```
3. **Compilar en Release:**
   ```powershell
   pwsh -File tools/build_shipwright_9_1_1.ps1 -Configuration Release
   ```
4. **Desplegar entorno de ejecución:**
   - Copie `runtime-template/` a su carpeta de juego.
   - Proporcione su archivo legal `oot.o2r` (extraído con la ROM original).
   - Inicie `soh.exe`.

---

## 📁 Arquitectura del Repositorio

Consulte [`docs/PROJECT_STRUCTURE.md`](docs/PROJECT_STRUCTURE.md) para el detalle de roles de cada directorio:
- `docs/`: Documentación técnica, arquitectónica y guías de recuperación.
- `patches/`: Parches canónicos de producción V03.1.1.
- `source-overlay/`: Árbol de código fuente propio modificado de Shipwright y LibUltraShip.
- `localization/`: Fuentes canónicas ES-419, tablas de mensajes, scripts y glifos.
- `materials/`: Datasets, reglas compiladas y taxonomía de `material_intelligence_01`.
- `visual/`: Documentación de pipeline visual, análisis ACES y niebla F3DEX2.
- `tools/`: Herramientas de build, exportación de parches y utilidades de automatización.
- `tests/`: Suites de validación automatizada de runtime y certificación de recuperación.
- `reports/`: Informes técnicos de consolidación y certificaciones QA.
- `manifests/`: `PROJECT_MASTER_MANIFEST.json` y matrices de inventario.
- `runtime-template/`: Plantilla modelo de distribución con estructura de mods y configs.
- `archive/`: Parches históricos y material de referencia archivado.

---

## 🧪 Certificación y Validación de Runtime (Mediciones Reales)

| Parámetro | Perfil Classic | Perfil Enhanced | Estado de Certificación |
| :--- | :--- | :--- | :--- |
| **VisualProfile** | `0` | `1` | PASS |
| **Pases de Tonemapping** | `0` | `> 0` (301 medidos) | PASS |
| **Pases Atmosféricos** | `0` | `> 0` (301 medidos) | PASS |
| **Llamadas Dibujado Materiales** | `0` | `> 0` (6.536 medidos) | PASS |
| **Reglas de Materiales** | 1.407 indexadas | 1.407 indexadas | PASS |
| **Montaje de `es.o2r`** | YES | YES | PASS |
| **Carga de Tabla Español** | YES | YES | PASS |
| **Enrutamiento Mido `0x1042`** | Fallback (ENG) | Directo (SPA) | PASS |
| **Métricas de Rendimiento** | NOT_MEASURED | NOT_MEASURED | INFORMATIVO |

---

## 📄 Licencia y Reconocimientos

- **Couch Edition:** Distribuido bajo licencia [MIT](LICENSE). Copyright (c) 2026 Daniel Uribe y colaboradores.
- **Shipwright / Ship of Harkinian:** [Harbour Masters](https://github.com/HarbourMasters/Shipwright).
- **LibUltraShip:** Copyright (c) 2022 kenix3 (MIT License).
- **The Legend of Zelda:** © 1998 Nintendo Co., Ltd.
