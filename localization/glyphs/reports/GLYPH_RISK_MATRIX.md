# Glyph risk matrix

| Risk | Likelihood | Impact | Mitigation |
|---|---:|---:|---|
| No semantic resolver | High | High | Add one registry/resolver before broad renderer work |
| Axis/trigger shown as wrong digital button | High | High | Preserve source kind, direction and threshold |
| Rebinding makes prompt stale | High | High | Resolve at draw/update time per port, invalidate cache |
| Message control code breaks vanilla text | Medium | High | Optional extension and classic fallback |
| Ocarina semantics regress | Medium | High | Exclude Ocarina from pilot |
| Device names are unstable | Medium | Medium | Prefer SDL mapping/type and active mapping metadata |
| Multiple controllers cross-contaminate | Medium | High | Port-scoped resolver and tests |
| New assets inflate package | Low | Medium | Registry/atlas and reuse SoH assets where compatible |

