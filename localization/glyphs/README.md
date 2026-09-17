# G01 — Dynamic Xbox Glyphs / Input UI Readiness Audit

Estado: auditoría de arquitectura y diseño, sin implementación.

## Alcance

Se auditó el checkout canónico `C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV`, con fuente Shipwright en `source\shipwright`. Se revisaron el renderer de mensajes, el loader de iconos de fuente, Ocarina, HUD, Input Viewer/SoH UI, y las APIs de mappings de Ship. El inventario de localización reporta 137 mensajes con prompts.

## Regla de no mutación

G01 escribe únicamente dentro de `glyph_workspace`. No se modificaron fuente, assets, runtime, mappings, configuración, instalación viva ni los workspaces de visual, backup, audio o localization.

## Resultado ejecutivo

La base actual es suficiente para un piloto: existe un renderer central de mensajes y Ship expone mappings por botón, stick/dirección, tipo de dispositivo, nombre físico y edición en caliente. No existe todavía un resolver semántico que convierta esos mappings en una familia de glyphs para mensajes, HUD, Ocarina y SoH UI. El piloto recomendado es un prompt de mensaje de acción Z, con fallback N64 intacto.

Ver `reports/G01_FINAL_REPORT.md` para el cierre y los límites de evidencia.

