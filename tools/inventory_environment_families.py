"""Inventory native scene/room variants; proposals never enable production lighting.

Skybox/indoor flags describe authoring. They do not prove an unobstructed sky
or a physical window, so a visual/geometry review remains an explicit gate.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile
from audit_temple_light_origins import parse_scene

ROOT=Path(__file__).resolve().parents[1]
FAMILIES=["EXTERIOR_OPEN_SKY","EXTERIOR_PARTLY_COVERED","INTERIOR_APERTURES",
          "INTERIOR_ARTIFICIAL","CAVE_DUNGEON","MIXED_PENDING"]

def inventory(archive_path,output,pilot=None):
    table=(ROOT/"source/shipwright/soh/include/tables/scene_table.h").read_text()
    scenes={segment:(int(number,16),enum) for number,segment,enum in re.findall(
        r"/\*\s*0x([0-9A-Fa-f]+)\s*\*/\s*DEFINE_SCENE\(\s*([^,]+),\s*[^,]+,\s*([^,]+)",table)}
    records=[];failures=[]
    with zipfile.ZipFile(archive_path) as archive:
        names=set(archive.namelist())
        headers=sorted(n for n in names if re.fullmatch(r"scenes/[^/]+/[^/]+/[^/]+(?:_scene|_room_\d+)(?:Set_[A-Fa-f0-9]{6})?",n))
        for path in headers:
            file=path.rsplit("/",1)[-1]
            base=file.split("Set_")[0]
            scene_segment=base.split("_room_")[0]+"_scene" if "_room_" in base else base
            identity=scenes.get(scene_segment)
            room=int(base.rsplit("_room_",1)[1]) if "_room_" in base else None
            header_variant=file[len(base):] or "DEFAULT"
            try: commands=parse_scene(archive.read(path))
            except (ValueError,IndexError,UnicodeError) as error:
                failures.append({"resource":path,"reason":str(error)});continue
            sky=next((c for c in commands if c["command"]=="0x11"),{})
            disabled=next((c for c in commands if c["command"]=="0x12"),{})
            lights=[c for c in commands if c["command"] in ("0x0C","0x0F")]
            alternate=[c for c in commands if c["command"]=="0x18"]
            meshes=[m for c in commands if c["command"]=="0x0A" for m in c.get("meshes",[])]
            notes=[];proposal="MIXED_PENDING"
            if sky.get("indoors")==0 and sky.get("skybox_id",0)!=0:
                proposal="EXTERIOR_OPEN_SKY"
                notes.append("Native skybox and outdoor flag; roof/vegetation coverage requires geometry review.")
            elif sky.get("indoors"):
                notes.append("Native indoor flag; windows/artificial sources cannot be inferred from this flag.")
            if identity and identity[0]<0x11:
                proposal="CAVE_DUNGEON";notes.append("Native scene-table dungeon range; apertures and room lighting still need review.")
            if scene_segment=="tokinoma_scene":
                proposal="INTERIOR_APERTURES";notes.append("Temple window geometry has a separate resource audit; each pane still requires transport evidence.")
            records.append({"sceneId":identity[0] if identity else None,
                "sceneName":identity[1] if identity else scene_segment,"room":room,
                "headerVariant":header_variant,"variant":{"age":"UNRESOLVED","setup":header_variant,
                    "time":"RUNTIME_DEPENDENT","weather":sky.get("weather","UNRESOLVED")},
                "resource":path,"resourceSha256":hashlib.sha256(archive.read(path)).hexdigest(),
                "origin":"NATIVE","family":"MIXED_PENDING","familyProposal":proposal,
                "confidence":"LOW_REQUIRES_GEOMETRY_AND_VISUAL_REVIEW","evidence":notes,
                "nativeSkybox":sky,"nativeSkyRestrictions":disabled,"nativeLighting":lights,
                "alternateHeaders":alternate,"meshReferences":meshes,
                "tags":[],"tagsPending":["water","lava","fog","emissive"],
                "productionEnabled":False,"profileOrigin":"INFERRED",
                "approvalGate":"PILOT_THEN_ROOM_REVIEW"})
    result={"schemaVersion":1,"families":FAMILIES,"archiveSha256":hashlib.sha256(archive_path.read_bytes()).hexdigest(),
        "pilotEvidence":str(pilot) if pilot else None,"scope":"NATIVE_HEADER_INVENTORY_WITH_UNAPPROVED_PROPOSALS",
        "records":records,"parseFailures":failures,
        "policy":{"inferredProduction":False,"automaticSameLightingForFamily":False,
            "authoredSourcesRequire":["room","resource","geometryAnchor","orientation","sourceOrigin","receiverEvidence"],
            "variantResolution":"Alternate setup is not assumed to encode age/time without command-table evidence."}}
    output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    return {"headers":len(records),"scenes":len({r["sceneId"] for r in records if r["sceneId"] is not None}),
            "parseFailures":len(failures),"productionProfilesEnabled":0,"output":str(output)}

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("archive",type=Path)
    parser.add_argument("--output",type=Path,default=ROOT/"reports/navi_pilot_20261015/environment-inventory.json")
    parser.add_argument("--pilot",type=Path);args=parser.parse_args()
    print(json.dumps(inventory(args.archive,args.output,args.pilot),indent=2))
