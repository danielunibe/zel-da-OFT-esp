"""Package the validated GPU images losslessly with the interactive viewer for offline review."""
import base64
import io
import json
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
state = json.loads((ROOT / "reports/dxr_comparison/suite.status.json").read_text(encoding="utf-8"))
assert state["status"] == "COMPLETE_SCOPED_GPU_COMPARISONS"
portable = {"status": state["status"], "current": None, "items": []}
count = 0
for item in state["items"]:
    record = item["comparison"]
    assert record["status"] == "PASS_SCOPED_SAME_FRAME_GPU_COMPARISON"
    captures = ROOT / "reports/scene_harness" / record["run_id"] / "captures"
    for image in record["images"].values():
        path = captures / image["preview"]
        assert path.resolve().parent == captures.resolve()
        rgb = Image.open(path).convert("RGB")
        encoded = io.BytesIO()
        rgb.save(encoded, format="PNG", compress_level=6)
        # Packaging may change the PNG encoding, never the RGB pixels.
        assert np.array_equal(np.asarray(rgb), np.asarray(Image.open(io.BytesIO(encoded.getvalue()))))
        image["embedded"] = "data:image/png;base64," + base64.b64encode(encoded.getvalue()).decode("ascii")
        image["embedded_codec"] = "LOSSLESS_RGB_PNG; PIXELS_MATCH_GPU_OPAQUE_PREVIEW"
        count += 1
    portable["items"].append({"scenario": item["scenario"], "status": item["status"], "comparison": record, "failures": []})
template = (ROOT / "tools/dxr_comparison_viewer.html").read_text(encoding="utf-8")
marker = "const embeddedData = null;"
assert template.count(marker) == 1
data = json.dumps(portable, ensure_ascii=True, separators=(",", ":")).replace("</", "<\\/")
output = ROOT / "reports/dxr_comparison/comparison-snapshot.html"
output.write_text(template.replace(marker, "const embeddedData = " + data + ";"), encoding="utf-8")
print(f"OFFLINE_VIEWER={output}")
print(f"LOSSLESS_IMAGES={count} BYTES={output.stat().st_size}")
