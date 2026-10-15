"""Verify the selected native source against real GPU receivers in HDR linear space.

The four controls come from one dispatch and one command list. This analyzer
never renders a replacement game frame. SDR comparisons are display evidence;
the acceptance gates use raw FP16 data and GPU first-hit records.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]

def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def direct_brdf(receiver, direction, view_direction):
    """Independent CPU evaluation of the shader's Lambert + GGX convention."""
    base=np.array(receiver["baseColorLinear"],dtype=float)
    n=np.array(receiver.get("normalWorld",receiver["normalTrace"]),dtype=float)
    view=np.array(view_direction,dtype=float)
    view/=max(np.linalg.norm(view),1e-10)
    ndl=max(0.,float(n@direction)); ndv=max(0.,float(n@view))
    if ndl<=0: return np.zeros(3)
    h=view+direction; h/=np.linalg.norm(h)
    ndh=max(0.,float(n@h)); vdh=max(0.,float(view@h))
    metallic=np.clip(receiver["metallic"],0,1)
    roughness=np.clip(receiver["roughness"],.04,1)
    specular=np.clip(receiver["specular"],0,1)
    f0=.04*specular*(1-metallic)+base*metallic
    fresnel=f0+(1-f0)*(1-vdh)**5
    diffuse=base*(1-metallic)*(1-fresnel)*ndl/np.pi
    a2=roughness**4
    distribution=a2/max(np.pi*(ndh*ndh*(a2-1)+1)**2,1e-5)
    k=(roughness+1)**2/8
    geom=lambda x:x/max(x*(1-k)+k,1e-5)
    spec=distribution*geom(ndv)*geom(ndl)*fresnel/max(4*ndv*ndl,1e-4)*ndl if ndv>0 else np.zeros(3)
    return diffuse+spec

def read_camera_eye(run):
    for line in (run/"harness.log").read_text(encoding="utf-8-sig").splitlines():
        if not line.strip(): continue
        event=json.loads(line)
        if event.get("event")=="CAMERA_POSE_FROZEN":
            eye=np.array(event["camera_eye"],dtype=float)
            if eye.shape==(3,) and np.isfinite(eye).all(): return eye
            raise ValueError("Captured camera eye is malformed")
    raise ValueError("Frozen camera pose is missing from the harness log")

