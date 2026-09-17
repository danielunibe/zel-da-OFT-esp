#include "glyph_resolver.h"

/*
 * PLAYABLE_SPANISH_BUILD_01 — Couch Edition
 *
 * G02 dynamic message glyphs are DISABLED FOR STABILITY.
 *
 * The dynamic Xbox glyph path (GlyphResolver -> LTBtn OTR resource swap inside
 * the message text pipeline) was implicated in message-box crashes. Per the
 * PLAYABLE_SPANISH_BUILD_01 policy:
 *
 *   - Physical controller mappings are NOT touched (LT still maps to N64 Z).
 *   - Message boxes render the classic N64 Z glyph until G02 is re-validated.
 *
 * Returning NULL makes every call site fall back to the classic N64 glyph.
 * This stub intentionally has no libultraship includes so the stability of the
 * build never depends on the dynamic controller-detection code.
 */

extern "C" const char* GlyphResolver_GetZTexture(void) {
    return nullptr;
}
