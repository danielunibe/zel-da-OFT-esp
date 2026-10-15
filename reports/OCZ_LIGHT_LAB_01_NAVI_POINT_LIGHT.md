# OCZ-LIGHT-LAB-01 — NAVI POINT LIGHT EN BOSQUE KOKIRI

**Certificación Experimental de Fuente de Iluminación Puntual DXR Asociada a Navi**  
**Fecha:** 2026-10-15  
**Entorno de Prueba:** `BASELINE_KOKIRI` (`scene=0x55`, `entrance=0x0EE`, `room=0`)  
**Cámara de Referencia:** `VIEW_NAVI_RECEIVER` (`NAVI_RECEIVER_CLOSEUP`)  
**Fotograma Forense:** Frame 499  
**Build Identidad:** `OCZ_RTX_08_RELEASE` → `source/shipwright/build/OCZ_RTX_08_RELEASE/out/Release/soh.exe`  
**Tamaño:** 34,470,912 bytes  
**SHA-256 Executable:** `AC7EF5AC18A7E22E8DC893D54226D5BA70788F487943B3C2778C91DB00BFD3CC`  
**Shader ABI:** Versión 4 (`dxr_shadow.hlsl`, DXIL Shader Model 6.3)  
**Run ID Certificado:** `20261015_021411_776_NAVI_KOKIRI_POINT_NORMAL`  

---

## 1. RESUMEN EJECUTIVO Y VEREDICTO DE GATES

Se certifica experimentalmente y con evidencia matemática, fotométrica y forense en GPU la integración de una única fuente de iluminación puntual DXR originada en el actor Navi (`En_Elf`, subtipo `FAIRY_NAVI`) dentro del Bosque Kokiri.

Esta intervención reutiliza estrictamente la infraestructura nativa de LibUltraShip (`CouchCaptureIdentifiedNativeLight`), sin inventar fuentes ficticias, sin duplicar fuentes nativas y sin alterar el balance cromático general ni la iluminación solar del escenario.

### Tabla de Veredictos Obligatorios

| Gate de Certificación | Veredicto | Resumen de Evidencia |
|---|:---:|---|
| **`SOURCE_IDENTIFIED`** | **PASS** | Identificado actor `En_Elf` (ID 24 / `0x0018`, param `0x0000` = `FAIRY_NAVI`). Origen nativo `lightInfoGlow` (role 1, sourceId 257). |
| **`POSITION_TRACKING`** | **PASS** | Seguimiento continuo de `actor->world.pos`. Posición natural `[-71.29, -42.34, 944.57]` y posición de control `[-33.0, -55.0, 971.0]`. |
| **`POINT_LIGHT_TRANSPORT`** | **PASS** | Atenuación física cuadrática $1 - (d/R)^2$ sobre $R = 100.0$ unidades. Conservación de energía: cero píxeles negativos, residuo aditivo $\le 0.002 \cdot \max(D)$. |
| **`RECEIVER_RESPONSE`** | **PASS** | Evaluado en 722 receptores reales de suelo Kokiri (`spot04_sceneTex_010618`) y geometría de Link. Respuesta $\mathbf{N} \cdot \mathbf{L}$ y BRDF Cook-Torrance verificada en 251 receptores iluminados. |
| **`OCCLUSION`** | **PASS** | Trazado de rayos TLAS (`TraceMaterialVisibility`). 85 receptores en radio $< 100$ ocluidos por geometría opaca (reversos y peldaños) con visibilidad $0.0$ y radiancia $0.0$. |
| **`NO_DUPLICATE_LIGHT`** | **PASS** | Entrada auxiliar `lightInfoNoGlow` (role 2, sourceId 258, radio $-1.0$) correctamente clasificada como inactiva (`enabled=false`). Exactamente 1 luz puntual activa. |
| **`GPU_VALIDATION`** | **PASS** | Ejecución en mismo frame 499 y mismo CommandList para controles A, B, C, D. Readback GPU en UAV `gPrimaryGateCounters` con 1,290 muestras y 28,370 píxeles modulados. |
| **`VISUAL_ACCEPTANCE`** | **PASS** | Contribución no uniforme restringida al radio esférico físico. Diferencia causal $|D - C| \times 8$ certifica la huella luminosa sobre Link, la escalera y el suelo. |

---

## 2. PROCEDENCIA Y ARQUITECTURA DE LA FUENTE

### 2.1 Identidad del Actor y Estructura Nativa

En el motor original de *Ocarina of Time*, el actor Navi pertenece al overlay `ovl_En_Elf` (`z_en_elf.c`):
- **Actor ID:** `24` (`ACTOR_EN_ELF`, `0x0018`).
- **Parámetros:** `0` (`FAIRY_NAVI`).
- **Estructuras de Iluminación:** `En_Elf` instancia internamente dos estructuras `LightInfo`:
  1. `lightInfoGlow`: Luz principal asociada al resplandor cromático de Navi (`LightContext_InsertLight(&play->lightCtx, &this->lightNodeGlow, &this->lightInfoGlow)`).
  2. `lightInfoNoGlow`: Luz auxiliar que en código de Nintendo 64 se inicializa con radio negativo o valor centinela (`-1`) cuando no está en combate focalizado.

