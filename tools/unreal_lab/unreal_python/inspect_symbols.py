import unreal
import json
import os

all_attrs = dir(unreal)
usd_attrs = [a for a in all_attrs if "usd" in a.lower()]
level_attrs = [a for a in all_attrs if "level" in a.lower() and "lib" in a.lower()]

res = {
    "total_unreal_symbols": len(all_attrs),
    "usd_symbols": usd_attrs,
    "level_symbols": level_attrs
}

out_path = "C:/Users/danie/Desktop/Zeldacouch/zel-da-OFT-esp-main (3)/zel-da-OFT-esp-main/tools/unreal_lab/unreal_python/ue_symbols.json"
with open(out_path, "w", encoding="utf-8") as f:
    json.dump(res, f, indent=2)

print("Exported symbols successfully.")
