# GUÍA DE RECUPERACIÓN TOTAL DESDE CERO (RECOVERY FROM ZERO)

> **Escenario de Catástrofe:** *"Mi equipo local se ha destruido o formateado por completo; únicamente dispongo del repositorio en GitHub (`danielunibe/zel-da-OFT-esp`)."*

Esta guía documenta el procedimiento determinista, reproducible y validado para reconstruir el entorno completo de desarrollo y ejecución de *Ocarina of Time PC: Couch Edition* desde cero.

---

## 1. Requisitos Previos del Sistema

Instalar en un entorno Windows 10/11 limpio:
- **Git for Windows:** `winget install --id Git.Git -e`
- **Visual Studio 2022 Community / Professional:** Con la carga de trabajo *"Desarrollo para el escritorio con C++"* (MSVC v143, Windows SDK).
- **CMake:** Versión 3.24 o superior (`winget install --id Kitware.CMake -e`).
- **Python 3.10+:** Asegurando agregar Python al `PATH` (`winget install --id Python.Python.3.11 -e`).
- **PowerShell 7 (pwsh):** Recomendado para la ejecución de las herramientas automatizadas.

---

## 2. Clonar el Repositorio Canónico

```powershell
git clone https://github.com/danielunibe/zel-da-OFT-esp.git C:\CouchEdition
cd C:\CouchEdition
```

---

## 3. Preparar el Árbol Fuente Upstream

Couch Edition se construye sobre las versiones fijadas exactas de Harbour Masters:
- **Shipwright:** commit `4aaad850bd5540cd77c2d83f3ad348d3b38605b2` (Bravo 9.1.1)
- **LibUltraShip:** commit `17a0b7939bd05f5e617cef89457ca43774fc9a9f`

Ejecutar el script automatizado de aprovisionamiento de upstream:
```powershell
pwsh -File tools/setup_upstream.ps1
```

*(O manualmente)*:
```powershell
mkdir source
cd source
git clone https://github.com/HarbourMasters/Shipwright.git shipwright
cd shipwright
git checkout 4aaad850bd5540cd77c2d83f3ad348d3b38605b2
git submodule update --init --recursive
cd libultraship
git checkout 17a0b7939bd05f5e617cef89457ca43774fc9a9f
cd ../../..
```

---

## 4. Aplicar las Modificaciones de Couch Edition

Se disponen de dos métodos equivalentes y validados para integrar el desarrollo:

### Método A: Aplicación Directa de Source-Overlay (Recomendado)
Copia el árbol exacto de archivos modificados y nuevos (glifos, tablas de localización, backend D3D11 Enhanced, MaterialRegistry):
```powershell
pwsh -File tools/apply_source_overlay.ps1
```

### Método B: Aplicación de Parches Canónicos Reproducibles
Aplica los parches de producción validados contra upstream limpio:
```powershell
cd source/shipwright
git apply --check ../../patches/shipwright_couch_edition_v03_1_1.patch
git apply ../../patches/shipwright_couch_edition_v03_1_1.patch

cd libultraship
git apply --check ../../../patches/libultraship_couch_edition_v03_1_1.patch
git apply ../../../patches/libultraship_couch_edition_v03_1_1.patch
cd ../../..
```

---

## 5. Compilar la Solución

Ejecutar la suite de compilación asistida con guardián de procesos:
```powershell
pwsh -File tools/build_shipwright_9_1_1.ps1 -Configuration Release
```

El ejecutable `soh.exe` y el recurso base `soh.o2r` se generarán en la carpeta de salida `build/x64/Release/` o `soh/`.

---

## 6. Regenerar o Incorporar Recursos Propios de Localización (`es.o2r`)

Si requiere reconstruir el paquete de localización en español desde los archivos de texto canónicos:
```powershell
python localization/scripts/build_spanish_otr.py
```
O bien, utilizar el `es.o2r` pre-generado localizado en `runtime-template/mods/es.o2r`.

---

## 7. Proporcionar Legalmente los Assets de Ocarina of Time (`oot.o2r`)

> [!IMPORTANT]
> Por restricciones legales y de propiedad intelectual de Nintendo Co., Ltd., los archivos de la ROM y `oot.o2r` **NUNCA** se distribuyen en este repositorio.

1. Obtenga una copia legalmente adquirida de su cartucho o ROM de *The Legend of Zelda: Ocarina of Time* (se admiten ROMs compatibles como Debug ROM, 1.0 NTSC, PAL o GameCube).
2. Coloque el archivo de la ROM en el directorio del extractor (`source/shipwright/OTRExporter/` o en la carpeta raíz del juego).
3. Ejecute el extractor oficial de Shipwright o inicie `soh.exe` por primera vez para que solicite la ROM y genere automáticamente `oot.o2r`.
4. Coloque el archivo resultante `oot.o2r` junto a `soh.exe` en la carpeta de ejecución.

---

## 8. Configuración de Ejecución (Couch Edition V03.1.1)

1. En la carpeta de ejecución (ej. `runtime/`), verifique la presencia de:
   - `soh.exe`
   - `soh.o2r`
   - `oot.o2r` (aportado legalmente por el usuario)
   - `mods/es.o2r` (recurso en español de Couch Edition)
   - `shipofharkinian.json` (configuración con perfil Enhanced habilitado)
2. Inicie `soh.exe`.
3. Abra el menú de configuración (tecla **F1** o botón de menú en mando):
   - **Idioma:** Navegue a `Settings -> General -> Language` y seleccione **Español**.
   - **Canalización Gráfica:** En `Settings -> Enhancements -> Visuals`, confirme que **Visual Profile** se encuentre en **Enhanced**.
4. ¡El entorno está completamente recuperado y listo para desarrollo y disfrute!

---

## 9. Verificación Automatizada del Entorno
Para confirmar que todos los componentes han sido restaurados con integridad SHA-256 perfecta:
```powershell
python tests/validate_recovery.py
```
Resultado esperado: `RECOVERY_VALIDATION: PASS`.
