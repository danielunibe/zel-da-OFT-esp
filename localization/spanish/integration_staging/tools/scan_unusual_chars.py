import json

path = r"C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV\localization_workspace\spanish\corpus\MASTER_LOCALIZATION_ES_419.jsonl"

all_unusual = []
with open(path, "r", encoding="utf-8") as f:
    for line in f:
        if line.strip():
            d = json.loads(line)
            txt = d.get("spanish", "")
            for i, c in enumerate(txt):
                cp = ord(c)
                is_ascii = (32 <= cp <= 126) or c in ["\n", "\r", "\t"]
                is_sp = c in ["á", "é", "í", "ó", "ú", "Á", "É", "Í", "Ó", "Ú", "ñ", "Ñ", "¡", "¿"]
                if not (is_ascii or is_sp):
                    all_unusual.append({
                        "domain": d.get("domain"),
                        "id": d.get("id"),
                        "char": repr(c),
                        "codepoint": f"U+{cp:04X}",
                        "context": [f"U+{ord(ch):04X}" for ch in txt[max(0, i-5):min(len(txt), i+6)]]
                    })

print(f"Total unusual character occurrences: {len(all_unusual)}")
by_cp = {}
for u in all_unusual:
    cp = u["codepoint"]
    if cp not in by_cp:
        by_cp[cp] = []
    by_cp[cp].append(u)

for cp, items in sorted(by_cp.items()):
    print(f"Codepoint {cp} ({items[0]['char']}): {len(items)} occurrences. Example in [{items[0]['domain']} - {items[0]['id']}]")
