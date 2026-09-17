import os
import subprocess

def export_patches():
    dev_root = os.path.abspath(".")
    patches_dir = os.path.join(dev_root, "patches")
    os.makedirs(patches_dir, exist_ok=True)
    
    shipwright_dir = os.path.join(dev_root, "source", "shipwright")
    libultraship_dir = os.path.join(dev_root, "source", "shipwright", "libultraship")
    
    shipwright_patch_path = os.path.join(patches_dir, "shipwright_couch_edition_v03_1_1.patch")
    libultraship_patch_path = os.path.join(patches_dir, "libultraship_couch_edition_v03_1_1.patch")
    
    print("[1] Generating shipwright_couch_edition_v03_1_1.patch...")
    # Exclude libultraship submodule from shipwright patch
    cmd_sw = ["git", "diff", "--binary", "4aaad850bd5540cd77c2d83f3ad348d3b38605b2", "--", ".", ":(exclude)libultraship"]
    res_sw = subprocess.run(cmd_sw, cwd=shipwright_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    
    # Normalize line endings to LF and ensure no BOM
    text_sw = res_sw.stdout.decode("utf-8", errors="replace").replace("\r\n", "\n")
    with open(shipwright_patch_path, "wb") as f:
        f.write(text_sw.encode("utf-8"))
    print(f"  -> Written {len(text_sw)} bytes to {shipwright_patch_path}")
    
    print("[2] Generating libultraship_couch_edition_v03_1_1.patch...")
    cmd_lus = ["git", "diff", "--binary", "17a0b7939bd05f5e617cef89457ca43774fc9a9f", "--", "."]
    res_lus = subprocess.run(cmd_lus, cwd=libultraship_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    
    text_lus = res_lus.stdout.decode("utf-8", errors="replace").replace("\r\n", "\n")
    with open(libultraship_patch_path, "wb") as f:
        f.write(text_lus.encode("utf-8"))
    print(f"  -> Written {len(text_lus)} bytes to {libultraship_patch_path}")

    # Verify BOM absence and LF
    for ppath in [shipwright_patch_path, libultraship_patch_path]:
        with open(ppath, "rb") as f:
            content = f.read()
            assert not content.startswith(b"\xef\xbb\xbf"), f"BOM detected in {ppath}!"
            assert b"\r" not in content, f"CR found in {ppath} (not strict LF)!"
        print(f"  -> Verified UTF-8 no BOM LF: {os.path.basename(ppath)}")

if __name__ == "__main__":
    export_patches()