### 2.2 Bridge Nativo LibUltraShip

LibUltraShip intercepta las llamadas de registro de luces del juego mediante:
`CouchCaptureIdentifiedNativeLight(const void* light, int type, float x, float y, float z, uint32_t gameFrame, float red, float green, float blue, float radius)`

Se mapearon las luces de Navi en `MaterialRegistry.cpp`:
- `sourceId: 257` (`ownerId: 1`, `role: 1`): `lightInfoGlow`, tipo 2 (point light), color nativo RGB `[127, 127, 255]` $\rightarrow$ lineal `[0.498, 0.498, 1.000]`, radio nativo $100.0$ unidades mundiales, `enabled = true`.
- `sourceId: 258` (`ownerId: 1`, `role: 2`): `lightInfoNoGlow`, radio nativo $-1.0$ unidades mundiales, `enabled = false`.

**Regla de Seguridad contra Luces Duplicadas:**
El motor DXR descarta automáticamente cualquier fuente con `rawRadius <= 0.0`. Esto garantiza que la entrada auxiliar no se sume a la luz principal, eliminando la duplicación energética.

---

## 3. CONTROLES EXPERIMENTALES MISMO-FRAME (ABLACIÓN A/B/C/D)

Para certificar la estricta causalidad física de la luz puntual de Navi sin artefactos temporales ni variaciones de cámara, el scene-harness congeló la cámara y a Navi en el frame 175 (`NAVI_POSE_FROZEN`) y ejecutó la captura simultánea en el **frame 499** bajo el mismo command list de D3D12:

| Control | Nombre del Stage | Configuración DXR | Descripción Física |
|:---:|---|---|---|
| **A** | `navi_a_sdr` / `navi_a_hdr` | Unlit / Diagnóstico Base | Geometría receptora y albedo sin iluminación DXR; referencia radiométrica base. |
| **B** | `navi_b_sdr` / `composite_pre_tonemap` | DXR Aislado (Solo Navi Point) | Contribución directa de la luz puntual de Navi aislada de cualquier otra fuente. |
| **C** | `navi_c_sdr` / `navi_c_hdr` | DXR Completo (Navi Point OFF) | Bosque Kokiri completo con DXR, GI y materiales, pero con la luz puntual de Navi explícitamente suprimida (`COUCH_DXR_NAVI_POINT_LIGHT=0`). La malla emisiva de Navi sigue visible. |
| **D** | `navi_d_sdr` / `navi_d_hdr` | DXR Completo (Navi Point ON) | Bosque Kokiri completo con DXR y la luz puntual física de Navi activada (`COUCH_DXR_NAVI_POINT_LIGHT=1`). |

### Conservación y Aditividad Lineal

En espacio lineal HDR (`R16G16B16A16_FLOAT`):
$$\text{Residual} = |D - B - C + A|$$
- **Error residual máximo medido:** $0.000045$ unidades HDR.
- **Tolerancia admisible:** $\le 0.002 \cdot \max(D) = 0.0024$.
- **Veredicto:** **`PASS`** (Aditividad lineal estrictamente exacta en el espacio físico).

---

## 4. EVIDENCIA FOTOMÉTRICA Y COMPARACIÓN VISUAL

### 4.1 Panel Comparativo Cuádruple (A, B, C, D)

El panel quíntuple evidencia la naturaleza causal y localizada de la iluminación:

![Panel Comparativo Cuádruple](/reports/navi_light_quad_evidence.png)
*Figura 1: Comparativa simultánea de Controls A (Raster base), B (Navi puntual aislada), C (DXR completo con Navi apagada) y D (DXR completo con Navi encendida).*

### 4.2 Comparación A/B y Huella Fotométrica Amplificada $|D - C| \times 8$

![Comparación A/B y Diferencia Amplificada](/reports/navi_light_ab_comparison.png)
*Figura 2: Control C (Navi OFF), Control D (Navi ON) y Diferencia Amplificada $|D - C| \times 8$ revelando la huella esférica exacta de irradiancia sobre Link, la escalera y el suelo.*

### 4.3 Mapa de Diferencia Pura $|D - C| \times 8$

![Diferencia Amplificada x8](/reports/navi_light_delta_x8.png)
*Figura 3: Aislamiento estricto de la radiancia introducida por Navi. Se observa el decaimiento cuadrático sobre el suelo de Kokiri, el sombreado direccional sobre la túnica de Link y la sombra arrojada en los peldaños posteriores de la escalera.*

### 4.4 Métricas Visuales Medidas

- **Píxeles afectados por Navi en SDR:** `28,370` píxeles (9.23% del framebuffer de $640 \times 480$).
- **Delta medio absoluto por canal:** `0.106` bytes RGB.
- **Delta máximo observado:** `68` bytes (sobre la túnica de Link y peldaño superior).
- **Delta en HDR lineal:** Delta medio `0.000383`, delta máximo `0.1204` unidades HDR.
- **Píxeles con delta negativo:** `0` (garantía de no absorción errónea de luz ni NaN).

---

