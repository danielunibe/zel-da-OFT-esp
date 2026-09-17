# Visual / RTX roadmap

## V02 — Registry proof of concept

Capture draw context and prove stable identity, classic fallback and UI exclusion in Temple of Time.

## V03 — Enhanced constants

Add opt-in roughness/metallic/emissive constants for a reviewed opaque subset; measure frame-time and memory deltas.

## V04 — PBR Lite pilot

Add authored normal/roughness maps only after validation; preserve original albedo and compare gameplay visibility.

## V05 — Water, shadows, lighting

Treat these as separate feature gates, starting with raster techniques and explicit per-draw classification.

## V06 — Offline asset pipeline

Deterministic candidate generation, validation, review and registry approval; no automatic replacement.

## V07 — Selective upscale

Only source-backed, high-coverage candidates; exclude fonts, HUD and pixel-perfect glyphs.

## V08 — Postprocess

Scene/UI separation first, then neutral controls, subtle bloom/sharpen and optional motion blur.

## V09 — DLAA feasibility

Capture depth, jitter, matrices, motion/reactive data and UI separation before any vendor SDK integration.

## V10 — DXR experiment

Only after a stable enhanced raster path; evaluate D3D12/backend and acceleration-structure scope. Frame generation and path tracing remain experimental.

**Stop rule:** V01 ends here. Await review before V02; do not modify source, assets or runtime as part of this audit.
