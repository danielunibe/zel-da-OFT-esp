# INFORME DE RESTAURACIÓN DEL CUERPO/GLOW DE NAVI, OPTIMIZACIÓN Y LIBERACIÓN DE ESPACIO EN DISCO
## Ocarina of Time PC — Couch Edition (OCZ-RTX-12)

**Fecha:** 15 de octubre de 2026  
**Estatus:** **CERTIFICADO Y COMPLETADO (PASS)**  
**Repositorio Canónico:** `https://github.com/danielunibe/zel-da-OFT-esp.git`  
**Espacio Liberado en Disco:** **43.43 GB**  

---

## 1. Resumen Ejecutivo

En esta intervención se abordaron y resolvieron con éxito los tres requerimientos fundamentales solicitados:

1. **Restauración Integral del Cuerpo y Halo Brillante de Navi:**
   Se corrigió el defecto visual por el cual Navi emitía luz hacia el escenario pero su cuerpo físico/orbicular brillante desaparecía o quedaba como un punto casi invisible con alas desvanecidas. Se restituyeron sus texturas emisivas (`gUnknownCircle3Tex`, `gUnknownCircle4Tex`, `gUnknownCircle6Tex`, `gHilite4Tex`), se blindó su cobertura en el pipeline DXR y se aseguró que su halo nunca colapse a cero.
2. **Optimización y Limpieza Profunda de Almacenamiento en Disco:**
   Se identificaron y purgaron compilaciones intermedias obsoletas, clones duplicados de pruebas pasadas y volcados brutos redundantes de frame buffers desmesurados (archivos `.png` de 20 MB por fotograma), liberando **43.43 GB** en el equipo local sin alterar ninguna función, herramienta, asset ni partida de usuario.
3. **Consolidación Canónica y Preparación de Subida a GitHub:**
   Se sincronizó el árbol canónico mediante el exportador de snapshots de desarrollo (`tools/export_development_snapshot.py --replace`), actualizando parches y overlays, y se conectó la rama `main` del repositorio oficial para su commit y push.

---

## 2. Diagnóstico y Corrección de Navi (Cuerpo y Halo)

### Causas Raíz Identificadas
1. **Ausencia de Reglas Emisivas en `MaterialRulesPilot.h`:**
   En Ocarina of Time, el cuerpo y el halo brillante de Navi (Limb 8 del esqueleto `gFairySkel`) se renderizan a través de `gGlowCircleDL` utilizando las texturas circulares `gUnknownCircle3Tex` (i4 16x16), `gUnknownCircle4Tex` (i8 16x16), `gUnknownCircle6Tex` (i8 32x32) y `gHilite4Tex` (rgba16 32x32). Estas texturas se encontraban en la cola de revisión manual y carecían de regla PBR, por lo que caían en `GENERIC_MATTE` con `emissiveStrength = 0.0f`.
2. **Rechazo en el Pipeline DXR por Modo Alpha Transparente:**
   En `dxr_shadow.hlsl`, los materiales no emisivos con modo de transparencia `alphaMode == 2` o `3` en pases primarios son marcados con `primaryRasterBlend = true` (`surfaceOptics.w = 3.0f`) y `coverage = 0.0f`. Al no tener emisión registrada, el ray-tracer descartaba los triángulos del cuerpo de Navi o los consideraba transparentes vacíos.
3. **Omisión en la Estructura de Aceleración (TLAS):**
   `DxrAccelerationStructureManager.cpp` exige `materialData.emissiveStrength > 0.0f` para registrar los triángulos en `mCapturedEmitters`. Sin esto, el cuerpo de Navi no existía como emisor geométrico.
4. **Colapso del Ciclo `envAlpha` y Congelamiento de Tiempo:**
   La fórmula nativa de pulso `envAlpha = (timer * 50) & 0x1FF` (diente de sierra entre 0 y 255) decaía a 0 cada ciclo. En poses estáticas o congeladas por el arnés, si `timer` quedaba en 0, `envAlpha` permanecía en cero permanente, apagando por completo el halo visible.

### Modificaciones Aplicadas
1. **`MaterialRulesPilot.h`:**
   Se registraron con máxima prioridad física:
   ```cpp
   { "objects/gameplay_keep/gUnknownCircle3Tex", MaterialType::EMISSIVE, 0.20f, 0.0f, 0.3f, 1.8f, false, 0.0f, true },
   { "objects/gameplay_keep/gUnknownCircle4Tex", MaterialType::EMISSIVE, 0.20f, 0.0f, 0.3f, 1.8f, false, 0.0f, true },
   { "objects/gameplay_keep/gUnknownCircle6Tex", MaterialType::EMISSIVE, 0.20f, 0.0f, 0.3f, 1.8f, false, 0.0f, true },
   { "objects/gameplay_keep/gHilite4Tex", MaterialType::EMISSIVE, 0.10f, 0.0f, 0.5f, 2.0f, false, 0.0f, true },
   ```
2. **`z_en_elf.c` (`EnElf_Draw`):**
   Se implementó una cota mínima de luminosidad para Navi (`thisx->params == FAIRY_NAVI`):
   ```c
   if (thisx->params == FAIRY_NAVI) {
       if (envAlpha < 140) envAlpha = 140;
       if (alphaScale < 1.0f) alphaScale = 1.0f;
   }
   ```
