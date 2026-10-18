import unreal
import json

data = {
    "AutomationLibrary": dir(unreal.AutomationLibrary),
    "RenderingLibrary": dir(unreal.RenderingLibrary),
    "EditorLevelLibrary": dir(unreal.EditorLevelLibrary)
}

out_file = r"C:\Users\danie\Desktop\Zeldacouch\zel-da-OFT-esp-main (3)\zel-da-OFT-esp-main\tools\unreal_lab\unreal_python\render_methods.json"
with open(out_file, "w", encoding="utf-8") as f:
    json.dump(data, f, indent=2)

print("Exported render methods successfully.")
