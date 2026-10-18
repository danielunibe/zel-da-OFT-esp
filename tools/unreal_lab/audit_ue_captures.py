"""
Audit UE captures in reports/unreal_lab/captures
Verifies SHA-256 hashes, exact byte sizes, pixel differences between passes,
and inspects the HDR binary file structure.
"""
import os
import hashlib
import json
import struct
from PIL import Image
import numpy as np

CAPTURES_DIR = r"c:\Users\danie\Desktop\Zeldacouch\zel-da-OFT-esp-main (3)\zel-da-OFT-esp-main\reports\unreal_lab\captures"

def compute_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def inspect_hdr(path):
    with open(path, "rb") as f:
        header = b""
        line = f.readline()
        while line and line != b"\n":
            header += line
            line = f.readline()
        res_line = f.readline()
        remaining_data = f.read()

    header_text = header.decode("latin1", errors="replace")
    res_text = res_line.decode("latin1", errors="replace")

    # Parse Radiance RGBE
    is_radiance = "#?RADIANCE" in header_text or "#?RGBE" in header_text
    format_line = [l for l in header_text.splitlines() if "FORMAT=" in l]

    return {
        "file_size": os.path.getsize(path),
        "is_radiance_rgbe": is_radiance,
        "format_tag": format_line[0] if format_line else "UNKNOWN",
        "resolution_line": res_text.strip(),
        "header_preview": header_text[:300],
        "data_bytes": len(remaining_data)
    }

def main():
    files = [f for f in os.listdir(CAPTURES_DIR) if f.endswith(('.png', '.hdr', '.json'))]
    results = {
        "files": {},
        "comparisons": {},
        "identical_pairs": [],
        "different_pairs": []
    }

    images = {}

    for fname in sorted(files):
        fpath = os.path.join(CAPTURES_DIR, fname)
        size = os.path.getsize(fpath)
        sha = compute_sha256(fpath)
        results["files"][fname] = {
            "size": size,
            "sha256": sha
        }
        if fname.endswith('.png'):
            try:
                img = Image.open(fpath)
                arr = np.array(img)
                images[fname] = arr
                results["files"][fname].update({
                    "mode": img.mode,
                    "resolution": list(img.size),
                    "mean_channel_values": [float(v) for v in arr.mean(axis=(0,1))],
                    "min_val": int(arr.min()),
                    "max_val": int(arr.max())
                })
            except Exception as e:
                results["files"][fname]["error"] = str(e)
        elif fname.endswith('.hdr'):
            results["files"][fname].update(inspect_hdr(fpath))

    # Compare all PNG pairs
    png_names = sorted(list(images.keys()))
    for i in range(len(png_names)):
        for j in range(i + 1, len(png_names)):
            n1, n2 = png_names[i], png_names[j]
            arr1, arr2 = images[n1], images[n2]
            sha1 = results["files"][n1]["sha256"]
            sha2 = results["files"][n2]["sha256"]

            if sha1 == sha2:
                results["identical_pairs"].append((n1, n2))
                pair_key = f"{n1} vs {n2}"
                results["comparisons"][pair_key] = {
                    "identical": True,
                    "sha256_match": True,
                    "mae": 0.0,
                    "max_diff": 0
                }
            else:
                if arr1.shape == arr2.shape:
                    diff = np.abs(arr1.astype(np.int32) - arr2.astype(np.int32))
                    mae = float(diff.mean())
                    max_d = int(diff.max())
                    pct_diff_pixels = float((diff > 0).any(axis=-1).mean() * 100.0) if len(diff.shape) == 3 else float((diff > 0).mean() * 100.0)
                else:
                    mae = -1.0
                    max_d = -1
                    pct_diff_pixels = 100.0

                results["different_pairs"].append((n1, n2))
                pair_key = f"{n1} vs {n2}"
                results["comparisons"][pair_key] = {
                    "identical": False,
                    "sha256_match": False,
                    "mae": mae,
                    "max_diff": max_d,
                    "pct_diff_pixels": pct_diff_pixels
                }

    output_path = os.path.join(CAPTURES_DIR, "capture_audit_report.json")
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(results, f, indent=2)

    print("AUDIT SUMMARY:")
    print(f"Total files audited: {len(results['files'])}")
    print(f"Identical pairs: {len(results['identical_pairs'])}")
    for p in results['identical_pairs']:
        print(f"  IDENTICAL: {p[0]} == {p[1]}")
    print(f"Different pairs: {len(results['different_pairs'])}")
    for p in results['different_pairs']:
        comp = results['comparisons'][f"{p[0]} vs {p[1]}"]
        print(f"  DIFF: {p[0]} vs {p[1]} -> MAE={comp['mae']:.2f}, max_diff={comp['max_diff']}, {comp['pct_diff_pixels']:.1f}% pixels differ")

if __name__ == "__main__":
    main()
