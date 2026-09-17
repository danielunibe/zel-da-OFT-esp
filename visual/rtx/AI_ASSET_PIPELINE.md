# AI Asset Pipeline (Offline)

## Overview
This document defines an offline AI asset pipeline for transforming original N64 textures from Ocarina of Time into modern PBR materials. The pipeline operates entirely offline with no runtime AI inference, ensuring deterministic and auditable results.

## Source Structure
- `soh/assets/textures/` - Original N64 texture assets
- `libultraship/src/fast/` - Fast texture processing utilities

## 1. Input Stage
- Source: Original N64 textures extracted from ROM/assets
- Input formats: RGBA, IA, I, and other N64 texture formats
- Metadata: Texture dimensions, format, usage context

## 2. AI Analysis Stage
### 2.1 Material Classification
- Identify material categories: wood, metal, fabric, stone, plastic, etc.
- Detect surface properties from texture patterns and color statistics

### 2.2 Alpha Detection
- Analyze alpha channel for transparency
- Distinguish between cutout (binary alpha) and blended (smooth alpha) materials
- Preserve alpha usage semantics

### 2.3 Baked Lighting Detection
- Identify pre-baked lighting and shadows in textures
- Separate lighting contributions from albedo where possible
- Flag textures with complex baked lighting for manual review

### 2.4 Visual Importance Ranking
- Rank textures by estimated screen space impact
- Prioritize high-visibility assets (UI, main props, character clothing)
- Consider texture tiling frequency and screen coverage

## 3. Candidate Generation
### 3.1 Normal Maps
- Generate normal maps from albedo using offline normal estimation algorithms
- Preserve the pixel-art character of the original textures

### 3.2 Roughness Maps
- Derive roughness values from texture analysis and material classification
- Create smoothness variation maps

### 3.3 Metallic Masks
- Identify metallic surfaces based on color and pattern analysis
- Create binary or smooth metallic masks

### 3.4 Emissive Masks
- Detect emissive elements (fire, magic, glowing surfaces)
- Create emissive masks for later integration

### 3.5 2x Upscale
- Upscale textures to 2x resolution while preserving pixel art character
- Use offline upscaling algorithms (e.g., advanced interpolation, edge-aware scaling)

## 4. Automatic Validation
### 4.1 Dimension Checks
- Verify output dimensions match expected values
- Ensure proper aspect ratio preservation

### 4.2 Format Checks
- Validate output format (PNG, EXR, etc.)
- Check bit depth and color space correctness

### 4.3 Alpha Preservation Checks
- Confirm alpha channel integrity after processing
- Verify alpha blending modes are preserved

## 5. Human/Agent Review Stage
- Review generated maps for quality and correctness
- Validate material properties against original art intent
- Approve or reject candidate generations
- Provide feedback for regeneration if needed

## 6. Approval and Integration
- Integrate approved materials into the Material Registry
- Update material definitions with new PBR maps
- Version control all changes
- Maintain audit trail of modifications

## 7. Tools (Offline Only)
- **Image Processing**: ImageMagick, GIMP, Adobe Photoshop
- **Normal Map Generation**: Blender, Substance Painter, specialized offline tools
- **Texture Analysis**: Custom Python scripts using PIL/Pillow, OpenCV
- **Validation**: Custom validation tools and scripts
- **Upscaling**: Offline upscaling algorithms (e.g., waifu2x, advanced interpolation)

## 8. Safety Guards
- **No automatic repaint**: Original albedo textures are never modified
- **No geometry changes**: Pipeline processes textures only, never mesh data
- **Original albedo always preserved**: Source textures remain untouched and unmodified
- **Additive changes only**: Pipeline generates new maps, never alters existing ones
- **Audit trail**: All operations are logged and reversible