# 20 - Final Material Intelligence Report

STATUS: MATERIAL_INTELLIGENCE_COMPLETE

TASK: MATERIAL_INTELLIGENCE_DATASET_01

SOURCE_CHANGED: NO

RUNTIME_CHANGED: NO

LIVE_ROOT_CHANGED: NO

TOTAL_VISUAL_RESOURCES: 24,372

CLASSIFIED_HIGH: 7,541

CLASSIFIED_MEDIUM: 719

CLASSIFIED_LOW: 16,112

UNKNOWN: 15,536

UI_PROTECTED: 6,102

EMISSIVE_CANDIDATES: 358

WATER_CANDIDATES: 237

METAL_CANDIDATES: 163

TEMPLE_OF_TIME: COMPLETE

KOKIRI_FOREST: COMPLETE

HYRULE_FIELD: COMPLETE

MATERIAL_RULES_DATASET: C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV\visual_workspace\material_intelligence_01\17_MATERIAL_RULES_CANDIDATE.json

MANUAL_REVIEW_COUNT: 5442

READY_FOR_MATERIAL_REGISTRY: PARTIAL

BLOCKERS:
- Scene textures are offset-named; surface-level material mapping requires in-engine
  draw capture (not possible read-only) or visual texture inspection (binary OTR
  contents not read).
- Master Quest (GC_MQ_D) and other build configs not covered by GC_NMQ_NTSC_U XML.
- LOW/UNKNOWN majority is expected and correct; registry must ship with classic fallback.

## Deliverables index

| File | Content |
|---|---|
| 01_RESOURCE_SOURCE_MAP.md | Where visual resources live + identifier stability |
| 02_RESOURCE_INVENTORY.csv | 24,372 resources, typed + XML formats |
| 03_MATERIAL_TAXONOMY.md | Category definitions + rules |
| 04_MATERIAL_CLASSIFICATION.csv | Per-resource category, confidence, evidence, flags |
| 05_PBR_DEFAULTS.json | Design defaults per category (NOT calibrated, NOT implemented) |
| 06_SPECIAL_MATERIALS.csv | WATER/LAVA/GLASS/ICE/MAGIC/EMISSIVE/alpha/UI/SPRITE specials |
| 07_UI_PROTECTION_LIST.csv | Resources that must never receive world-space effects |
| 08_EMISSIVE_CANDIDATES.csv | Glow candidates with strength classes |
| 09_WATER_SURFACES.csv | Water surfaces + subtype classification |
| 10_METAL_CANDIDATES.csv | Metal surfaces with partial-object risk |
| 11-13 pilot markdown | Temple of Time / Kokiri Forest / Hyrule Field |
| 14_MANUAL_REVIEW_QUEUE.csv | LOW/ambiguous resources awaiting validation |
| 15_SHARED_RESOURCE_RISKS.md | Global-pool + shared-texture hazards |
| 16_MATERIAL_RULE_STRATEGY.md | Safest-to-riskiest rule keying |
| 17_MATERIAL_RULES_CANDIDATE.json | Machine-readable candidate rules |
| 18_COVERAGE_METRICS.json | Machine-readable coverage |
| 19_COVERAGE_REPORT.md | Coverage analysis |
| scripts/ | The read-only analysis scripts used (reproducible) |

## Explicit non-goals honored

No MaterialRegistry, no renderer edits, no compilation, no RTX work, no game file
modifications. Dataset only.
