import zipfile
import struct
import io

# Scene Command IDs
CMD_START_POS = 0x00
CMD_ACTOR_LIST = 0x01
CMD_CS_CAM = 0x02
CMD_COL_HEADER = 0x03
CMD_ROOM_LIST = 0x04
CMD_WIND = 0x05
CMD_ENTRANCE_LIST = 0x06
CMD_SPECIAL_OBJECTS = 0x07
CMD_ROOM_BEHAVIOR = 0x08
CMD_MESH = 0x0A
CMD_OBJECT_LIST = 0x0B
CMD_LIGHT_LIST = 0x0C
CMD_PATHWAYS = 0x0D
CMD_TRANS_ACTOR_LIST = 0x0E
CMD_LIGHTING_SETTINGS = 0x0F
CMD_TIME_SETTINGS = 0x10
CMD_SKYBOX_SETTINGS = 0x11
CMD_SKYBOX_MODIFIER = 0x12
CMD_EXIT_LIST = 0x13
CMD_END_MARKER = 0x14
CMD_SOUND_SETTINGS = 0x15
CMD_ECHO_SETTINGS = 0x16
CMD_CUTSCENES = 0x17
CMD_ALT_HEADERS = 0x18
CMD_CAMERA_SETTINGS = 0x19

ACTOR_OBJ_SYOKUDAI = 0x005D # 93 - Torch

def read_string(f):
    length_bytes = f.read(4)
    if len(length_bytes) < 4: return ""
    length = struct.unpack('<I', length_bytes)[0]
    s_bytes = f.read(length)
    return s_bytes.decode('utf-8', errors='ignore')

def parse_room_actors(data, room_name):
    if len(data) < 0x48: return []
    f = io.BytesIO(data)
    f.seek(0x40) # skip 64-byte OTR resource header
    cmd_count_bytes = f.read(4)
    if len(cmd_count_bytes) < 4: return []
    cmd_count = struct.unpack('<I', cmd_count_bytes)[0]
    
    actors = []
    for _ in range(cmd_count):
        cmd_id_bytes = f.read(4)
        if len(cmd_id_bytes) < 4: break
        cmd_id = struct.unpack('<I', cmd_id_bytes)[0]
        
        if cmd_id == CMD_ACTOR_LIST:
            # SetActorListFactory:
            # ReadCommandId -> reads 4 bytes cmdId (already read) or does it read more?
            # In SetActorListFactory.cpp: ReadCommandId(setActorList, reader);
            # Let's check what ReadCommandId does in SceneCommandFactory.cpp!
            pass
    return actors

def scan_all_rooms():
    zf = zipfile.ZipFile('runtime/build-test/oot.o2r')
    torches_found = []
    
    # We can scan all binary files in scenes/
    for name in zf.namelist():
        if not name.startswith('scenes/'): continue
        if any(x in name.lower() for x in ['tex', 'dl', 'vtx', 'tlut', 'anim', 'cs', 'col']): continue
        
        data = zf.read(name)
        if len(data) < 0x48: continue
        
        # Scan for actor entry chunks: 16 bytes: <HhhhhhhH
        for pos in range(0x40, len(data) - 16, 2):
            actor_id, px, py, pz, rx, ry, rz, params = struct.unpack('<HhhhhhhH', data[pos:pos+16])
            if actor_id == ACTOR_OBJ_SYOKUDAI:
                # Validate realistic coordinates
                if -10000 < px < 10000 and -10000 < py < 10000 and -10000 < pz < 10000:
                    torches_found.append({
                        'resource': name,
                        'offset': hex(pos),
                        'pos': (px, py, pz),
                        'rot': (rx, ry, rz),
                        'params': hex(params),
                        'lit_initially': bool(params & 0x400)
                    })
                    
    print(f"Total potential torches found: {len(torches_found)}")
    by_scene = {}
    for t in torches_found:
        scene = t['resource'].split('/')[2] if len(t['resource'].split('/')) > 2 else 'other'
        by_scene.setdefault(scene, []).append(t)
        
    for sc, tlist in sorted(by_scene.items()):
        print(f"\nScene: {sc} (Torches: {len(tlist)})")
        for t in tlist[:5]:
            print(f"  {t['resource']} @ Pos={t['pos']} Params={t['params']} (Lit={t['lit_initially']})")

if __name__ == '__main__':
    scan_all_rooms()
