import unreal
import os
import json

print("=== CHECKING UNREAL RENDER APIS ===")

has_auto_lib = hasattr(unreal, "AutomationLibrary")
has_render_lib = hasattr(unreal, "RenderingLibrary")
has_movie_pipeline = hasattr(unreal, "MoviePipelineQueueEngineSubsystem")

print(f"has_auto_lib: {has_auto_lib}")
print(f"has_render_lib: {has_render_lib}")
print(f"has_movie_pipeline: {has_movie_pipeline}")

# Check console command execution
cmd_funcs = [attr for attr in dir(unreal.SystemLibrary) if "cmd" in attr.lower() or "console" in attr.lower()]
print(f"SystemLibrary console functions: {cmd_funcs}")

# Check Ray Tracing / Lumen / Path Tracing cvars
results = {
    "has_automation_library": has_auto_lib,
    "has_rendering_library": has_render_lib,
    "has_movie_pipeline": has_movie_pipeline,
    "console_functions": cmd_funcs
}

out_file = r"C:\Users\danie\Desktop\Zeldacouch\zel-da-OFT-esp-main (3)\zel-da-OFT-esp-main\tools\unreal_lab\unreal_python\render_api_check.json"
with open(out_file, "w", encoding="utf-8") as f:
    json.dump(results, f, indent=2)

print(f"Report written to {out_file}")
