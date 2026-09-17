import os
import shutil
import hashlib

def sha256_file(filepath):
    h = hashlib.sha256()
    with open(filepath, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest().upper()

def create_runtime_package():
    dev_root = os.path.abspath(".")
    build_test_dir = os.path.join(dev_root, "runtime", "build-test")
    pkg_dir = os.path.join(dev_root, "V03_1_1_RUNTIME")
    
    if os.path.exists(pkg_dir):
        shutil.rmtree(pkg_dir)
    os.makedirs(pkg_dir, exist_ok=True)
    
    files_to_copy = [
        "soh.exe",
        "soh.o2r",
        "es.o2r",
        "shipofharkinian.json",
        "gamecontrollerdb.txt"
    ]
    
    print("Creando paquete local V03_1_1_RUNTIME...")
    manifest = {}
    for fname in files_to_copy:
        src = os.path.join(build_test_dir, fname)
        dst = os.path.join(pkg_dir, fname)
        assert os.path.exists(src), f"Archivo no encontrado: {src}"
        shutil.copy2(src, dst)
        h = sha256_file(dst)
        sz = os.path.getsize(dst)
        manifest[fname] = {"sha256": h, "size": sz}
        print(f"  + {fname} ({sz:,} bytes) SHA256: {h}")
        
    # Validacion estricta: oot.o2r NO debe estar presente
    assert not os.path.exists(os.path.join(pkg_dir, "oot.o2r")), "ERROR: oot.o2r no debe incluirse en el paquete!"
    print("  -> Verificado: oot.o2r NO esta incluido en el paquete.")
    
    # Crear archivo de instrucciones de uso en espanol
    instrucciones = """============================================================
OCARINA OF TIME PC - COUCH EDITION V03.1.1
INSTRUCCIONES DE USO E INSTALACION
============================================================

1. REQUISITO PREVIO OBLIGATORIO:
   Debes colocar tu propio archivo 'oot.o2r' (generado a partir de tu
   copia legal de la ROM de Nintendo 64) en esta misma carpeta, junto a:
     - soh.exe
     - soh.o2r
     - es.o2r
     - shipofharkinian.json
     - gamecontrollerdb.txt

2. INICIAR EL JUEGO:
   Ejecuta 'soh.exe'.

3. PERFIL VISUAL CANONICO (MEJORAS GRAFICAS):
   El motor viene configurado por defecto en modo Enhanced (perfil 1).
   Para alternar el perfil visual en cualquier momento:
   - Presiona la tecla [F1] para abrir la barra de menus de Ship of Harkinian.
   - Navega a:
       Settings -> Graphics -> Visual Profile
   - Opciones disponibles:
       * Classic (0): Renderizado identico bit-a-bit al N64 / Shipwright 9.1.1 puro.
       * Enhanced (1): Tonemapping ACES activo, correccion atmosferica y materiales calibrados.
   - La CVar interna canonica en 'shipofharkinian.json' es:
       gEnhancements.Graphics.VisualProfile

4. LOCALIZACION AL ESPANOL (ES-419):
   - El archivo 'es.o2r' contiene la traduccion completa de dialogos y textos del juego al espanol latino.
   - La interfaz de menus de configuracion de Ship of Harkinian (F1) permanece en ingles por diseno upstream, mientras que todos los textos, dialogos y mensajes de la aventura se presentan integramente en espanol.

============================================================
"""
    instr_path = os.path.join(pkg_dir, "INSTRUCCIONES_USO.txt")
    with open(instr_path, "w", encoding="utf-8") as f:
        f.write(instrucciones)
    print(f"  + INSTRUCCIONES_USO.txt creado.")
    
    print("\nPaquete V03_1_1_RUNTIME completado con exito.")

if __name__ == "__main__":
    create_runtime_package()
