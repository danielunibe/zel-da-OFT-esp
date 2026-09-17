# G02B — Fallback Validation

## Static fallback evidence

G02's `GlyphResolver_GetZTexture()` returns `NULL` for classic profile, missing deck/controller/button, non-SDL mapping, non-trigger mapping, non-LT trigger, and non-explicit Xbox/XInput device names. `Font_LoadChar` then copies the unchanged `fontTbl[0x84]` N64 Z resource.

| Case | Expected | Runtime result | Gate |
|---|---|---|---|
| CLASSIC profile | N64 Z | Not visually exercised; static path confirmed | PASS_STATIC / NOT_TESTED_RUNTIME |
| UNKNOWN device | N64 Z | No runtime resolver hook exposed | PASS_STATIC / NOT_TESTED_RUNTIME |
| UNSUPPORTED binding | N64 Z | No runtime resolver hook exposed | PASS_STATIC / NOT_TESTED_RUNTIME |
| Explicit Xbox/XInput + Player 1 Z -> LT | LT | Not visually exercised | PASS_BUILD / NOT_TESTED_RUNTIME |

No mapping was disconnected or globally changed. No source fix was required. The fallback behavior is independent of message ID and therefore also applies to the corrected pilot `0x100D`.
