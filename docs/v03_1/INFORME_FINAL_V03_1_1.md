# INFORME FINAL DE CERTIFICACIÓN Y VALIDACIÓN RUNTIME — OCARINA COUCH EDITION V03.1.1

Fecha: 17 de Septiembre de 2026
Autor: Antigravity AI Assistant
Entorno: Windows 11 x64, Direct3D 11, MSBuild v143 / Visual Studio 2022

---

## 1. Resumen Ejecutivo
Se ha completado con éxito la auditoría, corrección de fuentes, compilación Release limpia, certificación en tiempo de ejecución (runtime real con Direct3D 11) y empaquetado canónico para **Ocarina Couch Edition V03.1.1**.

Todos los defectos detectados en la versión previa han sido subsanados:
1. **Perfil Canónico Unificado:** La clave canónica del perfil visual es `gEnhancements.Graphics.VisualProfile` (0 = Classic, 1 = Enhanced). Se implementó migración automática retrocompatible para la clave legada `gVisualEnhancements.MasterTonemapping`.
2. **Diagnósticos en Tiempo de Ejecución:** El motor ahora registra con precisión la carga de `es.o2r`, la carga de la tabla de textos en español (`spa_message_data_static`), el estado de inicialización del `MaterialRegistry` (1.407 reglas compiladas) y los contadores de pases (`tonemapPassCount`, `atmosphericPassCount`, `materialDrawCallCount`).
3. **Parches Canónicos Limpios:** Generados en UTF-8 sin BOM con terminaciones de línea LF y verificados con éxito mediante `git apply --numstat`, `git apply --check` y `git apply` en worktrees limpios.
4. **Paquete Local Autónomo (`V03_1_1_RUNTIME`):** Contiene los binarios compilados y configurados (`soh.exe`, `soh.o2r`, `es.o2r`, `shipofharkinian.json`, `gamecontrollerdb.txt` e `INSTRUCCIONES_USO.txt`). No contiene `oot.o2r`.
5. **Documentación Oficial:** Sincronizada y publicada en GitHub (`https://github.com/danielunibe/zel-da-OFT-esp.git`).

---

## 2. Métricas y Evidencia en Tiempo de Ejecución

Los resultados obtenidos por la suite de prueba automatizada `tools/test_v03_1_1_runtime.py` son:

- **Modo Classic (`VisualProfile = 0`):**
  - Perfil real detectado: `0`
  - `tonemapPassCount`: `0`
  - Renderizado: Bit-a-bit idéntico a N64 / Shipwright 9.1.1 base sin postprocesado.

- **Modo Enhanced (`VisualProfile = 1`):**
  - Perfil real detectado: `1`
  - `tonemapPassCount`: `301` (> 0)
  - `atmosphericPassCount`: `301` (> 0)
  - `materialDrawCallCount`: `6536` (> 0)
  - `material_rules_loaded`: `1407`

- **Localización ES-419:**
  - Montaje de `es.o2r`: `YES`
  - Carga de tabla de español: `YES`

- **Integridad de Partidas Guardadas:**
  - Hashes SHA-256 de `file2.sav`, `file3.sav` y `global.sav` 100% preservados tras las pruebas.

- **Estado de PBR-Lite:**
  - Reportado estrictamente como `METADATA_ONLY`.

---

## 3. Parches y Reproducibilidad

- `patches/shipwright_couch_edition_v03_1_1.patch`:
  - Codificación: UTF-8 sin BOM, LF.
  - Verificación `git apply --check`: PASS.
  - Aplicación limpia: PASS.

- `patches/libultraship_couch_edition_v03_1_1.patch`:
  - Codificación: UTF-8 sin BOM, LF.
  - Verificación `git apply --check`: PASS.
  - Aplicación limpia: PASS.

---

## 4. Ubicación de Entregables

- **Directorio de Pruebas Runtime:**
  `C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV\runtime\build-test`
- **Paquete Local V03.1.1 para Juego:**
  `C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV\V03_1_1_RUNTIME`
- **Repositorio Público GitHub:**
  `https://github.com/danielunibe/zel-da-OFT-esp.git` (rama `main`, commit `0e79865`)
