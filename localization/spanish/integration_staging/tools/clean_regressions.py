import os, json, csv

DEV_ROOT = r"C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV"
SPANISH_ROOT = os.path.join(DEV_ROOT, "localization_workspace", "spanish")
L03A_ROOT = os.path.join(SPANISH_ROOT, "integration_staging")
QA_DIR = os.path.join(L03A_ROOT, "qa")
MG_PATH = os.path.join(SPANISH_ROOT, "corpus", "ocarina_messages_es_419.jsonl")
MASTER_PATH = os.path.join(SPANISH_ROOT, "corpus", "MASTER_LOCALIZATION_ES_419.jsonl")

# 1. Update ocarina_messages_es_419.jsonl
regressions = []
records = []
with open(MG_PATH, "r", encoding="utf-8") as f:
    for line in f:
        if not line.strip():
            continue
        d = json.loads(line)
        mid = d["id"]
        txt = d.get("plain_text_es_419", "")
        
        # Regression 1: 0x061B Chinese MT glitch
        if mid == "0x061B" and "when" in txt:
            before = txt
            after = "Stalfos\n¡Atráelo cerca de ti y observa\nsu movimiento con cuidado! ¡Atácalo\ncuando baje la guardia!"
            d["plain_text_es_419"] = after
            regressions.append({
                "id": mid,
                "type": "CORRUPTION_REMOVED",
                "before": before.replace("\n", " "),
                "after": after.replace("\n", " "),
                "reason": "Removed automated MT residual 'when它放松警惕时' -> '¡Atácalo cuando baje la guardia!'"
            })
        # Regression 2: 0x404D grave accent typo crèeme -> créeme
        elif mid == "0x404D" and "crèeme" in txt:
            before = txt
            after = txt.replace("crèeme", "créeme")
            d["plain_text_es_419"] = after
            regressions.append({
                "id": mid,
                "type": "TYPO_FIX",
                "before": before.replace("\n", " "),
                "after": after.replace("\n", " "),
                "reason": "Corrected Italian/French grave accent è to Spanish acute é ('créeme')"
            })
        # Regression 3: 0x088B trailing null byte
        elif mid == "0x088B" and "\x00" in txt:
            before = txt
            after = txt.replace("\x00", "")
            d["plain_text_es_419"] = after
            regressions.append({
                "id": mid,
                "type": "TRAILING_NULL_REMOVED",
                "before": repr(before),
                "after": repr(after),
                "reason": "Removed trailing null byte from plain text message"
            })
        # Unicode dash normalization in dialogue (— and – to -)
        elif "—" in txt or "–" in txt:
            before = txt
            after = txt.replace("—", "-").replace("–", "-")
            d["plain_text_es_419"] = after
            regressions.append({
                "id": mid,
                "type": "DASH_NORMALIZATION",
                "before": before.replace("\n", " "),
                "after": after.replace("\n", " "),
                "reason": "Normalized typographic em/en-dashes to standard N64 font hyphen-minus '-'"
            })

        records.append(d)

with open(MG_PATH, "w", encoding="utf-8") as f:
    for r in records:
        f.write(json.dumps(r, ensure_ascii=False) + "\n")

print(f"Cleaned {len(regressions)} regressions in ocarina_messages_es_419.jsonl.")

# Write QA report: LOCALIZATION_REGRESSIONS_RESOLVED.csv
os.makedirs(QA_DIR, exist_ok=True)
reg_path = os.path.join(QA_DIR, "LOCALIZATION_REGRESSIONS_RESOLVED.csv")
with open(reg_path, "w", encoding="utf-8", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=["id", "type", "before", "after", "reason"])
    writer.writeheader()
    writer.writerows(regressions)
print(f"Wrote {reg_path}")
