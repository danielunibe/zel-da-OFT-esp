#include "fast/backends/DxrPipelinePolicy.h"
#include "fast/backends/DxrSequencePolicy.h"
#include "fast/Fast3DCombinerReference.h"
#include "fast/MaterialRegistry.h"

#include <array>
#include <cmath>
#include <iostream>
#include <string_view>

namespace {
int failures = 0;

void Check(bool condition, const char* name) {
    if (condition) return;
    std::cerr << "FAIL: " << name << '\n';
    ++failures;
}

void TestProfileSelection() {
    Check(!Fast::DxrProfileSelected(0), "Classic remains raster only");
    Check(!Fast::DxrProfileSelected(1), "Enhanced remains raster only");
    Check(Fast::DxrProfileSelected(2), "Hybrid RT selects DXR");
    Check(Fast::DxrProfileSelected(3), "Path Traced Experimental selects DXR");
    Check(!Fast::DxrProfileSelected(4), "unknown profiles do not select DXR");
}

void TestFast3DMaterialIdentity() {
    Fast::DrawCallInfo base{};
    base.textureHash = 0xA1B2C3D4u;
    base.resourcePath = "textures/shared_tex";
    base.textureHashes[0] = base.textureHash;
    base.textureUsed[0] = true;
    base.combineMode = 0x0000000000001234ull;
    base.primR = base.primG = base.primB = base.primA = 255;
    base.envR = base.envG = base.envB = base.envA = 255;

    const uint64_t baseKey = Fast::Fast3DMaterialStateKey(base);
    Check(baseKey != 0, "Fast3D draw state has a nonzero stable identity");
    Check(Fast::Fast3DMaterialStateKey(base) == baseKey,
          "identical Fast3D material state has a repeatable identity");

    auto changed = base;
    changed.combineMode ^= 0x100000000ull;
    Check(Fast::Fast3DMaterialStateKey(changed) != baseKey,
          "same texture with a different combiner has a distinct material identity");
    changed = base;
    changed.primR = 96;
    Check(Fast::Fast3DMaterialStateKey(changed) != baseKey,
          "primitive tint stays attached to material identity");
    changed = base;
    changed.textureUsed[1] = true;
    changed.textureHashes[1] = 0x55667788u;
    changed.texturePaths[1] = "textures/second_tile";
    Check(Fast::Fast3DMaterialStateKey(changed) != baseKey,
          "second RDP texture tile stays attached to material identity");
    changed = base;
    changed.textureTileModes[0] = 1u;
    Check(Fast::Fast3DMaterialStateKey(changed) != baseKey,
          "tile address/filter state stays attached to material identity");
}

void TestFast3DPrimaryRelightEligibility() {
    using Fast::MaterialType;
    Check(Fast::DxrPrimaryRelightEligible(MaterialType::STONE, 0u, true, true),
          "opaque Fast3D-lit stone can use hybrid relight");
    Check(Fast::DxrPrimaryRelightEligible(MaterialType::CERAMIC, 0u, true, true),
          "eligibility is not restricted to the Temple floor pilot texture");
    Check(!Fast::DxrPrimaryRelightEligible(MaterialType::STONE, 0u, false, true),
          "a draw without G_LIGHTING preserves captured shade");
    Check(!Fast::DxrPrimaryRelightEligible(MaterialType::STONE, 0u, true, false),
          "missing per-vertex Fast3D lighting provenance blocks relight");
    Check(!Fast::DxrPrimaryRelightEligible(MaterialType::FOLIAGE, 2u, true, true),
          "blended foliage keeps raster composition");
    Check(!Fast::DxrPrimaryRelightEligible(MaterialType::GLASS, 0u, true, true),
          "glass keeps its authored transmissive response");
    Check(!Fast::DxrPrimaryRelightEligible(MaterialType::EMISSIVE, 0u, true, true),
          "emitters are not neutralized by primary relight");
    Check(!Fast::DxrPrimaryRelightEligible(MaterialType::UNKNOWN, 0u, true, true),
          "unknown fallback materials are not physically relit");
}

uint64_t EncodeFast3DCycle(uint32_t rgbA, uint32_t rgbB, uint32_t rgbC, uint32_t rgbD,
                           uint32_t alphaA, uint32_t alphaB, uint32_t alphaC, uint32_t alphaD) {
    const uint64_t rgb = (rgbA & 0xfu) | ((uint64_t)(rgbB & 0xfu) << 4u) |
                         ((uint64_t)(rgbC & 0x1fu) << 8u) | ((uint64_t)(rgbD & 7u) << 13u);
    const uint64_t alpha = (alphaA & 7u) | ((uint64_t)(alphaB & 7u) << 3u) |
                           ((uint64_t)(alphaC & 7u) << 6u) | ((uint64_t)(alphaD & 7u) << 9u);
    return rgb | (alpha << 16u);
}

void TestFast3DCombinerReference() {
    Fast::Fast3DCombinerInputs inputs{};
    inputs.texel0 = {0.8f, 0.4f, 0.2f, 0.25f};
    inputs.texel1 = {0.15f, 0.5f, 0.9f, 0.6f};
    inputs.primitive = {0.5f, 1.0f, 0.25f, 0.75f};
    inputs.shade = {0.6f, 0.3f, 0.1f, 0.5f};
    inputs.environment = {0.2f, 0.7f, 0.4f, 0.8f};
    Fast::Fast3DCombinerColor output{};

    // (TEXEL0 - ZERO) * PRIMITIVE + ZERO, alpha passthrough from TEXEL0.
    const uint64_t primitiveModulate = EncodeFast3DCycle(1u, 31u, 3u, 31u, 7u, 7u, 6u, 1u);
    Check(Fast::EvaluateFast3DCombinerReference(primitiveModulate, 0u, inputs, output),
          "primitive-color combiner variant is supported");
    Check(std::abs(output[0] - 0.4f) < 1.0e-6f && std::abs(output[1] - 0.4f) < 1.0e-6f &&
          std::abs(output[2] - 0.05f) < 1.0e-6f,
          "primitive color modulates TEXEL0 without replacing its base color");
    Check(std::abs(output[3] - inputs.texel0[3]) < 1.0e-6f,
          "alpha passthrough preserves the texture coverage input");

    const uint64_t environmentPass = EncodeFast3DCycle(4u, 4u, 0u, 5u, 4u, 4u, 7u, 5u);
    Check(Fast::EvaluateFast3DCombinerReference(environmentPass, 0u, inputs, output) &&
          std::abs(output[1] - inputs.environment[1]) < 1.0e-6f,
          "environment combiner input is preserved independently from physical ambient light");

    // Cycle zero outputs TEXEL0; cycle one outputs TEXEL1. Fast3D packs cycle
    // one at bit 28, matching Interpreter::GfxDpSetCombineMode.
    const uint64_t twoCycleTiles = EncodeFast3DCycle(0u, 0u, 0u, 1u, 7u, 7u, 7u, 1u) |
        (EncodeFast3DCycle(0u, 0u, 0u, 1u, 7u, 7u, 7u, 1u) << 28u);
    Check(Fast::EvaluateFast3DCombinerReference(twoCycleTiles, 1u << 20u, inputs, output),
          "two-cycle dual-texture combiner is supported");
    Check(std::abs(output[0] - inputs.texel1[0]) < 1.0e-6f &&
          std::abs(output[1] - inputs.texel1[1]) < 1.0e-6f,
          "second cycle samples its captured TEXEL1 input");

    const uint64_t shadePass = EncodeFast3DCycle(4u, 4u, 0u, 4u, 4u, 4u, 7u, 4u);
    Check(Fast::EvaluateFast3DCombinerReference(shadePass, 0u, inputs, output) &&
          std::abs(output[0] - inputs.shade[0]) < 1.0e-6f,
          "SHADE input remains available for artistic or pre-lit vertex colors");

    const uint64_t unsupportedLod = EncodeFast3DCycle(1u, 31u, 13u, 31u, 7u, 7u, 7u, 1u);
    Check(!Fast::EvaluateFast3DCombinerReference(unsupportedLod, 0u, inputs, output),
          "unsupported LOD input requests the conservative raster-color fallback");
}

void TestFrameExecutionGates() {
    Fast::DxrFrameEvidence frame{};
    frame.frame = 10;
    frame.requested = true;
    frame.adapterCompatible = true;
    frame.dispatchCalls = 1;
    frame.signalOutputsWritten = 1;
    frame.compositeDraws = 1;
    Check(!frame.DispatchExecuted(), "missing TLAS cannot count as dispatch");
    frame.tlasValid = true;
    Check(frame.DispatchExecuted(), "compatible requested dispatch with TLAS is executed");
    Check(frame.SignalsProduced(), "dispatch with a recorded output signal produces signals");
    Check(frame.CompositionExecuted(), "signal plus composite draw records composition");
    Check(!Fast::DxrVisualContributionVerified(false, frame),
          "dispatch counters and composition alone do not certify visible contribution");
    Check(Fast::DxrVisualContributionVerified(true, frame),
          "causal evidence plus composition can certify contribution");
    frame.adapterCompatible = false;
    Check(!frame.DispatchExecuted(), "incompatible adapter cannot count as executed DXR");
}

void TestCaptureCompleteness() {
    std::array<Fast::DxrCaptureEvidence, 8> captures{};
    for (auto& capture : captures) {
        capture.frame = 42;
        capture.requestSequence = 1;
        capture.dimensionsValid = true;
        capture.formatValid = true;
        capture.payloadCompleteAndReadable = true;
        capture.finiteSignals = true;
        capture.requestMatches = true;
    }
    Check(Fast::DxrCaptureSetComplete(captures), "eight valid captures complete");
    captures[7].frame = 43;
    Check(!Fast::DxrCaptureSetComplete(captures), "mixed capture frames are rejected");
    captures[7].frame = 42;
    captures[3].requestMatches = false;
    Check(!Fast::DxrCaptureSetComplete(captures), "capture request mismatch is rejected");
    captures[3].requestMatches = true;
    captures[5].finiteSignals = false;
    Check(!Fast::DxrCaptureSetComplete(captures), "non-finite signal capture is rejected");
    captures[5].finiteSignals = true;
    captures[0].payloadCompleteAndReadable = false;
    Check(!Fast::DxrCaptureSetComplete(captures), "incomplete payload is rejected");
}

void TestCaptureIdentityAndPresentationLimits() {
    Check(Fast::DxrCaptureStageId(2) == std::string_view("RASTER_SCENE_COLOR_COPY"),
          "raster scene copy is not named as a DXR result");
    Check(Fast::DxrCaptureStageId(4) == std::string_view("FINAL_SDR_PRE_PRESENT"),
          "final backbuffer capture is explicitly pre-present");
    Fast::DxrFrameEvidence frame{};
    frame.requested = frame.adapterCompatible = frame.tlasValid = true;
    frame.dispatchCalls = frame.signalOutputsWritten = frame.compositeDraws = 1;
    Check(!Fast::DxrVisualContributionVerified(false, frame),
          "pre-present capture and DXGI acceptance are not visual certification");
}

void TestSequenceModesAndBounds() {
    using Fast::DxrSequenceMode;
    Check(Fast::SelectDxrSequenceMode(false, true, false, false) == DxrSequenceMode::TruthV1,
          "V1 enables without the legacy diagnostic switch");
    Check(Fast::SelectDxrSequenceMode(true, false, true, false) == DxrSequenceMode::TruthV2,
          "V2 takes precedence and remains preserved");
    Check(Fast::SelectDxrSequenceMode(true, true, false, false) == DxrSequenceMode::TruthV1,
          "V1 takes precedence over the legacy diagnostic sequence");
    Check(Fast::SelectDxrSequenceMode(true, false, false, true) == DxrSequenceMode::GateCounter,
          "gate counter selects its two-frame sequence");
    Check(Fast::SelectDxrSequenceMode(false, false, false, false) == DxrSequenceMode::None,
          "no switch leaves sequences disabled");

    constexpr auto v1 = DxrSequenceMode::TruthV1;
    Check(Fast::DxrSequenceDuration(v1) == 3600, "V1 duration matches six 600-frame stages");
    Check(Fast::DxrSequenceStage(v1, 0) == 100, "V1 starts at OFF");
    Check(Fast::DxrSequenceStage(v1, 599) == 100, "V1 first stage upper boundary");
    Check(Fast::DxrSequenceStage(v1, 600) == 101, "V1 advances exactly at boundary");
    Check(Fast::DxrSequenceStage(v1, 3599) == 105, "V1 last frame is final stage");
    Check(Fast::DxrSequenceStage(v1, 3600) == -1, "V1 terminates at frame 3600");
    Check(Fast::DxrSequenceComplete(v1, 3600), "V1 reports completion at its duration");
    Check(!Fast::DxrSequenceCaptureDue(v1, 599) && Fast::DxrSequenceCaptureDue(v1, 600),
          "V1 capture windows align with executed stage boundaries");

    constexpr auto v2 = DxrSequenceMode::TruthV2;
    Check(Fast::DxrSequenceDuration(v2) == 2400, "V2 duration matches four 600-frame stages");
    Check(Fast::DxrSequenceStage(v2, 2399) == 203, "V2 final stage upper boundary");
    Check(Fast::DxrSequenceStage(v2, 2400) == -1, "V2 terminates at frame 2400");
    constexpr auto gate = DxrSequenceMode::GateCounter;
    Check(Fast::DxrSequenceStage(gate, 0) == 6 && Fast::DxrSequenceStage(gate, 1) == 7,
          "gate counter stages occupy frames 0 and 1");
    Check(Fast::DxrSequenceStage(gate, 2) == -1 && Fast::DxrSequenceComplete(gate, 2),
          "gate counter terminates after its two executed stages");
}
} // namespace

int main() {
    TestProfileSelection();
    TestFast3DMaterialIdentity();
    TestFast3DPrimaryRelightEligibility();
    TestFast3DCombinerReference();
    TestFrameExecutionGates();
    TestCaptureCompleteness();
    TestCaptureIdentityAndPresentationLimits();
    TestSequenceModesAndBounds();
    if (failures != 0) return 1;
    std::cout << "OCZ RTX hotfix policy tests passed\n";
    return 0;
}