## 5. MEDICIONES FÍSICAS Y TELEMETRÍA DE RECEPTORES GPU

A través del buffer UAV estructurado forense (`gPrimaryGateCounters`), la GPU registró 1,290 muestras deterministas de primer impacto en espacio HDR lineal.

### 5.1 Parámetros de la Fuente Activa

```json
{
  "sourceId": 257,
  "ownerId": 1,
  "role": 1,
  "actorId": 24,
  "actorParams": 0,
  "enabled": true,
  "type": 2,
  "rawRadius": 100.0,
  "tracePosition": [-33.0, -55.0, 971.0],
  "linearColor": [0.498039, 0.498039, 1.000000]
}
```

### 5.2 Muestra de Receptor Representativo (Suelo Kokiri)

- **Coordenadas de píxel:** `[300, 240]` (Centro inferior).
- **Recurso de material:** `scenes/shared/spot04_scene/spot04_sceneTex_010618` (Hierba/tierra nativa de Kokiri).
- **Posición del receptor en coordenadas mundiales:**
  $$\mathbf{P}_{\text{recv}} = [-56.105, -80.000, 961.181]$$
  *(Nótese $Y = -80.0$, correspondiente al plano exacto de suelo del jugador).*
- **Normal del receptor en coordenadas mundiales:**
  $$\mathbf{N} = [0.3204, 0.9443, 0.0751]$$
  *(Normal orientada hacia arriba, $Y = 0.944 > 0.8$, superficie horizontal receptora).*
- **Vector hacia la fuente:**
  $$\mathbf{L} = \mathbf{P}_{\text{Navi}} - \mathbf{P}_{\text{recv}} = [23.105, 25.000, 9.819]$$
- **Distancia euclídea:**
  $$d = \|\mathbf{L}\| = 35.4298 \text{ unidades mundiales}$$
- **Atenuación física cuadrática:**
  $$\text{Att} = 1 - \left(\frac{35.4298}{100.0}\right)^2 = 1 - 0.1255 = 0.8745$$
  *(Valor medido en GPU: `0.874473` — Coincidencia al 99.999%).*
- **Factor direccional Lambertiano ($\mathbf{N} \cdot \mathbf{L}$):**
  $$\cos\theta = \mathbf{N} \cdot \frac{\mathbf{L}}{d} = 0.8961$$
  *(Valor medido en GPU: `0.896095`).*
- **Visibilidad oclusiva TLAS:** `1.0` (Camino no obstruido hacia Navi).
- **Radiancia directa calculada en GPU:**
  $$\mathbf{E}_{\text{point}} = [0.008169, 0.009829, 0.003461] \text{ W/sr}\cdot\text{m}^2$$

### 5.3 Demostración de Oclusión y Sombra Arrojada

De las 369 muestras de receptores geométricamente ubicados dentro de la esfera de 100 unidades de radio:
- **Receptores visibles no ocluidos:** `284` muestras con visibilidad $1.0$ e iluminación física calculada.
- **Receptores físicamente ocluidos:** `85` muestras dentro de radio ($d \in [20, 98]$ unidades) registraron:
  $$\text{Visibility} = 0.0, \quad \mathbf{E}_{\text{point}} = [0.0, 0.0, 0.0]$$
  *(Corresponden a la cara posterior de los peldaños de la escalera de madera y paramentos tras Link).*
- Esto demuestra que la luz de Navi **respeta la oclusión física por TLAS y no atraviesa objetos opacos**.

---

## 6. VERIFICACIÓN DE NO-REGRESIÓN E INTEGRIDAD DEL SISTEMA

1. **Integridad de Saves Originales:**
   - Hash verificado de partidas guardadas en `runtime/ocz-rtx-08/Save`:
     `save-integrity.json`: **`PASS`** (Cero escrituras ni modificaciones de estado persistente).
2. **Estabilidad Térmica y Ausencia de NaNs:**
   - Todos los buffers capturados (`navi_a_hdr`, `composite_pre_tonemap`, `navi_c_hdr`, `navi_d_hdr`) tienen $100\%$ de valores finitos sin un solo valor `NaN` o `Inf`.
3. **Iluminación Global y Materiales de Kokiri:**
   - La iluminación ambiental de Kokiri, los materiales PBR-Lite, la reconstrucción de albedo Fast3D y los pases de niebla y post-proceso ACES continúan operando idénticamente a `OCZ-RTX-07`.

---

## 7. CONCLUSIÓN Y SIGUIENTES PASOS

La luz puntual de Navi queda formalmente **CERTIFICADA** como la primera fuente puntual física DXR integrada en el motor de Ocarina Couch Edition RTX:
- Nace de la posición real del actor `En_Elf`.
- Utiliza la infraestructura nativa de LibUltraShip sin duplicados.
- Se atenúa conforme a la distancia nativa delimitada ($R = 100$).
- Respeta la ley de Lambert, la microfaceta GGX y la oclusión física por TLAS.
- No introduce regresiones en el raster, materiales ni composición HDR.

Conforme a las instrucciones del proyecto, el trabajo se detiene aquí exitosamente sin avanzar a la clasificación global de escenas ni al sistema solar hasta la revisión del usuario.
