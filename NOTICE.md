# AVISO DE DERECHOS DE AUTOR Y ATRIBUCIONES (NOTICE)

## Ocarina of Time PC — Couch Edition

Este repositorio contiene código fuente original, modificaciones, herramientas, conjuntos de datos de materiales, perfiles visuales y recursos de localización al español desarrollados para la adaptación de *Ocarina of Time PC: Couch Edition*.

---

### 1. Política Estricta de Propiedad Intelectual y ROMs
- **Nintendo Co., Ltd.:** *The Legend of Zelda: Ocarina of Time* es una marca registrada y propiedad intelectual de Nintendo Co., Ltd. Todos los derechos reservados.
- **Sin redistribución de ROMs ni Assets propietarios:** Este repositorio **NO** contiene, aloja, distribuye ni enlaza a ningún archivo de ROM de Nintendo 64 (`.z64`, `.n64`, `.v64`), volcados de cartuchos originales, ni archivos de extracción propietarios completos (`oot.o2r`).
- **Requisito del Usuario:** Para compilar y ejecutar el software resultante, cada usuario final debe proporcionar legalmente su propia copia original del juego y extraer los activos utilizando las herramientas oficiales de decompilación y extracción de Shipwright.

---

### 2. Atribuciones de Código y Proyectos Upstream
Este proyecto se construye sobre el trabajo de las comunidades de decompilación y preservación:

- **Shipwright / Ship of Harkinian:**
  Desarrollado por Harbour Masters y colaboradores de la comunidad de decompilación de OoT.
  Repositorio oficial: https://github.com/HarbourMasters/Shipwright
  Commit de referencia fijado: `4aaad850bd5540cd77c2d83f3ad348d3b38605b2` (Versión 9.1.1 Bravo)

- **LibUltraShip (LUS):**
  Copyright (c) 2022 kenix3 y colaboradores de LibUltraShip.
  Licencia: MIT License.
  Repositorio oficial: https://github.com/HarbourMasters/libultraship
  Commit de referencia fijado: `17a0b7939bd05f5e617cef89457ca43774fc9a9f`

- **ZAPDTR / OTRExporter:**
  Herramientas de extracción y empaquetado de assets de Harbour Masters bajo sus respectivas licencias de código abierto.

---

### 3. Código y Recursos Propios de Couch Edition
Las siguientes características corresponden a desarrollo propio y se distribuyen bajo los términos de la licencia MIT incluida en este repositorio:
- **Enrutamiento de mensajes y localización ES-419:** Generadores semánticos, integración en `z_message_PAL.c` (`sSpaMessageEntryTablePtr`), glifos extendidos y paquete de recursos `es.o2r`.
- **Canalización Visual y PBR:** Perfil visual Enhanced, mapeo de tonos ACES, integración de MaterialRegistry y conjunto de datos `material_intelligence_01` (1.407 reglas compiladas).
- **Herramientas de construcción y control agéntico:** `agent_build_guard.ps1`, suites de pruebas automatizadas de runtime y validación de parches.
