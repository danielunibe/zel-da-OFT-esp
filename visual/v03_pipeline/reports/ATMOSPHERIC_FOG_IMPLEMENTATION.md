# Atmospheric Fog — V03 Implementation

## Integration
Fog is integrated into the ACES post-process pass (single fullscreen draw).

## Depth Buffer
- Backbuffer (fb_id=0) has depth_stencil_srv when rendering directly
- Fog samples depth texture at slot t1 with point sampler
- Linear depth computed in shader

## Fog Parameters (Constant Buffer b2)
```
fogParams.x = 0.3  (fogStart)
fogParams.y = 0.7  (fogRange)
fogColor    = (0.45, 0.55, 0.70, 0.15)  (blue-grey, strength 0.15)
```

## Fog Curve
```hlsl
float linearDepth = fogStart + depth * fogRange;
float fogFactor = saturate(1.0 - exp(-linearDepth * linearDepth));
fogFactor = fogFactor * fogFactor;  // smooth squared falloff
```
- Exponential squared falloff for smooth depth transition
- fogStrength (0.15) keeps fog subtle — preserves original scene intent
- Fog color is scene-appropriate blue-grey, not overpowering

## Enhanced Only
- Fog constant buffer only bound when RunTonemappingPass() is called
- RunTonemappingPass only called when ENHANCED profile active
- CLASSIC: no fog pass, no depth SRV binding, no constant buffer update

## Indoor/Outdoor
- Fog uses raw depth buffer values — same formula everywhere
- Dungeons with short view distances get minimal fog (depth is small)
- Large outdoor areas get more fog (depth is larger)
- Temple of Time vs Hyrule Field distinction comes naturally from scene depth

## Original Scene Intent
- Fog parameters are additive — original N64 fog is already composited in the scene
- Enhanced fog adds atmospheric depth ON TOP of existing fog
- Does not replace or override original fog color/behavior
