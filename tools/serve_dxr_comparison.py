"""Read-only loopback viewer for the harness. Serves only results and PNG evidence."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import time
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--status", type=Path, default=ROOT / "reports/dxr_comparison/suite.status.json")
    parser.add_argument("--idle-seconds", type=int, default=7200)
    args = parser.parse_args()
    assets = (ROOT / "reports/scene_harness").resolve()
    state = {"last_request": time.monotonic()}

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            state["last_request"] = time.monotonic()
            path = unquote(urlsplit(self.path).path)
            if path == "/":
                data = (ROOT / "tools/dxr_comparison_viewer.html").read_bytes()
                content_type = "text/html; charset=utf-8"
            elif path == "/api/status":
                try:
                    result = json.loads(args.status.read_text(encoding="utf-8"))
                except (OSError, ValueError):
                    result = {"status": "WAITING_FOR_CAPTURES", "items": [], "current": None}
                data = json.dumps(result, ensure_ascii=False).encode("utf-8")
                content_type = "application/json; charset=utf-8"
            elif path == "/api/contract":
                contract_path = ROOT / "source/shipwright/scripts/scene-harness/scenarios/scenarios.v1.json"
                try:
                    registry = json.loads(contract_path.read_text(encoding="utf-8"))
                    scenario = next(item for item in registry.get("scenarios", []) if item.get("id") == "TEMPLE_INTERIOR")
                    contract = scenario["lightingContract"]
                    result = {
                        "status": "SOURCE_CONTRACT_LOADED",
                        "source": "source/shipwright/scripts/scene-harness/scenarios/scenarios.v1.json",
                        "scenario": "TEMPLE_INTERIOR",
                        "contract": contract,
                    }
                except (OSError, ValueError, KeyError, StopIteration) as error:
                    result = {"status": "SOURCE_CONTRACT_UNAVAILABLE", "message": str(error)}
                data = json.dumps(result, ensure_ascii=False).encode("utf-8")
                content_type = "application/json; charset=utf-8"
            elif path.startswith("/asset/"):
                target = (assets / path.removeprefix("/asset/")).resolve()
                if not target.is_relative_to(assets) or target.suffix.lower() != ".png" or not target.is_file():
                    self.send_error(404)
                    return
                data = target.read_bytes()
                content_type = "image/png"
            else:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.end_headers()
            self.wfile.write(data)

    with ThreadingHTTPServer(("127.0.0.1", args.port), Handler) as server:
        server.timeout = 1
        url = f"http://127.0.0.1:{server.server_port}/"
        info = ROOT / "reports/dxr_comparison/viewer.server.json"
        info.parent.mkdir(parents=True, exist_ok=True)
        import os
        info.write_text(json.dumps({"url": url, "pid": os.getpid(), "bind": "127.0.0.1", "access": "READ_ONLY"}, indent=2) + "\n")
        print("VIEWER_URL=" + url, flush=True)
        while time.monotonic() - state["last_request"] < args.idle_seconds:
            server.handle_request()


if __name__ == "__main__":
    main()
