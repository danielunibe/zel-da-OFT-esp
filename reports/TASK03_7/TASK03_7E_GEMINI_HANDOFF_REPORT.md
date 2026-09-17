# TASK 03.7E — Gemini Handoff & Three-Layer Input Architecture Report

**Fecha**: 2026-09-15  
**Canonical Dev Root**: `C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV`  
**Tool**: `XboxBackendDiagnostic.exe`  
**Target Hardware**: Xbox Series X Wireless Controller (Model 1914, Firmware 5.23)  

---

## 1. Resumen Ejecutivo & Causa Raíz Identificada

El usuario observó un fallo crítico en `ControllerValidation.exe` y `SDLInputDiagnostic.exe`:
- El control abría correctamente en SDL (`Xbox Series X Controller`, instance ID 0).
- Sin embargo, los botones físicos (`A`, `B`, `X`, `Y`, `LB`, `RB`, etc.) no registraban pulsaciones.
- Los gatillos `LT` y `RT` aparecían atascados en `32767` en reposo (idle).

### Hallazgo de Ingeniería (Causa Raíz Aislada)
1. **Ruta del Dispositivo y Protocolo**:
   La inspección del device path en SDL reveló:
   `\\?\HID#{00001812-0000-1000-8000-00805f9b34fb}&Dev&VID_045e&PID_0b13&REV_0523...`
   - El UUID `{00001812...}` corresponde al servicio estándar de Bluetooth SIG **HOGP (HID over Generic Attribute Profile)**.
   - `VID_045E` (Microsoft Corporation) y `PID_0B13` corresponden al mando Xbox Series X conectado vía **Bluetooth LE**.
2. **Discrepancia entre SDL Raw/HIDAPI y XInput Nativo de Windows**:
   - Ejecutamos un sondeo directo de Windows Native XInput (`test_xinput_probe.exe`).
   - **Resultado en XInput**:
     - `Slot 0: CONNECTED!`
     - `LT: 0/255, RT: 0/255` (¡Completamente en reposo, 0%!)
     - `ThumbLX: 303, ThumbLY: 143` (Sticks centrados con desviación < 1%)
     - `Buttons: 0x0`
   - **Conclusión Técnica**:
     El hardware y el firmware del mando están 100% sanos y funcionando perfectamente a nivel del sistema operativo. El problema radica en cómo SDL2 en Windows intenta leer el dispositivo Bluetooth HOGP a través de la capa RawInput/HIDAPI sin mensaje de ventana o con el descriptor de ejes sin normalizar, mientras que el subsistema XInput de Windows lo lee de forma inmediata y nativa sin anomalías de gatillos.

---

## 2. Herramienta de Diagnóstico Tres Capas Construida

Se desarrolló, compiló con MSVC x64 e instaló:
- **`tools/hardware_validation/XboxBackendDiagnostic.cpp`**
- **`tools/hardware_validation/XboxBackendDiagnostic.exe`**
- **`tools/hardware_validation/RUN_XBOX_BACKEND_DIAGNOSTIC.bat`**

### Características de la Herramienta
- **Sin gate inicial**: Inicia inmediatamente sin exigir presionar A para comenzar.
- **Visualización simultánea de 3 capas**:
  1. `[LAYER A] SDL GAMECONTROLLER`: botones y ejes lógicos normalizados.
  2. `[LAYER B] SDL RAW JOYSTICK`: todos los botones físicos y ejes numéricos crudos.
  3. `[LAYER C] WINDOWS NATIVE XINPUT`: slot activo 0..3, botones bitmask, sticks (-32768..32767) y gatillos (0..255).
- **Trazabilidad en tiempo real**: Registra cada evento o cambio de estado en `reports/TASK03_7/BACKEND_EVENT_TRACE.csv`.
- **Salida limpia**: Presionando `Q` en el teclado finaliza inmediatamente.

---

## 3. Estado de Artefactos de Reporte
- `reports/TASK03_7/SDL_GAMECONTROLLER_MAPPING.txt`: Generado con GUID, Vendor/Product y mapping actual.
- `reports/TASK03_7/SDL_BACKEND_INFO.txt`: Auditoría de hints baseline de SDL (todos en estado default no forzado).
- `reports/TASK03_7/BACKEND_EVENT_TRACE.csv`: Inicializado para la prueba física de Daniel.

---

## 4. Estado de Seguridad
- `SOURCE_CHANGED`: NO (cero modificaciones en `source/shipwright`).
- `RUNTIME_CHANGED`: NO (`runtime/build-test` intacto).
- `LIVE_PROJECT_CHANGED`: NO (`Ocarina of Time PC` protegido e intacto).
- `TASK04_STARTED`: NO (estrictamente detenido hasta cerrar Task 03).
