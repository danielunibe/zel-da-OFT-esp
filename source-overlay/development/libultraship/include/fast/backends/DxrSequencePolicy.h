#pragma once

namespace Fast {

enum class DxrSequenceMode {
    None,
    Diagnostic,
    TruthV1,
    TruthV2,
    GateCounter,
};

constexpr DxrSequenceMode SelectDxrSequenceMode(bool diagnostic,
                                                 bool truthV1,
                                                 bool truthV2,
                                                 bool gateCounter) {
    if (truthV2) return DxrSequenceMode::TruthV2;
    if (truthV1) return DxrSequenceMode::TruthV1;
    if (diagnostic && gateCounter) return DxrSequenceMode::GateCounter;
    if (diagnostic) return DxrSequenceMode::Diagnostic;
    return DxrSequenceMode::None;
}

constexpr int DxrSequenceStageCount(DxrSequenceMode mode) {
    switch (mode) {
        case DxrSequenceMode::Diagnostic: return 8;
        case DxrSequenceMode::TruthV1: return 6;
        case DxrSequenceMode::TruthV2: return 4;
        case DxrSequenceMode::GateCounter: return 2;
        case DxrSequenceMode::None: return 0;
    }
    return 0;
}

constexpr int DxrSequenceStageFrames(DxrSequenceMode mode) {
    return mode == DxrSequenceMode::GateCounter ? 1 : 600;
}

constexpr int DxrSequenceDuration(DxrSequenceMode mode) {
    return DxrSequenceStageCount(mode) * DxrSequenceStageFrames(mode);
}

constexpr bool DxrSequenceComplete(DxrSequenceMode mode, int elapsedFrames) {
    return mode != DxrSequenceMode::None && elapsedFrames >= DxrSequenceDuration(mode);
}

constexpr int DxrSequenceStage(DxrSequenceMode mode, int elapsedFrames) {
    if (mode == DxrSequenceMode::None || elapsedFrames < 0 ||
        elapsedFrames >= DxrSequenceDuration(mode)) return -1;
    const int stage = elapsedFrames / DxrSequenceStageFrames(mode);
    switch (mode) {
        case DxrSequenceMode::Diagnostic: return stage;
        case DxrSequenceMode::TruthV1: return 100 + stage;
        case DxrSequenceMode::TruthV2: return 200 + stage;
        case DxrSequenceMode::GateCounter: return 6 + stage;
        case DxrSequenceMode::None: return -1;
    }
    return -1;
}

constexpr bool DxrSequenceCaptureDue(DxrSequenceMode mode, int elapsedFrames) {
    return mode != DxrSequenceMode::None && elapsedFrames >= 0 &&
           elapsedFrames < DxrSequenceDuration(mode) &&
           elapsedFrames % DxrSequenceStageFrames(mode) == 0;
}

} // namespace Fast
