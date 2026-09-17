# 16 - Material Rule Strategy (safest -> riskiest)

RANKING for the future V03 MaterialRegistry, based on observed identifier structure:

1. **UI protection veto (always first)** - resources in 07_UI_PROTECTION_LIST.csv are
   excluded before ANY other rule fires. Not a style choice; a correctness requirement.
2. **Exact OTR path / exact symbol** (`__OTR__objects/gameplay_keep/gDekuStickTex`)
   - SAFEST positive rule. Verified uniqueness in this dataset (24,300 unique paths).
3. **Object + resource** (`object_link_child` + `gLinkChildMasterSwordGuardTex`)
   - Safe; narrows scope, protects against future symbol collisions.
4. **Scene + resource** (`scene:tokinoma` + `tokinoma_room_0Tex_00xxxx`)
   - Required for offset-named scene textures; keys are build-fragile, so pair with
     per-build verification. MEDIUM safety.
5. **Resource prefix** (`gEff*`, `gBossDoor*Tex`)
   - Use only for effect families with consistent naming (fire/water/magic FX).
   - Risk: prefix drift (e.g., `gTorchSlug*` starts with Torch but is an enemy).
6. **Heuristic filename** (`*wood*`, `*stone*`)
   - RISKIEST. Useful only as a *proposal generator* feeding the manual review queue,
     never as an automatic rule.

## Recommended composition

- Tier A (ship-ready): UI veto + exact paths for effect families (fire/water FX).
- Tier B (validated per scene): exact/object rules for pilot scenes.
- Tier C (quarantined): prefix + heuristics -> land in 14_MANUAL_REVIEW_QUEUE.csv.

Every rule level below Tier A must keep the classic pipeline as fallback for
UNKNOWN (see MATERIAL_CLASSIFICATION_SPEC.md rule 3).