def analyze(run:Path):
    sources=read(run/"navi.sources.json"); gpu=read(run/"navi.receivers.json")
    pose=read(run/"navi.pose.json"); launcher=read(run/"launcher.status.json")
    verify=read(run/"verification.status.json")
    source=next((s for s in sources["sources"] if s["ownerId"]==sources["selectedOwner"] and s["role"]==1),None)
    if not source: raise ValueError("Navi owner/main native light was not captured")
    radius=abs(source["rawRadius"])
    camera_eye_world=read_camera_eye(run)
    source_world=np.array(source.get("worldPosition",source["tracePosition"]),dtype=float)
    effective_color=np.array(source.get("effectiveDxrColor",
        np.array(source["linearColor"],dtype=float)*float(source.get("appliedPointGain",1.0))),dtype=float)
    if gpu["frame"]!=sources["frame"] or gpu["viewHash"]!=sources["viewHash"]:
        raise ValueError("GPU source and receiver frame/view mismatch")
    if not (launcher["background"] and launcher["processExited"] and launcher["originalSaveUnchanged"]
            and not launcher["backendDeviceRemoved"] and not verify["failures"] and verify["sameFrame"]):
        raise ValueError("Isolated runtime/capture integrity failed")
    presentations=[json.loads(l) for l in (run/"presentation.evidence.jsonl").read_text().splitlines() if l.strip()]
    if not presentations or any(p["dxgi_present_call"]!="SKIPPED_BACKGROUND" or p["window_visible"] or p["window_foreground"] for p in presentations):
        raise ValueError("Hidden/no-Present evidence missing")
    anchor=pose.get("receiverAnchor",{})
    target_resource=anchor.get("resource")
    arrays={}; metas={}; images={}; sdr={}
    hdr_stages={"A":"navi_a_hdr","B":"composite_pre_tonemap","C":"navi_c_hdr","D":"navi_d_hdr"}
    for variant,stage in hdr_stages.items():
        meta=read(run/"captures"/(stage+".meta.json"))
        if meta["format"]!="R16G16B16A16_FLOAT" or meta["frame"]!=gpu["frame"]:
            raise ValueError("HDR control format/frame mismatch: "+stage)
        path=(run/"captures"/meta["payload"]).resolve()
        if path.parent!=(run/"captures").resolve(): raise ValueError("Invalid payload path")
        a=np.fromfile(path,dtype="<f2").reshape(meta["height"],meta["width"],4).astype(np.float32)[...,:3]
        if not np.isfinite(a).all(): raise ValueError("Nonfinite HDR control")
        arrays[variant]=a;metas[variant]=meta
        sdr_stage="navi_"+variant.lower()+"_sdr"
        smeta=read(run/"captures"/(sdr_stage+".meta.json"))
        if smeta["frame"]!=gpu["frame"] or smeta["request"]!=meta["request"]:
            raise ValueError("SDR/HDR request mismatch")
        image_path=run/"captures"/smeta["payload"]
        sdr[variant]=np.asarray(Image.open(image_path).convert("RGB"))
        preview=smeta["payload"].removesuffix("_raw.png")+"_opaque_preview.png"
        if not (run/"captures"/preview).is_file(): raise ValueError("GPU SDR preview missing")
        images[sdr_stage]={"raw":smeta["payload"],"preview":preview,"sha256":digest(image_path)}
    first=metas["A"]
    if any(m["request"]!=first["request"] or a.shape!=arrays["A"].shape for v,m in metas.items() for a in [arrays[v]]):
        raise ValueError("Control state or dimensions mismatch")
    point=arrays["B"]-arrays["A"]; mesh=arrays["C"]-arrays["A"]
    residual=arrays["D"]-arrays["B"]-arrays["C"]+arrays["A"]
    height,width=point.shape[:2]
    view=np.array(sources["viewMatrixRowVector"],dtype=float)
    inv=np.linalg.inv(view)
    samples=gpu["samples"]; verified=[]; targets=[]
    for receiver in samples:
        pos=np.array(receiver["positionTrace"],dtype=float)
        delta=np.array(source["tracePosition"])-pos
        distance=float(np.linalg.norm(delta)); radius=abs(source["rawRadius"])
        world=np.r_[pos,1.]@inv
        world_position=world[:3]/world[3]
        normal=np.r_[receiver["normalTrace"],0.]@view.T
        normal=normal[:3]/max(np.linalg.norm(normal[:3]),1e-10)
        world_delta=source_world-world_position
        world_distance=float(np.linalg.norm(world_delta))
        view_direction=camera_eye_world-world_position
        expected_attenuation=max(0.,1-(distance/radius)**2) if source["enabled"] and radius>0 else 0.
        # The GPU deliberately skips distance work for a disabled/out-of-range source.
        active=source["enabled"] and distance<radius
        errors=[]
        if active:
            if abs(receiver["distance"]-distance)>.03: errors.append("distance")
            if abs(receiver["attenuation"]-expected_attenuation)>2e-4: errors.append("attenuation")
            expected=direct_brdf(receiver,world_delta/max(world_distance,1e-10),view_direction)*effective_color*expected_attenuation*receiver["visibility"]
            if not np.allclose(expected,receiver["pointRadiance"],rtol=0.05,atol=8e-3): errors.append("BRDF/color/visibility")
        elif max(receiver["pointRadiance"])>1e-7: errors.append("inactive_or_outside_radius")
        receiver["positionWorld"]=list(world[:3]/world[3]);receiver["normalWorld"]=list(normal)
        receiver["sourceDistanceWorld"]=distance
        receiver["cpuContractErrors"]=errors
        receiver["normalizedPixel"]=[(receiver["pixel"][0]+.5)/gpu["dispatchSize"][0],(receiver["pixel"][1]+.5)/gpu["dispatchSize"][1]]
        if not errors: verified.append(receiver)
        resource=receiver.get("resource","")
        target_material_match=(resource==target_resource) if target_resource else "spot04" in resource.lower()
        receiver["targetMaterialMatch"]=bool(target_material_match)
        if receiver["albedoReconstructed"] and receiver.get("ownerId")==0 and normal[1]>.8 and target_material_match:
            targets.append(receiver)
    # Refuse to certify a clothing/fairy hit as the ground receiver.
    lit_target=[r for r in targets if max(r["pointRadiance"])>1e-6 and r["visibility"]>.99 and not r["cpuContractErrors"]]
    link_samples=[r for r in samples if any(token in r.get("resource","").lower() for token in
        ("objects/gameplay_keep/glink", "objects/gameplay_keep/ghylian", "objects/object_link_"))]
    lit_link=[r for r in link_samples if max(r["pointRadiance"])>1e-6 and r["visibility"]>.99 and not r["cpuContractErrors"]]
    strongest_link=max(lit_link,key=lambda r:max(r["pointRadiance"]),default=None)
    emission_target=[r for r in targets if max(r["meshRadiance"])>1e-8]
    mesh_hdr_delta=np.abs(arrays["C"]-arrays["A"])
    mesh_sdr_delta=np.max(np.abs(sdr["C"].astype(np.int16)-sdr["A"].astype(np.int16)),axis=2)
    mesh_visible_pixels=int(np.count_nonzero(mesh_sdr_delta>8))
    mesh_target_values=np.array([r["meshRadiance"] for r in emission_target],dtype=float) if emission_target else np.zeros((0,3))
    mesh_target_mean=mesh_target_values.mean(axis=0).tolist() if len(mesh_target_values) else [0.0,0.0,0.0]
    mesh_target_peak=float(mesh_target_values.max()) if len(mesh_target_values) else 0.0
    errors=[r for r in samples if r["cpuContractErrors"]]
    activated=source["enabled"]
    source_ids=[s["sourceId"] for s in sources["sources"]]
    aux=[s for s in sources["sources"] if s["ownerId"]==source["ownerId"] and s["role"]==2]
    activation_ok=len(source_ids)==len(set(source_ids)) and len(aux)==1 and (
        aux[0]["enabled"]==(aux[0]["rawRadius"]>0))
    additive_ok=float(np.max(np.abs(residual)))<=max(.002*float(np.max(arrays["D"])),2e-5)
    point_ok=bool(lit_target) if activated else bool(samples) and float(np.max(np.abs(point)))<1e-6
    gates={"source_identity_and_activation":"PASS" if activation_ok else "FAIL",
        "receiver_albedo_normal_contract":"PASS" if targets else "FAIL",
        "native_distance_color_brdf":"PASS" if samples and not errors else "FAIL",
        "point_to_target_surface":"PASS" if point_ok else "FAIL",
        "point_to_link_material":"PASS" if lit_link else "NOT_DEMONSTRATED",
        "mesh_emission_to_target_surface":"VISUALLY_MEASURABLE" if mesh_visible_pixels else
            "MEASURED_BUT_NOT_VISUALLY_SIGNIFICANT" if emission_target else "NOT_DEMONSTRATED",
        "linear_additivity_diagnostic":"PASS" if additive_ok else "FAIL",
        "opaque_occlusion":"MEASURED_VISIBLE_AND_BLOCKED" if any(r["visibility"]==0 and 0<r["sourceDistanceWorld"]<radius for r in targets) and lit_target else "NOT_DEMONSTRATED",
        "human_visual_acceptance":"PENDING"}
    pairs={}
    for name,on,off in (("navi_point","B","A"),("navi_mesh","C","A"),("navi_double","D","B")):
        d=np.abs(sdr[on].astype(np.int16)-sdr[off].astype(np.int16)); mag=d.max(2)
        hd=arrays[on]-arrays[off]
        pairs[name]={"on":"navi_"+on.lower()+"_sdr","off":"navi_"+off.lower()+"_sdr",
            "labels":[off+" · puntual "+("activa" if off in "BD" else "apagada")+" / malla "+("activa" if off in "CD" else "apagada"),on+" · puntual "+("activa" if on in "BD" else "apagada")+" / malla "+("activa" if on in "CD" else "apagada")],
            "total_pixels":int(mag.size),"mean_absolute_rgb_byte_delta":float(d.mean()),
            "max_channel_byte_delta":int(mag.max()),"pixel_counts_above_byte_delta":{str(n):int(np.count_nonzero(mag>n)) for n in (0,2,8,16,32)},
            "hdr_linear":{"mean_absolute_rgb_delta":float(np.abs(hd).mean()),"max_absolute_rgb_delta":float(np.abs(hd).max()),"negative_pixels":int(np.count_nonzero(hd.min(2)<-1e-5))}}
    representative=max(lit_target or targets or samples,key=lambda r:max(r["pointRadiance"]),default=None)
    result={"schema_version":1,"kind":"NAVI_CAUSAL_PILOT","status":"MEASURED_CAUSAL_PILOT",
        "run_id":run.name,"scenario":first["request"]["scenario_id"],"view":first["request"]["view_id"],
        "frame":gpu["frame"],"width":width,"height":height,"images":images,"pairs":pairs,
        "source":source,"sources":sources["sources"],"pose":pose,"gates":gates,
        "identity":{"shader_source_sha256":sources["shaderSourceSha256"],"shader_abi":sources["shaderAbi"],
            "executable_sha256":read(run/"manifest.launcher.json")["executableIdentity"]["sha256"],"launcher_manifest":read(run/"manifest.launcher.json")},
        "representative_receiver":representative,"receiver_samples":samples,
        "target_surface":{"resource":target_resource or "spot04 fallback", "evidence":anchor.get("evidence"),
            "anchor_world_position":anchor.get("worldPosition"), "receiver_samples":len(targets),
            "visible_in_radius_samples":len(lit_target),
            "nonzero_point_radiance_samples":sum(max(r["pointRadiance"])>1e-6 for r in targets),
            "normal_contract":"UPWARD" if targets and all(r["normalWorld"][1]>.8 for r in targets) else "INCOMPLETE"},
        "target_receivers":len(targets),"lit_target_receivers":len(lit_target),"emission_target_receivers":len(emission_target),
        "link_surface_samples":len(link_samples),"lit_link_surface_samples":len(lit_link),
        "link_surface_resources":sorted({r["resource"] for r in link_samples}),
        "strongest_link_point_radiance_rgb":strongest_link["pointRadiance"] if strongest_link else [0.0,0.0,0.0],
        "strongest_link_specular_radiance_rgb":strongest_link["pointSpecularRadiance"] if strongest_link else [0.0,0.0,0.0],
        "strongest_link_material":{"resource":strongest_link["resource"],"pixel":strongest_link["pixel"],
            "roughness":strongest_link["roughness"],"metallic":strongest_link["metallic"],
            "specular":strongest_link["specular"],"distance":strongest_link["sourceDistanceWorld"],
            "visibility":strongest_link["visibility"]} if strongest_link else None,
        "mesh_emission_target_mean_linear_rgb":mesh_target_mean,
        "mesh_emission_target_peak_linear":mesh_target_peak,
        "mesh_emission_sdr_pixels_above_8_byte":mesh_visible_pixels,
        "mesh_emission_hdr_mean_absolute_delta":float(mesh_hdr_delta.mean()),
        "mesh_emission_hdr_peak_absolute_delta":float(mesh_hdr_delta.max()),
        "floor_receivers":len(targets),"lit_floor_receivers":len(lit_target),"emission_floor_receivers":len(emission_target),
        "max_linear_additivity_error":float(np.abs(residual).max()),
        "isolation":"Selected native light or same actor's mesh; other light energy and bloom excluded. A is an unlit receiver diagnostic, not Classic mode.",
        "physical_coherence":"PASS_SCOPED_POINT_CONTRACT" if point_ok and not errors and targets and activation_ok else "INCOMPLETE",
        "visual_acceptance":"PENDING_HUMAN_REVIEW","hidden_no_present_verified":True,"original_saves_unchanged":True}
    (run/"navi-analysis.json").write_text(json.dumps(result,indent=2,allow_nan=False)+"\n",encoding="utf-8")
    return result

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("run",type=Path)
    parser.add_argument("--publish",action="store_true");args=parser.parse_args()
    result=analyze(args.run.resolve())
    if args.publish:
        path=ROOT/"reports/dxr_comparison/suite.status.json"
        data=read(path) if path.exists() else {"items":[]}
        data["items"]=[i for i in data.get("items",[]) if i.get("scenario")!=result["scenario"]]
        data["items"].insert(0,{"scenario":result["scenario"],"status":result["physical_coherence"],"comparison":result,"failures":[]})
        data.update(status="NAVI_PILOT_UPDATED",current=None,finished_utc=datetime.now(timezone.utc).isoformat())
        temp=path.with_suffix(".tmp");temp.write_text(json.dumps(data,allow_nan=False)+"\n",encoding="utf-8");temp.replace(path)
    print(json.dumps({k:result[k] for k in ("scenario","frame","gates","target_surface","physical_coherence")},indent=2))
