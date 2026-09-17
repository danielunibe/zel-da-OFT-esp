# Protected original-experience rules

- Preserve original albedo, UVs, alpha, sampler wrap/filter and draw order by default.
- Exclude UI, fonts, glyphs, HUD, sprites, particles, billboards and decals from PBR unless explicitly audited.
- Unknown materials and special combiners always use the classic fallback.
- Do not perform automatic 4K remastering, AI repainting or geometry replacement in V1.
- Every enhanced material must be disableable and must fall back without changing gameplay state.
- Enhanced lighting must not hide or reveal gameplay-critical information.
- Do not infer emissive, metal or water solely from color, filename or texture reuse.
- Preserve transparency and alpha-test semantics; never reorder a translucent draw without evidence.
- Keep classic rendering available for every scene and hardware profile.
- Review a material in its draw context, not as an isolated image.
- Record source identity, combiner, geometry mode, render mode and 2D/3D classification for each approval.
- Treat generated maps as candidates until human/agent review and deterministic validation pass.
- V01 is read-only against source, runtime and installed assets.
