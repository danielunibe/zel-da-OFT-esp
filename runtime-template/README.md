# RUNTIME TEMPLATE — OCARINA OF TIME PC: COUCH EDITION

Esta carpeta proporciona la estructura modelo para desplegar la versión jugable de *Couch Edition*.

---

## Estructura Requerida

Para ejecutar el juego se requieren los siguientes componentes en el mismo directorio:

```text
runtime/
├── soh.exe                   (Binario ejecutable compilado con parches de Couch Edition)
├── soh.o2r                   (Recursos base de la interfaz de usuario de SoH)
├── oot.o2r                   (ASSETS DE LA ROM BASE - APORTADO LEGALMENTE POR EL USUARIO)
├── gamecontrollerdb.txt      (Mapeo de mandos y controladores SDL)
├── shipofharkinian.json      (Configuración con español y Enhanced activados)
└── mods/
    └── es.o2r                (Localización en español ES-419 y glifos extendidos)
```

---

## Instrucciones de Despliegue

1. **Compilar o descargar binarios:**
   - Compile `soh.exe` y `soh.o2r` siguiendo `docs/RECOVERY_FROM_ZERO.md` o descárguelos desde la sección de *Releases* del repositorio.
2. **Generar `oot.o2r`:**
   - Extraiga los activos del juego base a partir de su ROM legal utilizando el extractor de Shipwright y coloque `oot.o2r` en la raíz junto a `soh.exe`.
3. **Copiar recursos de configuración y mods:**
   - Copie el contenido de `config-example/` a la raíz de ejecución.
   - Asegúrese de que la carpeta `mods/` contenga `es.o2r`.
4. **Ejecución:**
   - Inicie `soh.exe`.
   - Disfrute de la experiencia Couch Edition con diálogos en español auténticos y canalización visual Enhanced.
