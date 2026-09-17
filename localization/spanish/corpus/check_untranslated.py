import json
import re

PROPER_NOUNS = {
    "Mq","Gs","Bombchu","Deku","Goron","Zora","Darunia","Medigoron","Dampe",
    "Greg","Sheik","Ganon","Ganondorf","Malon","Impas","Loach","Tailpasaran",
    "Hyrule","Kakariko","Keese","Stalfos","Redead","Gibdos","Wolfos","Lizalfos",
    "Dinolfos","Armos","Beamos","Leever","Skulltula","Barinade","Volvagia",
    "Morpha","Twinrova","Bongo","Hylian","Kokiri","Saria","Epona","Zelda",
    "Nayru","Din","Farore","Rauru","Sheik","Gohma","Dodongo","Jabu",
    "Sage","Link","Talon","Cucco","Anju",
}

for fname in ['rand_016_translated.jsonl','rand_017_translated.jsonl','rand_018_translated.jsonl','rand_019_translated.jsonl']:
    path = r'C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV\localization_workspace\spanish\corpus\\' + fname
    with open(path, 'r', encoding='utf-8') as f:
        for line in f:
            d = json.loads(line.strip())
            t = d['plain_text_es_419']
            words = t.split()
            for w in words:
                clean = re.sub(r'[(),.]', '', w)
                if clean.isascii() and clean.isalpha() and len(clean) > 2 and clean not in PROPER_NOUNS:
                    print(f'{fname}: {d["id"]}: "{w}" in "{t}"')
                    break
