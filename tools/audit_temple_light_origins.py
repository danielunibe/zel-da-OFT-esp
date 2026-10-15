"""Read Temple of Time lighting and window geometry without starting the game.

The binary layouts follow Shipwright's SceneFactory, scene-command importers,
ArrayFactory and libultraship's DisplayListFactory. This tool audits authored
resources and existing logs; it does not certify a rendered lighting solution.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import struct
import zipfile
from pathlib import Path


class Reader:
    def __init__(self, data: bytes):
        if len(data) < 64 or data[0] != 0:
            raise ValueError("Expected an OTR little-endian binary resource")
        self.data = data
        self.pos = 64

    def read(self, fmt: str):
        fmt = "<" + fmt
        result = struct.unpack_from(fmt, self.data, self.pos)
        self.pos += struct.calcsize(fmt)
        return result[0] if len(result) == 1 else result

    def string(self):
        count = self.read("I")
        if count > len(self.data) - self.pos:
            raise ValueError("Invalid OTR string length")
        result = self.data[self.pos:self.pos + count].decode("utf-8")
        self.pos += count
        return result


def parse_scene(data: bytes):
    reader = Reader(data)
    result = []
    for index in range(reader.read("I")):
        offset = reader.pos
        command = reader.read("I")
        item = {"index": index, "command": f"0x{command:02X}", "byte_offset": offset}
        if command in (0x00, 0x01):
            item["actors"] = []
            for _ in range(reader.read("I")):
                actor = reader.read("HhhhhhhH")
                item["actors"].append({"id": actor[0], "position": list(actor[1:4]),
                                       "rotation_binang": list(actor[4:7]), "params": actor[7]})
        elif command == 0x18:
            item["alternate_headers"] = [reader.string() for _ in range(reader.read("I"))]
        elif command == 0x04:
            item["rooms"] = []
            for _ in range(reader.read("I")):
                item["rooms"].append({"path": reader.string(), "vrom": list(reader.read("II"))})
        elif command in (0x03, 0x17):
            item["resource"] = reader.string()
        elif command in (0x0B, 0x13):
            item["ids"] = [reader.read("H") for _ in range(reader.read("I"))]
        elif command == 0x06:
            item["entrances"] = [list(reader.read("BB")) for _ in range(reader.read("I"))]
        elif command == 0x0E:
            item["transition_actors"] = [list(reader.read("BBBBhhhhhh"))
                                          for _ in range(reader.read("I"))]
        elif command == 0x0F:
            item["lighting_settings"] = []
            for _ in range(reader.read("I")):
                setting = {"ambient_rgb": list(reader.read("BBB")),
                           "light1_direction": list(reader.read("bbb")),
                           "light1_rgb": list(reader.read("BBB")),
                           "light2_direction": list(reader.read("bbb")),
                           "light2_rgb": list(reader.read("BBB")),
                           "fog_rgb": list(reader.read("BBB")),
                           "fog_near": reader.read("h"), "fog_far": reader.read("H")}
                item["lighting_settings"].append(setting)
        elif command == 0x0C:
            item["native_lights"] = [list(reader.read("BhhhBBBBh"))
                                      for _ in range(reader.read("I"))]
        elif command == 0x0A:
            item["data"], item["mesh_type"], count = reader.read("BBB")
            if item["mesh_type"] not in (0, 2):
                raise ValueError("Only polygon mesh types 0 and 2 are supported")
            item["meshes"] = []
            for _ in range(count):
                mesh = {"polygon_type": reader.read("B")}
                if item["mesh_type"] == 2:
                    mesh["culling_center_and_radius"] = list(reader.read("hhhh"))
                mesh["opaque"] = reader.string()
                mesh["translucent"] = reader.string()
                item["meshes"].append(mesh)
        elif command in (0x08, 0x19, 0x02):
            item["values"] = list(reader.read("BI"))
        elif command == 0x11:
            item["unknown"], item["skybox_id"], item["weather"], item["indoors"] = reader.read("BBBB")
        elif command in (0x10, 0x15):
            item["values"] = list(reader.read("BBB"))
        elif command == 0x12:
            item["skybox_disabled"], item["sun_moon_disabled"] = reader.read("BB")
        elif command == 0x07:
            item["values"] = list(reader.read("BH"))
        elif command == 0x16:
            item["echo"] = reader.read("B")
        elif command == 0x05:
            item["wind"] = list(reader.read("BBBB"))
        elif command == 0x0D:
            item["paths"] = [reader.string() for _ in range(reader.read("I"))]
        elif command != 0x14:
            raise ValueError(f"Unsupported scene command {command:#x} at {offset}")
        result.append(item)
    if reader.pos != len(data):
        raise ValueError(f"Unparsed scene bytes: {len(data) - reader.pos}")
    return result


def crc64(text: str):
    value = 0xFFFFFFFFFFFFFFFF
    mask = value
    for byte in text.encode("utf-8"):
        value ^= byte << 56
        for _ in range(8):
            value = ((value << 1) ^ (0x42F0E1EBA9EA3693 if value & (1 << 63) else 0)) & mask
    return value


def parse_display_list(data: bytes, names: dict[int, str]):
    reader = Reader(data)
    ucode = reader.read("B")
    reader.pos = (reader.pos + 7) & ~7
    result = []
    while reader.pos < len(data):
        offset = reader.pos
        w0, w1 = reader.read("II")
        opcode = w0 >> 24
        entry = {"offset": offset, "opcode": f"0x{opcode:02X}",
                 "w0": f"0x{w0:08X}", "w1": f"0x{w1:08X}"}
        if opcode in (0x20, 0x31, 0x32, 0x33, 0x35, 0x36):
            high, low = reader.read("II")
            resource_hash = (high << 32) | low
            entry["resource_hash"] = f"{resource_hash:016X}"
            entry["resource"] = names.get(resource_hash, "UNRESOLVED")
        if opcode == 0xD9:
            entry["lighting_set"] = bool(w1 & 0x00020000)
            entry["lighting_cleared"] = not bool(w0 & 0x00020000)
        result.append(entry)
        if opcode == (0xDF if ucode in (4, 5) else 0xB8):
            break
    if reader.pos != len(data):
        raise ValueError(f"Unparsed display-list bytes: {len(data) - reader.pos}")
    return {"ucode": ucode, "commands": result}


def vertices(data: bytes):
    reader = Reader(data)
    resource_type = struct.unpack_from("<I", data, 4)[0]
    if resource_type == 0x4F415252:  # SOH Array, ArrayResourceType::Vertex
        if reader.read("I") != 25:
            raise ValueError("Expected a vertex array")
    count = reader.read("I")
    result = []
    for _ in range(count):
        vertex = reader.read("hhhHhhBBBB")
        result.append({"position": list(vertex[:3]), "uv": list(vertex[4:6]),
                       "rgba": list(vertex[6:])})
    if reader.pos != len(data):
        raise ValueError("Vertex resource length does not match its count")
    return result


def parse_light_log(run: Path):
    lines = (run / "harness-backend.log").read_text(encoding="utf-8", errors="replace").splitlines()
    capture_frame = json.loads((run / "capture.ack.json").read_text(encoding="utf-8-sig"))["capture_frame"]
    records = []
    for number, line in enumerate(lines, 1):
        if "[COUCH-DXR-LIGHT-SYNC]" not in line or "scene=0x43" not in line:
            continue
        fields = dict(re.findall(r"(\w+)=([^\s]+)", line))
        source = [float(v) for v in fields["sourceDir"].strip("()").split(",")]
        productive = [float(v) for v in fields["productiveDir"].strip("()").split(",")]
        angle = math.degrees(math.acos(max(-1.0, min(1.0, sum(a * b for a, b in zip(source, productive))))))
        records.append({"line": number, "frame": int(fields["frame"]),
                        "game_frame_native_state_counter": int(fields["gameFrame"]),
                        "observed_fast3d_direction": source, "productive_direction": productive,
                        "direction_difference_degrees": angle, "match": fields["match"]})
    return {"capture_frame": capture_frame, "capture_record": next((r for r in records if r["frame"] == capture_frame), None),
            "logged_temple_frames": len(records), "mismatched_frames": sum(r["match"] != "YES" for r in records),
            "counter_note": "Native game counter is state.frames; capture request game_frame is the harness counter. Do not equate them."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--run", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    prefix = "scenes/shared/tokinoma_scene/"
    object_prefix = "objects/object_toki_objects/"
    with zipfile.ZipFile(args.archive, "r") as archive:
        temple_names = [n for n in archive.namelist() if n.startswith((prefix, object_prefix))]
        name_lookup = {crc64(n): n for n in temple_names}
        scenes = {}
        scene_names = [n for n in temple_names if re.fullmatch(
            r"scenes/shared/tokinoma_scene/tokinoma_(?:scene|room_[01])(?:Set_[0-9A-F]{6})?", n)]
        for path in scene_names:
            scenes[path] = {"sha256": hashlib.sha256(archive.read(path)).hexdigest(),
                            "commands": parse_scene(archive.read(path))}
        actors = []
        for resource, scene in scenes.items():
            for command in scene["commands"]:
                actors.extend({"resource": resource, **actor} for actor in command.get("actors", []) if actor["id"] == 0x6A)
        display_lists = {}
        for suffix in ("object_toki_objects_DL_007E20", "object_toki_objects_DL_007EE0", "object_toki_objects_DL_008190"):
            path = object_prefix + suffix
            display_lists[path] = parse_display_list(archive.read(path), name_lookup)
        mesh_vertices = {}
        for display_list in display_lists.values():
            for command in display_list["commands"]:
                path = command.get("resource", "")
                if command["opcode"] == "0x32" and path != "UNRESOLVED":
                    mesh_vertices[path] = vertices(archive.read(path))
        window_path = object_prefix + "object_toki_objectsVtx_0078C0"
        window_vertices = mesh_vertices[window_path]
        groups = [window_vertices[s:e] for s, e in ((0, 5), (5, 10), (10, 15), (15, 20), (20, 24))]
        windows = []
        for index, group in enumerate(groups):
            low = [min(v["position"][axis] for v in group) for axis in range(3)]
            high = [max(v["position"][axis] for v in group) for axis in range(3)]
            windows.append({"index": index, "local_bounds_min": low, "local_bounds_max": high,
                            "local_bounds_center": [(a + b) / 2 for a, b in zip(low, high)],
                            "note": "Bounds center is a geometry anchor, not an authored physical light."})
        default_window_actor = next(actor for actor in actors if actor["params"] == 0 and
                                    actor["resource"] == prefix + "tokinoma_room_1")
        if default_window_actor["rotation_binang"] != [0, 0, 0]:
            raise ValueError("World anchor export currently requires an unrotated window actor")
        for window in windows:
            for key in ("bounds_min", "bounds_max", "bounds_center"):
                window["world_" + key] = [coordinate + offset for coordinate, offset in
                                          zip(window["local_" + key], default_window_actor["position"])]
            window["world_transform_basis"] = "Default room_1 actor params=0; InitChain scale=1; rotation=0; Actor_Draw translation"
        with args.archive.open("rb") as stream:
            archive_sha256 = hashlib.file_digest(stream, "sha256").hexdigest()
        light_evidence = parse_light_log(args.run)
        captured_light = light_evidence["capture_record"]
        provenance_status = ("NOT_RECORDED_AT_CAPTURE" if captured_light is None else
                             "FAIL_DIRECTION_MISMATCH_AT_CAPTURE" if captured_light["match"] != "YES" else
                             "PASS_DIRECTION_MATCH_ONLY")
        output = {"schema_version": 1, "scope": "STATIC_AUTHORED_RESOURCES_AND_EXISTING_LOGS_NO_GAME_LAUNCH",
                  "light_provenance_status": provenance_status,
                  "window_emitter_certification": "NOT_IMPLEMENTED_IN_CURRENT_LIGHT_MODEL",
                  "physical_lighting_certification": "NOT_CERTIFIED",
                  "archive_sha256": archive_sha256,
                  "limitations": ["No area-light or portal emitter is inferred from a window's brightness.",
                                  "Archive resources do not certify active alternate-asset selection.",
                                  "Probe direction is interpreter evidence, not a physical emitter coordinate."],
                  "scenes": scenes, "window_actor_instances": actors,
                  "window_display_lists": display_lists, "referenced_vertices": mesh_vertices,
                  "window_geometry_anchors": windows, "existing_runtime_light_evidence": light_evidence}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(args.output), "light_provenance_status": provenance_status,
                      "parsed_scene_headers": len(scenes),
                      "default_window_actor": default_window_actor,
                      "window_geometry_anchors": windows,
                      "existing_runtime_light_evidence": output["existing_runtime_light_evidence"]}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
