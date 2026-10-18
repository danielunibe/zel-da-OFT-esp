#include "fast/WaterMaterialContract.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>

static bool Finite3(const float v[3]) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

// MaterialRegistry lower-cases the resource path before classification;
// the contract itself requires a lower-cased input.
static std::string Lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

static Fast::WaterProfile Classify(const char* path, bool is2D = false, bool isRect = false,
                                   uint32_t alphaMode = 0u) {
    return Fast::ClassifyWaterDraw(Lower(path), is2D, isRect, alphaMode);
}

int main() {
    using namespace Fast;

    // Ordinals must match the GPU constants in WaterMaterial.hlsli.
    static_assert(WaterClassOrdinal(WaterClass::NONE) == 0u, "NONE ordinal");
    static_assert(WaterClassOrdinal(WaterClass::WATER_SURFACE) == 1u, "SURFACE ordinal");
    static_assert(WaterClassOrdinal(WaterClass::WATER_FLOWING) == 2u, "FLOWING ordinal");
    static_assert(WaterClassOrdinal(WaterClass::WATER_FALLING) == 3u, "FALLING ordinal");
    static_assert(WaterClassOrdinal(WaterClass::WATER_SHALLOW) == 4u, "SHALLOW ordinal");
    static_assert(WaterClassOrdinal(WaterClass::WATER_SPECIAL) == 5u, "SPECIAL ordinal");
    static_assert(WaterClassOrdinal(WaterClass::WATER_UNDERWATER_VIEW) == 6u, "UNDERWATER ordinal");

    // Fresnel: normal incidence is F0; grazing approaches 1; clamped inputs.
    {
        assert(std::abs(WaterFresnelSchlick(1.0f) - kWaterF0) < 1e-5f);
        assert(WaterFresnelSchlick(0.0f) > 0.98f);
        assert(WaterFresnelSchlick(-2.0f) >= kWaterF0);
        assert(WaterFresnelSchlick(5.0f) <= 1.0f);
        assert(std::isfinite(WaterFresnelSchlick(std::numeric_limits<float>::quiet_NaN())));
    }

    // Eta: air->water is 1/1.333; water->air is 1.333.
    {
        assert(std::abs(WaterEta(true) - (1.0f / 1.333f)) < 1e-5f);
        assert(std::abs(WaterEta(false) - 1.333f) < 1e-5f);
    }

    // Beer-Lambert absorption: red dies first (why water reads blue); blue
    // survives longest. A negative path is identity.
    {
        float t[3];
        WaterAbsorb(10.0f, 1.0f, t);
        assert(Finite3(t));
        assert(t[0] < t[1] && t[1] < t[2]);
        assert(t[2] > 0.0f && t[0] >= 0.0f);
        WaterAbsorb(-1.0f, 1.0f, t);
        assert(t[0] == 1.0f && t[1] == 1.0f && t[2] == 1.0f);
        WaterAbsorb(1.0f, -1.0f, t);
        assert(t[0] > 0.0f && t[0] <= 1.0f);
    }

    // Effect overlays (splash, ripple, dust) and excluded assets never become
    // a refractive lake: they keep NONE so the renderer preserves Fast3D.
    for (const char* path : {
             "objects/gameplay_keep/gEffWaterSplash1Tex",
             "objects/gameplay_keep/gEffWaterRippleTex",
             "objects/gameplay_keep/gDust1Tex",
             "objects/gameplay_keep/gWaterMedallionPlatformTex",
             "objects/gameplay_keep/gEffWaterSplash8Tex",
         }) {
        const WaterProfile p = Classify(path);
        // Splash/ripple are effect overlays classified as WATER_SPECIAL with
        // milder optics, never WATER_SURFACE. Medallion is excluded outright.
        if (Lower(path).find("medallion") != std::string::npos) {
            assert(p.cls == WaterClass::NONE);
            assert(p.opticsBlend == 0.0f);
        } else if (Lower(path).find("dust") != std::string::npos) {
            // Dust is not in the aquatic token list at the classification
            // entry point when it arrives as a pure particle name; if it does
            // reach WATER via a broader token it must stay SPECIAL.
            assert(p.cls == WaterClass::NONE || p.cls == WaterClass::WATER_SPECIAL);
            if (p.cls == WaterClass::NONE) assert(p.opticsBlend == 0.0f);
        } else {
            assert(p.cls == WaterClass::WATER_SPECIAL);
            assert(p.opticsBlend < WaterProfileForClass(WaterClass::WATER_SURFACE).opticsBlend + 1e-5f);
        }
    }

    // 2D and rect draws are UI or screen-space effects, never world liquid.
    {
        const char* lake = "scenes/nonmq/spot02_scene/gwater tex";
        assert(Classify(lake, true).cls == WaterClass::NONE);
        assert(Classify(lake, false, true).cls == WaterClass::NONE);
    }

    // Cutout water keeps milder optics so its holes stay honest.
    {
        const WaterProfile opaque = Classify("scenes/nonmq/spot02_scene/gwater tex");
        const WaterProfile cutout = Classify("scenes/nonmq/spot02_scene/gwater tex", false, false, 1u);
        assert(opaque.cls == WaterClass::WATER_SURFACE);
        assert(cutout.cls == WaterClass::WATER_SURFACE);
        assert(cutout.opticsBlend < opaque.opticsBlend);
    }

    // Class families stay distinct so a lake, river and waterfall never
    // collapse into one identical blue sheet.
    {
        const WaterProfile lake = Classify("scenes/nonmq/spot02_scene/gwater tex");
        const WaterProfile river = Classify("scenes/nonmq/spot02_scene/griver watertex");
        const WaterProfile fall = Classify("scenes/nonmq/spot02_scene/gwaterfall tex");
        assert(lake.cls == WaterClass::WATER_SURFACE);
        assert(river.cls == WaterClass::WATER_FLOWING);
        assert(fall.cls == WaterClass::WATER_FALLING);
        assert(fall.flowSpeed > lake.flowSpeed);
        assert(river.flowSpeed > lake.flowSpeed);
        assert(fall.absorption < lake.absorption);
    }

    // Ice, lava, glass and stone masonry never inherit the water model.
    for (const char* path : {
             "objects/gameplay_keep/gIceFloorTex",
             "objects/gameplay_keep/gLavaFloorTex",
             "objects/gameplay_keep/gGlassWindowTex",
             "scenes/nonmq/ddan_scene/gstonewalltex",
             "objects/gameplay_keep/gTorchTex",
         }) {
        const WaterProfile p = Classify(path);
        assert(p.cls == WaterClass::NONE);
        assert(p.opticsBlend == 0.0f);
    }

    // Empty path is unproven.
    assert(Classify("").cls == WaterClass::NONE);

    std::cout << "water_material_contract_test: PASS\n";
    return 0;
}
