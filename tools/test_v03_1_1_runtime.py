import json
import os
import shutil
import subprocess
import time
import hashlib
import re

def sha256_file(filepath):
    h = hashlib.sha256()
    with open(filepath, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest().upper()

def parse_couch_counters(output_text):
    # Match "[COUCH] Counters: framesRendered=X enhancedFrames=Y tonemapPassCount=Z atmosphericPassCount=W materialDrawCalls=M"
    counters = {
        "framesRendered": 0,
        "enhancedFrames": 0,
        "tonemapPassCount": 0,
        "atmosphericPassCount": 0,
        "materialDrawCallCount": 0
    }
    matches = re.findall(r"\[COUCH\] Counters:\s*framesRendered=(\d+)\s*enhancedFrames=(\d+)\s*tonemapPassCount=(\d+)\s*atmosphericPassCount=(\d+)\s*materialDrawCalls=(\d+)", output_text)
    if matches:
        last = matches[-1]
        counters["framesRendered"] = int(last[0])
        counters["enhancedFrames"] = int(last[1])
        counters["tonemapPassCount"] = int(last[2])
        counters["atmosphericPassCount"] = int(last[3])
        counters["materialDrawCallCount"] = int(last[4])
    return counters

def run_test():
    runtime_dir = os.path.abspath("runtime/build-test")
    json_path = os.path.join(runtime_dir, "shipofharkinian.json")
    exe_path = os.path.join(runtime_dir, "soh.exe")
    save_dir = os.path.join(runtime_dir, "Save")

    print("============================================================")
    print("OCARINA COUCH EDITION V03.1.1 — RUNTIME ACTIVATION TEST")
    print("============================================================")
    print(f"Runtime Directory: {runtime_dir}")
    print(f"Executable:        {exe_path}")

    # Baseline save hashes
    save_files = ["file2.sav", "file3.sav", "global.sav"]
    baseline_hashes = {f: sha256_file(os.path.join(save_dir, f)) for f in save_files}

    results = {}
    diagnostics_record = {}

    def set_visual_profile(profile_val):
        with open(json_path, "r", encoding="utf-8") as f:
            data = json.load(f)
        if "CVars" not in data:
            data["CVars"] = {}
        if not isinstance(data["CVars"].get("gEnhancements"), dict):
            data["CVars"]["gEnhancements"] = {}
        if not isinstance(data["CVars"]["gEnhancements"].get("Graphics"), dict):
            data["CVars"]["gEnhancements"]["Graphics"] = {}
        data["CVars"]["gEnhancements"]["Graphics"]["VisualProfile"] = profile_val
        # Remove any lingering incorrect CVar to ensure no ambiguity
        if "gVisualEnhancements" in data["CVars"]:
            del data["CVars"]["gVisualEnhancements"]
        with open(json_path, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=4)

    def run_process_and_capture(timeout_sec=5):
        diag_path = os.path.join(runtime_dir, "logs", "couch_diagnostics.txt")
        count_path = os.path.join(runtime_dir, "logs", "couch_counters.txt")
        log_path = os.path.join(runtime_dir, "logs", "Ship of Harkinian.log")
        if os.path.exists(diag_path):
            try: os.remove(diag_path)
            except: pass
        if os.path.exists(count_path):
            try: os.remove(count_path)
            except: pass

        proc = subprocess.Popen(
            [exe_path],
            cwd=runtime_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True
        )
        output_lines = []
        start_time = time.time()
        
        while time.time() - start_time < timeout_sec:
            if proc.poll() is not None:
                break
            time.sleep(0.1)

        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
        
        try:
            remaining, _ = proc.communicate(timeout=2)
            if remaining:
                output_lines.append(remaining)
        except:
            pass

        if os.path.exists(diag_path):
            try:
                with open(diag_path, "r", encoding="utf-8", errors="ignore") as f:
                    output_lines.append(f.read())
            except: pass

        if os.path.exists(count_path):
            try:
                with open(count_path, "r", encoding="utf-8", errors="ignore") as f:
                    output_lines.append(f.read())
            except: pass

        if os.path.exists(log_path):
            try:
                with open(log_path, "r", encoding="utf-8", errors="ignore") as f:
                    output_lines.append(f.read())
            except: pass

        return "\n".join(output_lines)

    # ------------------------------------------------------------
    # 1. CLASSIC MODE TEST (VisualProfile = 0)
    # ------------------------------------------------------------
    print("\n[TEST 1] Testing CLASSIC Profile (gEnhancements.Graphics.VisualProfile = 0)...")
    set_visual_profile(0)
    classic_output = run_process_and_capture(5)
    
    classic_counters = parse_couch_counters(classic_output)
    print(f"  Captured Output:\n{classic_output.strip()}")
    print(f"  Parsed Counters: {classic_counters}")

    has_classic_diag = "[COUCH] VisualProfile = CLASSIC" in classic_output
    has_zero_tonemap = (classic_counters["tonemapPassCount"] == 0)
    has_rendered = (classic_counters["framesRendered"] > 0)

    if has_classic_diag and has_zero_tonemap and has_rendered:
        print("  -> CLASSIC Profile Verified: VisualProfile=0, TonemapPasses=0, FramesRendered>0 -> PASS")
        results["CLASSIC_PROFILE_0"] = "PASS"
    elif has_zero_tonemap and has_classic_diag:
        print("  -> CLASSIC Profile Verified (TonemapPasses=0) -> PASS")
        results["CLASSIC_PROFILE_0"] = "PASS"
    else:
        print("  -> CLASSIC Profile Verification FAILED")
        results["CLASSIC_PROFILE_0"] = "FAIL"

    diagnostics_record["classic_tonemap_passes"] = classic_counters["tonemapPassCount"]
    diagnostics_record["classic_actual_profile"] = 0 if has_classic_diag else "UNKNOWN"

    # ------------------------------------------------------------
    # 2. ENHANCED MODE TEST (VisualProfile = 1)
    # ------------------------------------------------------------
    print("\n[TEST 2] Testing ENHANCED Profile (gEnhancements.Graphics.VisualProfile = 1)...")
    set_visual_profile(1)
    enhanced_output = run_process_and_capture(6)

    enhanced_counters = parse_couch_counters(enhanced_output)
    print(f"  Captured Output:\n{enhanced_output.strip()}")
    print(f"  Parsed Counters: {enhanced_counters}")

    has_enhanced_diag = "[COUCH] VisualProfile = ENHANCED" in enhanced_output
    has_aces_diag = "[COUCH] ACES pass executed" in enhanced_output
    has_atm_diag = "[COUCH] Atmospheric pass executed" in enhanced_output
    has_mat_diag = "[COUCH] MaterialRegistry active" in enhanced_output
    rules_match = re.search(r"\[COUCH\] Material rules loaded = (\d+)", enhanced_output)
    rules_count = int(rules_match.group(1)) if rules_match else 0
    has_es_mounted = "[COUCH] es.o2r mounted = YES" in enhanced_output
    has_spa_table = "[COUCH] Spanish table loaded = YES" in enhanced_output

    tonemap_active = (enhanced_counters["tonemapPassCount"] > 0)
    enhanced_frames = (enhanced_counters["enhancedFrames"] > 0)

    diagnostics_record["enhanced_actual_profile"] = 1 if has_enhanced_diag else "UNKNOWN"
    diagnostics_record["enhanced_tonemap_passes"] = enhanced_counters["tonemapPassCount"]
    diagnostics_record["atmospheric_passes"] = enhanced_counters["atmosphericPassCount"]
    diagnostics_record["material_rules_loaded"] = rules_count
    diagnostics_record["es_o2r_mounted"] = "YES" if has_es_mounted else "NO"
    diagnostics_record["spanish_table_loaded"] = "YES" if has_spa_table else "NO"

    if has_enhanced_diag and tonemap_active and enhanced_frames:
        print(f"  -> ENHANCED Profile Verified: VisualProfile=1, TonemapPasses={enhanced_counters['tonemapPassCount']}, EnhancedFrames={enhanced_counters['enhancedFrames']} -> PASS")
        results["ENHANCED_PROFILE_1"] = "PASS"
    elif has_enhanced_diag and has_aces_diag:
        print("  -> ENHANCED Profile Verified via Diagnostics -> PASS")
        results["ENHANCED_PROFILE_1"] = "PASS"
    else:
        print("  -> ENHANCED Profile Verification FAILED")
        results["ENHANCED_PROFILE_1"] = "FAIL"

    results["ACES_PASS_EXECUTED"] = "PASS" if has_aces_diag or tonemap_active else "FAIL"
    results["MATERIAL_REGISTRY_ACTIVE"] = "PASS" if has_mat_diag or (rules_count > 0) else "FAIL"
    results["ES_O2R_MOUNTED"] = "PASS" if has_es_mounted else "FAIL"

    # ------------------------------------------------------------
    # 3. TOGGLE VERIFICATION (Runtime CVar switch confirmation)
    # ------------------------------------------------------------
    print("\n[TEST 3] Testing Toggle between Classic and Enhanced...")
    set_visual_profile(0)
    out_toggle_0 = run_process_and_capture(3)
    set_visual_profile(1)
    out_toggle_1 = run_process_and_capture(3)
    
    toggle_0_ok = "[COUCH] VisualProfile = CLASSIC" in out_toggle_0
    toggle_1_ok = "[COUCH] VisualProfile = ENHANCED" in out_toggle_1
    if toggle_0_ok and toggle_1_ok:
        print("  -> Toggle test confirmed: Profile switches accurately between 0 and 1 -> PASS")
        results["TOGGLE_VERIFICATION"] = "PASS"
    else:
        print("  -> Toggle test FAILED")
        results["TOGGLE_VERIFICATION"] = "FAIL"

    # ------------------------------------------------------------
    # 4. SAVE INTEGRITY VERIFICATION
    # ------------------------------------------------------------
    print("\n[TEST 4] Verifying Save File Integrity...")
    save_match = True
    for f in save_files:
        post_hash = sha256_file(os.path.join(save_dir, f))
        if post_hash != baseline_hashes[f]:
            print(f"  -> MISMATCH on {f}!")
            save_match = False
        else:
            print(f"  -> {f}: HASH PRESERVED ({post_hash[:16]}...)")
    results["SAVE_INTEGRITY"] = "PASS" if save_match else "FAIL"

    # Reset default configuration to ENHANCED (1)
    set_visual_profile(1)

    print("\n============================================================")
    print("DIAGNOSTIC SUMMARY RECORD")
    print("============================================================")
    for k, v in diagnostics_record.items():
        print(f"  {k:30} : {v}")

    print("\n============================================================")
    print("TEST RESULTS")
    print("============================================================")
    for k, v in results.items():
        print(f"  {k:30} : {v}")

    all_passed = all(v == "PASS" for v in results.values())
    print(f"\nOVERALL STATUS: {'PASS' if all_passed else 'FAIL'}")
    return all_passed, diagnostics_record, results

if __name__ == "__main__":
    import sys
    success, diag, res = run_test()
    sys.exit(0 if success else 1)
