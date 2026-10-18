import unreal
import json

doc = {
    "take_high_res_screenshot": str(unreal.AutomationLibrary.take_high_res_screenshot.__doc__),
    "take_automation_screenshot_at_camera": str(unreal.AutomationLibrary.take_automation_screenshot_at_camera.__doc__),
    "set_level_viewport_camera_info": str(unreal.EditorLevelLibrary.set_level_viewport_camera_info.__doc__),
    "export_render_target": str(unreal.RenderingLibrary.export_render_target.__doc__)
}

out_file = r"C:\Users\danie\Desktop\Zeldacouch\zel-da-OFT-esp-main (3)\zel-da-OFT-esp-main\tools\unreal_lab\unreal_python\doc_results.json"
with open(out_file, "w", encoding="utf-8") as f:
    json.dump(doc, f, indent=2)
