#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the active Player 1 Z glyph resource, or NULL for classic fallback. */
const char* GlyphResolver_GetZTexture(void);

#ifdef __cplusplus
}
#endif
