# G02C prestate

Captured before human validation on 2026-09-16.

## Source checkout

Root: `C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV\source\shipwright`

HEAD: `4aaad850bd5540cd77c2d83f3ad348d3b38605b2`

Status before G02C:

```text
 M .gitignore
 M soh/src/code/z_kanfont.c
?? soh/assets/custom/textures/buttons/LTBtn.ppm
?? soh/include/glyph_resolver.h
?? soh/soh/Enhancements/glyphs/
```

Diff stat: `.gitignore` 14 additions; `soh/src/code/z_kanfont.c` 9 additions. The remaining G02-owned files are untracked as shown above. These changes predate G02C and are not modified by this task.

## Executable evidence

- `source/shipwright/x64/Release/soh.exe`: SHA-256 `3282A70E68A53E8535ED7AB8C8E4ECCE56D5B2542A0A09FB9224747B02B6ADA4`
- `runtime/build-test/soh.exe`: SHA-256 `3282A70E68A53E8535ED7AB8C8E4ECCE56D5B2542A0A09FB9224747B02B6ADA4`

The runtime executable is byte-identical to the G02 Release build output and matches the G02B staged build evidence.
