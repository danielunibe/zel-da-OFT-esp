import os
import shutil
import subprocess

def test_clean_apply():
    dev_root = os.path.abspath(".")
    patches_dir = os.path.join(dev_root, "patches")
    sw_patch = os.path.join(patches_dir, "shipwright_couch_edition_v03_1_1.patch")
    lus_patch = os.path.join(patches_dir, "libultraship_couch_edition_v03_1_1.patch")
    
    test_dir = os.path.join(dev_root, "test_worktrees")
    if os.path.exists(test_dir):
        shutil.rmtree(test_dir, ignore_errors=True)
    os.makedirs(test_dir, exist_ok=True)
    
    sw_repo = os.path.join(dev_root, "source", "shipwright")
    lus_repo = os.path.join(dev_root, "source", "shipwright", "libultraship")
    
    sw_clean_dir = os.path.join(test_dir, "sw_clean")
    lus_clean_dir = os.path.join(test_dir, "lus_clean")
    
    print("[1] Creating clean worktrees...")
    subprocess.run(["git", "worktree", "prune"], cwd=sw_repo, check=True)
    subprocess.run(["git", "worktree", "add", "--detach", sw_clean_dir, "4aaad850bd5540cd77c2d83f3ad348d3b38605b2"], cwd=sw_repo, check=True)
    
    subprocess.run(["git", "worktree", "prune"], cwd=lus_repo, check=True)
    subprocess.run(["git", "worktree", "add", "--detach", lus_clean_dir, "17a0b7939bd05f5e617cef89457ca43774fc9a9f"], cwd=lus_repo, check=True)
    
    # 1. Shipwright checks
    print("\n--- Shipwright Patch Checks ---")
    print("Running git apply --numstat...")
    res_sw_numstat = subprocess.run(["git", "apply", "--numstat", sw_patch], cwd=sw_clean_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    print(res_sw_numstat.stdout[:500])
    assert res_sw_numstat.returncode == 0, f"SW numstat failed: {res_sw_numstat.stderr}"

    print("Running git apply --check...")
    res_sw_check = subprocess.run(["git", "apply", "--check", sw_patch], cwd=sw_clean_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    assert res_sw_check.returncode == 0, f"SW check failed: {res_sw_check.stderr}"
    print("  -> git apply --check passed successfully!")

    print("Running git apply...")
    res_sw_apply = subprocess.run(["git", "apply", sw_patch], cwd=sw_clean_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    assert res_sw_apply.returncode == 0, f"SW apply failed: {res_sw_apply.stderr}"
    print("  -> git apply applied successfully!")

    # 2. libultraship checks
    print("\n--- libultraship Patch Checks ---")
    print("Running git apply --numstat...")
    res_lus_numstat = subprocess.run(["git", "apply", "--numstat", lus_patch], cwd=lus_clean_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    print(res_lus_numstat.stdout[:500])
    assert res_lus_numstat.returncode == 0, f"LUS numstat failed: {res_lus_numstat.stderr}"

    print("Running git apply --check...")
    res_lus_check = subprocess.run(["git", "apply", "--check", lus_patch], cwd=lus_clean_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    assert res_lus_check.returncode == 0, f"LUS check failed: {res_lus_check.stderr}"
    print("  -> git apply --check passed successfully!")

    print("Running git apply...")
    res_lus_apply = subprocess.run(["git", "apply", lus_patch], cwd=lus_clean_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    assert res_lus_apply.returncode == 0, f"LUS apply failed: {res_lus_apply.stderr}"
    print("  -> git apply applied successfully!")

    # Cleanup worktrees
    subprocess.run(["git", "worktree", "remove", "--force", sw_clean_dir], cwd=sw_repo)
    subprocess.run(["git", "worktree", "remove", "--force", lus_clean_dir], cwd=lus_repo)
    shutil.rmtree(test_dir, ignore_errors=True)
    print("\nClean apply tests PASSED completely.")

if __name__ == "__main__":
    test_clean_apply()
