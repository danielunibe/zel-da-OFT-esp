# Visual feature priority matrix

| Feature | Visual impact | Impl. cost | GPU cost | CPU cost | Compat risk | Art risk | New assets | Source changes | Phase | Rank |
|---|---|---|---|---|---|---|---|---|---|---:|
| Roughness constants | Medium | Low | Low | Low | Low | Low | No | Yes | V03 | 1 |
| Metallic constants | Medium | Low | Low | Low | Low | Medium | No | Yes | V03 | 2 |
| Emissive masks/constants | High selective | Medium | Low-Med | Low | Medium | Medium | Maybe | Yes | V04 | 3 |
| Normal maps | High close-up | Medium-High | Medium | Low | Medium | High | Yes | Yes | V04 | 4 |
| Water shader | High selective | High | Medium | Medium | High | High | Maybe | Yes | V05 | 5 |
| Better shadows | High | High | Medium-High | Medium | High | Medium | No | Yes | V05 | 6 |
| Better lighting | High | High | Medium | Medium | High | High | Maybe | Yes | V05 | 7 |
| AO | Medium | Medium | Medium | Low | Medium | Medium | No | Yes | V05 | 8 |
| Bloom / tonemap / sharpen | Medium | Medium | Medium | Low | Medium | Medium | No | Yes | V08 | 9 |
| Motion blur | Low-Med | High | Medium | Medium | High | High | No | Yes | V08 | 10 |
| Selective upscale | Medium | Medium | Memory | Low | Medium | High | Yes | Maybe | V07 | 11 |
| DLAA | High | High | Medium | Medium | High | Medium | SDK | Yes | V09 | 12 |
| DLSS SR | High | Very High | Variable | Medium | Very High | Medium | SDK | Yes | V09 | 13 |
| Frame Generation | High | Very High | High | High | Very High | High | SDK | Yes | V10 | 14 |
| RT shadows/reflections/AO | High | Very High | High | High | Very High | No | Yes | V10 | 15 |
| Path tracing | Very High | Very High | Very High | High | Very High | Very High | Maybe | Yes | Experimental | 16 |
| RTX Remix Runtime | Uncertain | Very High | Unknown | Unknown | Very High | Very High | Yes | External | Not recommended | 17 |
| RTX Remix offline AI tools | Medium | Medium | Offline | Low | Low runtime | High | Yes | No | V06 | 18 |