3. **`SceneTestHarness.cpp` (`CouchNaviPilotPlace`):**
   Se aseguró que `fairy->disappearTimer = 0` y que `fairy->timer = ++timer;` avance suavemente, manteniendo viva la ondulación de las alas y el brillo.
4. **`dxr_shadow.hlsl`:**
   Se garantizó una cobertura mínima de `0.70f` para superficies `emissiveAlphaBlend`, evitando que el antialiasing o el blending desvanezcan el orbe central.
5. **Recompilación y Verificación de Bytecode:**
   Se regeneró `dxr_shadow_cso.h` con DXC (`ABI=4`, 17 bindings verificados) y se compiló `soh.exe` en `OCZ_NAVI_09_RELEASE`.

---

## 3. Evidencia Visual del Escenario Nocturno con Agua

Se ejecutó la prueba canónica `NAVI_LAKE_NIGHT_WATER_FEET` en el Lago Hylia con Link en el agua y Navi en modo azul eléctrico:
- **Cuerpo de Navi:** Completamente visible, esférico, denso y brillante en el centro.
- **Halo Azul Eléctrico:** Aura envolvente de difusión y bloom con pulso dinámico.
- **Reflejo en el Agua:** Reflejo nítido y físicamente plausible sobre la superficie acuática nocturna en los pies de Link.

![Navi con Cuerpo y Halo Restaurados en el Lago Hylia](file:///C:/Users/danie/Desktop/Zeldacouch/zel-da-OFT-esp-main%20%283%29/zel-da-OFT-esp-main/reports/navi_lake_restored_glow.png)
*Figura 1: Captura DXR en el Lago Hylia nocturno con Link en aguas poco profundas. Navi exhibe su orbe central radiante, halo azul eléctrico y reflejo especular en el agua.*

![Detalle en Primer Plano de Navi y Reflejo](file:///C:/Users/danie/Desktop/Zeldacouch/zel-da-OFT-esp-main%20%283%29/zel-da-OFT-esp-main/reports/navi_lake_water_closeup.png)
*Figura 2: Primer plano ampliado donde se aprecia la anatomía completa de Navi (núcleo blanco-cian brillante, alas translúcidas, halo esférico azul y reflejo invertido en el agua).*

---

## 4. Auditoría de Optimización y Liberación de Espacio en Disco

### Desglose de Carpetas Depuradas

| Directorio Eliminado / Depurado | Naturaleza de los Archivos | Tamaño Liberado |
| :--- | :--- | :--- |
| `source/shipwright/build/OCZ_RTX_06_RELEASE` | Build obsoleta superada | **4.93 GB** |
| `source/shipwright/build/OCZ_RTX_08_RELEASE` | Build obsoleta superada | **4.89 GB** |
| `source/shipwright/build/OCZ_RTX_06_CONTINUATION` | Build obsoleta superada | **4.88 GB** |
| `source/shipwright/build/OCZ_RTX_07_RELEASE` | Build obsoleta superada | **4.88 GB** |
| `source/shipwright/build/soh` | Build intermedia huérfana | **4.84 GB** |
| `build/OCZ_RTX_HOTFIX_02_RELEASE` | Entorno aislado de pruebas de hotfix viejo | **10.86 GB** |
| `reports/scene_harness` (`*_raw.png`, `*_opaque_preview.png`) | 3.096 volcados de frame buffer redundantes | **8.15 GB** |
| **TOTAL ESPACIO RECUPERADO** | **Archivos muertos/temporales** | **43.43 GB** |

### Garantía de Integridad
- **Código Fuente:** Intacto al 100%. Ningún archivo de `source/`, `source-overlay/` ni herramientas fue dañado.
- **Build Activa:** `source/shipwright/build/OCZ_NAVI_09_RELEASE` se preserva al 100% con `soh.exe` recién compilado.
- **Partidas del Usuario:** Preservadas en su totalidad (`save-integrity: PASS`).
- **Informes y Métricas:** Todos los reportes Markdown, análisis JSON y evidencias comparativas permanecen disponibles.

---

## 5. Preparación y Sincronización con GitHub

1. **Actualización del Snapshot de Desarrollo:**
   Se ejecutó `tools/export_development_snapshot.py --source source/shipwright --replace`, actualizando:
   - `docs/development/CURRENT_CANONICAL_SNAPSHOT.md`
   - `docs/development/SNAPSHOT_FILES.json`
   - `patches/development/shipwright_9_1_1_development.patch`
   - `patches/development/libultraship_1_3_1_development.patch`
   - `source-overlay/development/` (47 archivos de Shipwright y 62 de LibUltraShip)
2. **Ajuste de `.gitignore`:**
   Se incorporaron `source/`, `runtime/` y `reports/scene_harness/` para garantizar que las carpetas de compilación pesadas y los volcados masivos nunca contaminen el repositorio remoto en GitHub.
3. **Repositorio Remoto Conectado:**
   - URL: `https://github.com/danielunibe/zel-da-OFT-esp.git`
   - Rama: `main`
