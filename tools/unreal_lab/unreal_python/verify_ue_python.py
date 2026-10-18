import unreal
import sys
import json
import os

print("=== UNREAL PYTHON VERIFICATION ===")
print(f"Python version: {sys.version}")
try:
    engine_ver = unreal.SystemLibrary.get_engine_version()
    print(f"Engine version: {engine_ver}")
except Exception as e:
    engine_ver = str(e)
    print(f"Could not get engine version: {e}")

# Check USD classes in unreal module
usd_classes = [attr for attr in dir(unreal) if "usd" in attr.lower() or "stage" in attr.lower()]
print(f"Found {len(usd_classes)} USD/Stage related attributes in unreal module.")

results = {
    "engine_version": engine_ver,
    "python_version": sys.version,
    "has_editor_level_lib": hasattr(unreal, "EditorLevelLibrary"),
    "has_asset_tools": hasattr(unreal, "AssetToolsHelpers"),
    "has_editor_asset_lib": hasattr(unreal, "EditorAssetLibrary"),
    "usd_classes_sample": usd_classes[:20],
    "status": "PASS"
}

output_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "ue_python_check.json"))
with open(output_path, "w", encoding="utf-8") as f:
    json.dump(results, f, indent=2)

print(f"Wrote verification to {output_path}")
print("=== VERIFICATION COMPLETE ===")
