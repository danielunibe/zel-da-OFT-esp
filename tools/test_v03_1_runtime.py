import json
import os
import shutil
import subprocess
import time
import hashlib

def sha256_file(filepath):
    h = hashlib.sha256()
    with open(filepath, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest().upper()

def run_test():
    runtime_dir = os.path.abspath("runtime/build-test")
    json_path = os.path.join(runtime_dir, "shipofharkinian.json")
    exe_path = os.path.join(runtime_dir, "soh.exe")
    save_dir = os.path.join(runtime_dir, "Save")

    print("=== OCARINA COUCH EDITION V03.1 RUNTIME VALIDATION ===")
    print(f"Runtime Directory: {runtime_dir}")
    print(f"Executable: {exe_path}")

    # Baseline save hashes
    save_files = ["file2.sav", "file3.sav", "global.sav"]
    baseline_hashes = {f: sha256_file(os.path.join(save_dir, f)) for f in save_files}

    results = {}

    def set_visual_profile(val):
        with open(json_path, "r", encoding="utf-8") as f:
            data = json.load(f)
        if "CVars" not in data:
            data["CVars"] = {}
        if not isinstance(data["CVars"].get("gEnhancements"), dict):
            data["CVars"]["gEnhancements"] = {}
        if not isinstance(data["CVars"]["gEnhancements"].get("Graphics"), dict):
            data["CVars"]["gEnhancements"]["Graphics"] = {}
        data["CVars"]["gEnhancements"]["Graphics"]["VisualProfile"] = val
        if "gVisualEnhancements" in data["CVars"]:
            del data["CVars"]["gVisualEnhancements"]
        with open(json_path, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=4)

    # 1. CLASSIC MODE BOOT TEST
    print("\n[TEST 1] Testing Classic Mode Boot (VisualProfile = 0)...")
    set_visual_profile(0)
    proc = subprocess.Popen([exe_path], cwd=runtime_dir)
    time.sleep(5)
    poll_res = proc.poll()
    if poll_res is None:
        print(f"  -> Classic Mode Boot: ALIVE (PID: {proc.pid})")
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
        results["ClassicBoot"] = "PASS"
    else:
        print(f"  -> Classic Mode Boot: EXITED PREMATURELY (Code: {poll_res})")
        results["ClassicBoot"] = "FAIL"

    # 2. ENHANCED MODE BOOT TEST
    print("\n[TEST 2] Testing Enhanced Mode Boot (VisualProfile = 1)...")
    set_visual_profile(1)
    proc = subprocess.Popen([exe_path], cwd=runtime_dir)
    time.sleep(5)
    poll_res = proc.poll()
    if poll_res is None:
        print(f"  -> Enhanced Mode Boot: ALIVE (PID: {proc.pid})")
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
        results["EnhancedBoot"] = "PASS"
    else:
        print(f"  -> Enhanced Mode Boot: EXITED PREMATURELY (Code: {poll_res})")
        results["EnhancedBoot"] = "FAIL"

    # 3. TOGGLE STRESS TEST
    print("\n[TEST 3] Testing Toggle Stress (3 consecutive alternating boots)...")
    toggle_pass = True
    for i in range(1, 4):
        mode = i % 2
        set_visual_profile(mode)
        p = subprocess.Popen([exe_path], cwd=runtime_dir)
        time.sleep(3)
        if p.poll() is not None:
            print(f"  -> Cycle {i} (Mode {mode}) failed! Exited early.")
            toggle_pass = False
            break
        p.terminate()
        try:
            p.wait(timeout=3)
        except subprocess.TimeoutExpired:
            p.kill()
    if toggle_pass:
        print("  -> Toggle Stress Test: ALL 3 CYCLES PASSED CLEANLY")
        results["ToggleStress"] = "PASS"
    else:
        results["ToggleStress"] = "FAIL"

    # 4. NEGATIVE TEST: Missing es.o2r fallback
    print("\n[TEST 4] Testing Negative Fallback (Missing es.o2r)...")
    es_path = os.path.join(runtime_dir, "es.o2r")
    es_bak = os.path.join(runtime_dir, "es.o2r.tempbak")
    if os.path.exists(es_path):
        shutil.move(es_path, es_bak)
    try:
        p = subprocess.Popen([exe_path], cwd=runtime_dir)
        time.sleep(4)
        if p.poll() is None:
            print(f"  -> Negative Fallback Test: ALIVE (English fallback succeeded)")
            p.terminate()
            try:
                p.wait(timeout=3)
            except subprocess.TimeoutExpired:
                p.kill()
            results["NegativeFallback_MissingArchive"] = "PASS"
        else:
            print(f"  -> Negative Fallback Test: FAILED (Code: {p.poll()})")
            results["NegativeFallback_MissingArchive"] = "FAIL"
    finally:
        if os.path.exists(es_bak):
            shutil.move(es_bak, es_path)

    # 5. SAVE INTEGRITY VERIFICATION
    print("\n[TEST 5] Verifying Save File Integrity...")
    save_match = True
    for f in save_files:
        post_hash = sha256_file(os.path.join(save_dir, f))
        if post_hash != baseline_hashes[f]:
            print(f"  -> MISMATCH on {f}!")
            save_match = False
        else:
            print(f"  -> {f}: HASH PRESERVED ({post_hash[:16]}...)")
    results["SaveIntegrity"] = "PASS" if save_match else "FAIL"

    # Reset default to Enhanced Mode = 1
    set_visual_profile(1)

    print("\n=== SUMMARY RESULTS ===")
    for k, v in results.items():
        print(f"  {k:35} : {v}")

    all_passed = all(v == "PASS" for v in results.values())
    print(f"\nOVERALL RESULT: {'ALL PASS' if all_passed else 'SOME FAILURES'}")
    return all_passed

if __name__ == "__main__":
    import sys
    success = run_test()
    sys.exit(0 if success else 1)
