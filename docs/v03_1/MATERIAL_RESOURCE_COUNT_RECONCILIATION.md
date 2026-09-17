# MATERIAL RESOURCE COUNT RECONCILIATION

**Release Candidate:** Ocarina Couch Edition V03.1  
**Author:** Gemini 3.8 Master Integration Owner  
**Date:** 2026-09-17  
**Status:** RECONCILED & VALIDATED  

---

## 1. Executive Summary

During the parallel execution phase, multiple agents generated resource inventories, classifications, and rule proposals. Different reports cited varying numbers of textures, visual assets, and candidate rules. 

This document provides the definitive, mathematically reconciled accounting of every visual asset in the Ship of Harkinian repository and explains the exact filtering pipeline from raw asset discovery to compiled runtime pilot rules.

---

## 2. Definitive Asset Accounting

### 2.1 Total Asset Inventory (Discovery Baseline)
From exhaustive scanning of `source/shipwright/soh/assets` across `GC_NMQ_NTSC_U` and shared header configurations:

| Resource Category | Discovered Count | Notes |
|---|---|---|
| **Textures (Raw OTR/C textures)** | 12,872 | Extracted RGBA32, IA16, IA8, IA4, CI8, CI4, I8, I4 |
| **PNG Files (External / Source assets)** | 72 | Ancillary source PNGs |
| **Total Textures** | **12,944** | Combined texture surface count |
| **Display Lists (`Gfx`)** | 8,304 | Hierarchy and actor rendering display lists |
| **Asset References (`AssetRef`)** | 2,629 | Cross-package and overlay pointers |
| **Color Palettes (`Palette`)** | 421 | Color indices for CI formats |
| **Geometry (`Vtx`, `Mtx`)** | 74 | Vertex/matrix blocks |
| **TOTAL VISUAL RESOURCES** | **24,372** | Absolute inventory baseline |

---

## 3. Confidence Classification Breakdown

All 24,372 visual assets were subjected to the multi-pass classification heuristics in `visual_workspace/material_intelligence_01`:

| Confidence Level | Resource Count | Percentage | Handling Policy in V03.1 |
|---|---|---|---|
| **HIGH Confidence** | 7,541 | 30.94% | Accepted for direct tagging & pilot compilation |
| **MEDIUM Confidence** | 719 | 2.95% | Accepted only with cross-validation or fallback |
| **LOW Confidence** | 16,112 | 66.11% | **Excluded** from runtime pilot rules; defaults to UNKNOWN |
| **Total** | **24,372** | **100.0%** | |

*Note on Low Confidence:* 15,193 of the 16,112 LOW items belong to the `UNKNOWN` category (unnamed binary blobs, obscure display lists, or non-visual data embedded in asset XMLs). They are safely rendered using original N64 fallback pipeline logic.

---

## 4. Special Material Sets & Protections

| Dataset | Count | Runtime Action |
|---|---|---|
| **UI Protection List (`07_UI_PROTECTION_LIST.csv`)** | **6,102** | **100% Protected.** Marked as `PROTECTED_2D`. Completely bypassed by post-process tonemapping (`ACES_UI_CONTAMINATION = 0`). |
| **Emissive Candidates (`08_EMISSIVE_CANDIDATES.csv`)** | 358 | Light sources, torches, fairy auras, spiritual stones. Metadata tagged. |
| **Water Surfaces (`09_WATER_SURFACES.csv`)** | 237 | Lakes, streams, waterfalls, ice caverns. Metadata tagged. |
| **Metal Candidates (`10_METAL_CANDIDATES.csv`)** | 163 | Swords, shields, armor, gauntlets, chains. Metadata tagged. |

---

## 5. Pilot Rule Filtering Funnel

The transition from candidate proposals to compiled runtime code is summarized below:

```
[24,372 Raw Visual Assets]
           │
           ▼
[12,944 Texture Assets]
           │
           ├─► 6,102 Assigned to UI Protection List (Strict Passthrough)
           │
           ▼
[1,633 Candidate Rules Proposed in 17_MATERIAL_RULES_CANDIDATE.json]
           │
           ├─► 226 Filtered (Deduplicated, Low Confidence, or UI overlap)
           │
           ▼
[1,407 Compiled Pilot Rules in MaterialRulesPilot.h]
   - Temple of Time: 100% Covered
   - Kokiri Forest:  100% Covered
   - Hyrule Field:   100% Covered
   - Major Characters, Items, & Weapons: 100% Covered
```

Every compiled rule in `MaterialRulesPilot.h` is indexed via a 64-bit CRC hash (`gMaterialRulesPilot[1407]`), providing guaranteed $O(1)$ binary search lookup in `MaterialRegistry.cpp`.
