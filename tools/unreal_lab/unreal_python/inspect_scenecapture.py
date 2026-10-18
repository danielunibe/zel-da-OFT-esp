import unreal
import json

doc = {
    "create_render_target2d": str(unreal.RenderingLibrary.create_render_target2d.__doc__),
    "SceneCaptureSource": dir(unreal.SceneCaptureSource) if hasattr(unreal, "SceneCaptureSource") else [],
    "RenderTargetFormat": dir(unreal.RenderTargetFormat) if hasattr(unreal, "RenderTargetFormat") else [],
    "SceneCaptureComponent2D": [m for m in dir(unreal.SceneCaptureComponent2D) if "capture" in m.lower() or "texture" in m.lower() or "source" in m.lower()]
}

out_file = r"C:\Users\danie\Desktop\Zeldacouch\zel-da-OFT-esp-main (3)\zel-da-OFT-esp-main\tools\unreal_lab\unreal_python\scenecapture_doc.json"
with open(out_file, "w", encoding="utf-8") as f:
    json.dump(doc, f, indent=2)

print("Exported SceneCapture doc successfully.")
