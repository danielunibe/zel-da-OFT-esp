# INFORME FINAL DE MIGRACIÓN Y PRESERVACIÓN MAESTRA EN GITHUB
## Ocarina of Time PC — Couch Edition

**Fecha de Ejecución:** 17 de septiembre de 2026  
**Repositorio Canónico Remoto:** `https://github.com/danielunibe/zel-da-OFT-esp.git`  
**Rama Canónica:** `main`  
**Estatus Global:** **GITHUB_BACKUP_COMPLETO**

---

## 1. Resumen Ejecutivo
Se ha llevado a cabo la consolidación, auditoría e ingesta de todo el desarrollo técnico propio de *Ocarina of Time PC: Couch Edition*, transformándolo en un repositorio Git completamente estructurado, reproducible, auditable y autosuficiente.

Se garantiza que ante la pérdida total del almacenamiento local, el proyecto puede ser reconstruido desde cero a partir de GitHub sin pérdida de código propio, datos de materiales, perfiles visuales, localización o herramientas.

---

## 2. Métricas del Inventario Maestro
- **Total de Archivos Auditados en Raíces:** `75.338`
- **Duplicados Detectados y Mapeados:** `43.888` (documentados en `manifests/DUPLICATE_MAP.csv`)
- **Archivos Excluidos Rigurosamente:** `10.855` (documentados en `manifests/EXCLUDED_FILES.csv`)
- **Archivos Propios Preservados en Git:** Código fuente modificado, parches canónicos, datasets de materiales, localización ES-419, herramientas, pruebas, reportes y documentación.

---

## 3. Auditoría de Seguridad y Copyright
- **Secretos Detectados:** `0 / 0` (`SECRETS_FOUND = 0`). Ninguna clave privada, API key ni token presente.
- **Copyright de Nintendo:**
  - `oot.o2r`: **NO SUBIDO (EXCLUIDO)**.
  - ROMs (`.z64`, `.n64`, `.v64`) y dumps: **NO SUBIDOS (EXCLUIDOS)**.
  - Partidas de guardado personal (`*.sav`): **NO SUBIDOS (EXCLUIDOS)**.
- **Separación Absoluta de Contenido Personal (CV):** Cero archivos personales o profesionales ajenos al proyecto incorporados.

---

## 4. Mecanismos de Recuperación Certificados
1. **Source Development Snapshot (`source-overlay/`):**
   - Modificaciones completas sobre Shipwright (commit `4aaad850bd5540cd77c2d83f3ad348d3b38605b2`).
   - Modificaciones completas sobre LibUltraShip (commit `17a0b7939bd05f5e617cef89457ca43774fc9a9f`).
   - Script automatizado de aplicación: `tools/apply_source_overlay.ps1`.
2. **Reproducible Patch Set (`patches/`):**
   - `patches/shipwright_couch_edition_v03_1_1.patch`: Validado con `git apply --check` limpio contra upstream (Exit Code: 0).
   - `patches/libultraship_couch_edition_v03_1_1.patch`: Validado con `git apply --check` limpio contra upstream (Exit Code: 0).
   - Parches históricos aislados en `archive/legacy-patches/` con `DO_NOT_APPLY.md`.

---

## 5. Datasets y Componentes Clave Preservados
- **Material Intelligence (`materials/material_intelligence_01`):** 1.407 reglas compiladas, clasificaciones de materiales, PBR defaults, listas de protección de UI, superficies acuáticas, metales y emisivos.
- **Localización Español Latino (`localization/`):** Corpus completo de 2.116 mensajes, tabla binaria `spa_message_data_static`, generadores de recursos OTR, glifos extendidos y paquete `es.o2r`.
- **Desarrollo Visual (`visual/`):** Documentación de shaders D3D11, mapeo de tonos ACES Fitted, niebla dinámica F3DEX2 y mapas de pipeline.
- **Herramientas y Suites de Pruebas (`tools/` y `tests/`):** Scripts de compilación con guardián agéntico, validadores de entorno (`validate_recovery.py`) y pruebas de ejecución (`test_v03_1_1_runtime.py`).

---

## 6. Resultado de Pruebas
- `RECOVERY_VALIDATION`: **PASS**
- `PATCH_CLEAN_APPLY`: **PASS**
- `SECRETS_SCAN`: **PASS (0 encontrados)**
- `COPYRIGHT_AUDIT`: **PASS (100% libre de material de ROMs)**
