#pragma once
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace Fast {
// Only the two explicit DXR profiles may execute rays. Environment switches
// can opt features out, but cannot silently promote a raster profile.
inline bool DxrFeatureAllowed(const char* name, bool defaultValue = true) {
    const char* value = std::getenv(name);
    if (value && std::strcmp(value, "0") == 0) return false;
    if (value && std::strcmp(value, "1") == 0) return true;
    return defaultValue;
}
enum class DxrVisualProfile : int {
    Classic = 0,
    Enhanced = 1,
    HybridRt = 2,
    PathTracedExperimental = 3,
};

constexpr bool DxrProfileSelected(int profile) {
    return profile == static_cast<int>(DxrVisualProfile::HybridRt) ||
           profile == static_cast<int>(DxrVisualProfile::PathTracedExperimental);
}

struct DxrFrameEvidence {
    int frame = -1;
    bool requested = false;
    bool adapterCompatible = false;
    bool tlasValid = false;
    uint64_t dispatchCalls = 0;
    uint64_t signalOutputsWritten = 0;
    uint64_t compositeDraws = 0;

    constexpr bool DispatchExecuted() const {
        return requested && adapterCompatible && tlasValid && dispatchCalls > 0;
    }
    constexpr bool SignalsProduced() const {
        return DispatchExecuted() && signalOutputsWritten > 0;
    }
    constexpr bool CompositionExecuted() const {
        return SignalsProduced() && compositeDraws > 0;
    }
};

struct DxrCaptureEvidence {
    int frame = -1;
    int requestSequence = 0;
    bool dimensionsValid = false;
    bool formatValid = false;
    bool payloadCompleteAndReadable = false;
    bool finiteSignals = false;
    bool requestMatches = false;
};

constexpr bool DxrCaptureSetComplete(const std::array<DxrCaptureEvidence, 8>& captures) {
    const int frame = captures[0].frame;
    const int sequence = captures[0].requestSequence;
    if (frame < 0 || sequence <= 0) return false;
    for (const auto& capture : captures) {
        if (capture.frame != frame || capture.requestSequence != sequence ||
            !capture.dimensionsValid || !capture.formatValid ||
            !capture.payloadCompleteAndReadable || !capture.finiteSignals ||
            !capture.requestMatches) return false;
    }
    return true;
}

constexpr const char* DxrCaptureStageId(int slot) {
    switch (slot) {
        case 0: return "RASTER_HDR";
        case 1: return "COMPOSITE_PRE_TONEMAP";
        case 2: return "RASTER_SCENE_COLOR_COPY";
        case 3: return "POST_ACES_PRE_PRESENT";
        case 4: return "FINAL_SDR_PRE_PRESENT";
        case 5: return "DXR_REFLECTION_RGBA16F";
        case 6: return "DXR_INDIRECT_RGBA16F";
        case 7: return "EMISSIVE_MASK";
        default: return "UNKNOWN";
    }
}

// A successful DXGI Present call confirms API acceptance only. It cannot
// certify that pixels reached a physical display or were visible to a person.
constexpr bool DxrVisualContributionVerified(bool causalAblationEvidence,
                                             const DxrFrameEvidence& frame) {
    return causalAblationEvidence && frame.CompositionExecuted();
}
}
