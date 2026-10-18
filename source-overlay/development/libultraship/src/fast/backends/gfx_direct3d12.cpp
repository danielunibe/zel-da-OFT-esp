#if defined(_WIN32)

#include <windows.h>
#include <wrl/client.h>
#include <dxgi1_4.h>
#include <dxgi1_5.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include "fast/backends/gfx_window_manager_api.h"
#include "fast/couch_diagnostics.h"
#include "fast/backends/gfx_direct3d_common.h"
#define DECLARE_GFX_DXGI_FUNCTIONS
#include "fast/backends/gfx_dxgi.h"
#include "fast/backends/gfx_direct3d12.h"
#include "fast/MaterialRegistry.h"
#include "fast/GeometryCapture.h"
#include "ship/Context.h"
#include "ship/config/ConsoleVariable.h"
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <array>
#include <climits>
#include <string>
#include "fast/backends/DxrAccelerationStructureManager.h"
#include "fast/backends/DxrShadowRenderer.h"
#include "fast/backends/DxrSurfaceRenderer.h"
#include "fast/backends/GpuProfilerD3D12.h"
#include "fast/backends/TemporalManager.h"
#include "fast/backends/TemporalTrace.h"
#include "fast/backends/DxrPipelinePolicy.h"
#include "fast/backends/DxrSequencePolicy.h"

// ==== FORENSIC PHASE 2: minimal dependency-free PNG writer (RGBA, stored deflate) ====
#include <spdlog/spdlog.h>
#include <vector>
namespace pngw {
static uint32_t sCrcTable[256];
static bool sCrcInit = false;
static void InitCrc() {
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        sCrcTable[n] = c;
    }
    sCrcInit = true;
}
static uint32_t Crc32Range(const uint8_t* a, size_t la, const uint8_t* b, size_t lb) {
    if (!sCrcInit) InitCrc();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < la; ++i) c = sCrcTable[(c ^ a[i]) & 0xFF] ^ (c >> 8);
    for (size_t i = 0; i < lb; ++i) c = sCrcTable[(c ^ b[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
static void Be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x >> 24)); v.push_back((uint8_t)(x >> 16));
    v.push_back((uint8_t)(x >> 8));  v.push_back((uint8_t)x);
}
static void Chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& payload) {
    Be32(out, (uint32_t)payload.size());
    uint8_t t[4] = { (uint8_t)type[0], (uint8_t)type[1], (uint8_t)type[2], (uint8_t)type[3] };
    out.insert(out.end(), t, t + 4);
    if (!payload.empty()) out.insert(out.end(), payload.begin(), payload.end());
    Be32(out, Crc32Range(t, 4, payload.empty() ? nullptr : payload.data(), payload.size()));
}
static bool WritePng(const char* path, int w, int h, const uint8_t* rgba) {
    if (w <= 0 || h <= 0 || !rgba) return false;
    std::vector<uint8_t> raw;
    raw.reserve((size_t)h * ((size_t)w * 4 + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0); // filter: none
        raw.insert(raw.end(), rgba + (size_t)y * w * 4, rgba + ((size_t)y + 1) * w * 4);
    }
    std::vector<uint8_t> z;
    z.push_back(0x78); z.push_back(0x01);
    size_t off = 0;
    while (off < raw.size()) {
        size_t blk = raw.size() - off;
        if (blk > 65535) blk = 65535;
        z.push_back(off + blk >= raw.size() ? 1 : 0);
        z.push_back((uint8_t)(blk & 0xFF)); z.push_back((uint8_t)((blk >> 8) & 0xFF));
        z.push_back((uint8_t)(~blk & 0xFF)); z.push_back((uint8_t)(((~blk) >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + off, raw.begin() + off + blk);
        off += blk;
    }
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw.size(); ++i) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    Be32(z, (b << 16) | a);

    std::vector<uint8_t> out;
    const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    out.insert(out.end(), sig, sig + 8);
    std::vector<uint8_t> ihdr;
    Be32(ihdr, (uint32_t)w); Be32(ihdr, (uint32_t)h);
    ihdr.push_back(8); ihdr.push_back(6); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    Chunk(out, "IHDR", ihdr);
    Chunk(out, "IDAT", z);
    Chunk(out, "IEND", std::vector<uint8_t>());
    FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") != 0 || !f) return false;
    bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}
} // namespace pngw

namespace forensic {
Fast::GfxRenderingAPIDX12* gSelf = nullptr;
// FORENSIC (phase-2 fix): renderer-side frame counter. TemporalManager::GetFrameCounter()
// stays 0 when COUCH_TEMPORAL=0 (BeginFrame early-returns), so capture gates must not use it.
static int gForensicFrame = 0;
static bool SceneHarnessCaptureRequested();
static int gDxrDiagnosticSequenceStartFrame = -1;
static bool gDxrDiagnosticSequenceComplete = false;

static bool DxrTruthV2SequenceEnabled() {
    // DXR Truth Mode (V2): COUCH_DXR_TRUTH_V2_SEQUENCE=1 arms the 4-stage
    // WORLD_NORMAL -> SYNTHETIC_LAMBERT -> GAME_LIGHT_LAMBERT -> OFF sweep
    // (600 frames each) for V2 evidence. V1 sequence logic is untouched.
    static const bool enabled = [] {
        const char* value = getenv("COUCH_DXR_TRUTH_V2_SEQUENCE");
        return value && strcmp(value, "1") == 0;
    }();
    return enabled;
}

static bool DxrGateCounterSequenceEnabled() {
    static const bool enabled = [] {
        const char* value = getenv("COUCH_DXR_GPU_GATE_COUNTERS");
        return value && strcmp(value, "1") == 0;
    }();
    return enabled;
}

// DXR Truth Mode (V1): COUCH_DXR_TRUTH_SEQUENCE=1 alone arms the six-stage
// OFF -> HIT_MISS -> NORMAL -> DISTANCE -> IDS -> SYNTHETIC_LAMBERT sweep.
// Manual selection (no sequence) uses the TruthMode cvar and remains unchanged.
static bool DxrTruthSequenceEnabled() {
    static const bool enabled = [] {
        const char* value = getenv("COUCH_DXR_TRUTH_SEQUENCE");
        return value && strcmp(value, "1") == 0;
    }();
    return enabled;
}

static bool DxrDiagnosticSwitchEnabled() {
    static const bool enabled = [] {
        const char* value = getenv("COUCH_DXR_DIAGNOSTIC_SEQUENCE");
        return value && strcmp(value, "1") == 0;
    }();
    return enabled;
}

static Fast::DxrSequenceMode DxrSequenceModeFromEnvironment() {
    return Fast::SelectDxrSequenceMode(DxrDiagnosticSwitchEnabled(),
                                       DxrTruthSequenceEnabled(),
                                       DxrTruthV2SequenceEnabled(),
                                       DxrGateCounterSequenceEnabled());
}

static bool DxrDiagnosticSequenceEnabled() {
    return DxrSequenceModeFromEnvironment() != Fast::DxrSequenceMode::None;
}

static int DxrTruthManualView() {
    int mode = 0;
    auto context = Ship::Context::GetInstance();
    auto cvars = context ? context->GetConsoleVariables() : nullptr;
    if (cvars) mode = cvars->GetInteger("gEnhancements.Graphics.DXR.TruthMode", 0);
    const char* harness = getenv("COUCH_DXR_HARNESS");
    if (mode == 0 || (harness && strcmp(harness, "1") == 0)) {
        if (const char* env = getenv("COUCH_DXR_TRUTH_MODE")) {
            char* end = nullptr;
            const long parsed = strtol(env, &end, 10);
            if (end != env && end && *end == '\0' && parsed >= 0 && parsed <= 6)
                mode = static_cast<int>(parsed);
        }
    }
    if (mode < 0 || mode > 6) mode = 0;
    // cvar 0=OFF,1..5 -> DxrTruthView 10..14, 6 -> 15 GAME_LIGHT_LAMBERT (V2).
    // (the single authoritative enum lives in DxrSurfaceRenderer.h; values are
    // restated here to keep this translation unit free of numeric drift by
    // construction).
    return mode == 0 ? 0 : static_cast<int>(Fast::DxrTruthView::PrimaryHitMiss) + (mode - 1);
}

static int DxrDiagnosticTargetScene() {
    static const int targetScene = [] {
        const char* value = getenv("COUCH_DXR_TARGET_SCENE");
        if (!value || !*value) return 0x55;
        char* end = nullptr;
        const long parsed = strtol(value, &end, 0);
        return end != value && end && *end == '\0' && parsed >= 0 && parsed <= 0xFF
            ? static_cast<int>(parsed) : 0x55;
    }();
    return targetScene;
}

static int DxrDiagnosticSequenceStartFrame() {
    return gDxrDiagnosticSequenceStartFrame;
}

static int DxrDiagnosticSequenceStage(int frame) {
    const auto mode = DxrSequenceModeFromEnvironment();
    const int startFrame = DxrDiagnosticSequenceStartFrame();
    if (startFrame < 0) return -1;
    const int elapsed = frame - startFrame;
    return Fast::DxrSequenceStage(mode, elapsed);
}

// FORENSIC (bisection): COUCH_FORENSIC_CAPTURES=0 disables ALL GPU captures/readbacks
// (in-frame copies and separate-cmdlist captures) while keeping stage logging on.
static bool ForensicCapturesEnabled() {
    static const bool enabled = [] {
        const char* e = getenv("COUCH_FORENSIC_CAPTURES");
        return e && strcmp(e, "1") == 0;
    }();
    if (DxrDiagnosticSequenceEnabled() && DxrDiagnosticSequenceStartFrame() < 0) return false;
    auto context=Ship::Context::GetInstance();
    return enabled || (context && context->GetConsoleVariables()->GetInteger("gEnhancements.Graphics.DXR.CaptureFrame",0)!=0);
}

// In-frame readback: persistent buffers copied INSIDE the main cmdlist (flip-discard safe)
struct InFrameReadback {
    Microsoft::WRL::ComPtr<ID3D12Resource> buf;   // READBACK heap, backbuffer-sized
    bool pending = false;                          // copy enqueued, awaiting GPU completion
    int frame = -1;
    const char* stage = nullptr;
    int W = 0, H = 0;                              // backbuffer dims at enqueue time
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT rowPitch = 0;
    UINT bytesPerPixel = 0;
    bool wantPng = true;                           // timeline samples log stats only
};
static constexpr int kInFrameStages = 20;
static InFrameReadback gInFrame[kInFrameStages]; // required passes, controls, GPU SDR comparisons, Navi radiance and point ON/OFF
static Microsoft::WRL::ComPtr<ID3D12Fence> gInFrameFence;
static HANDLE gInFrameEvent = nullptr;
static UINT64 gInFrameFenceVal = 0;
static int gInFrameRasterCaptures = 0;
static int gInFrameCompositeCaptures = 0;
static const int kInFrameMaxCaptures = 8;
// FORENSIC: continuous raster-content timeline. Boot/loading legitimately shows black
// for tens of seconds on this machine, so fixed windows are ambiguous. Instead sample
// the backbuffer every kTimelineStride frames and log nonzero% per sample. The frame
// where content first appears is then directly comparable between A and E.
static const int kTimelineStride = 60;
static const int kTimelineFirst = 300;
static const int kTimelineLast = 12000;
static int gTimelinePngs = 0;
static uint32_t gHarnessCapturedMask = 0;
static uint32_t gHarnessCaptureAttemptMask = 0;
static std::array<std::string, 8> gHarnessCaptureFiles{};
static std::array<Fast::DxrCaptureEvidence, 8> gHarnessCaptureEvidence{};
static std::string gHarnessCaptureRequestSnapshot;
static float ForensicHalfToFloat(uint16_t value) {
    const bool negative = (value & 0x8000u) != 0;
    const uint16_t exponent = (value >> 10) & 0x1Fu;
    const uint16_t mantissa = value & 0x03FFu;
    float result = 0.0f;
    if (exponent == 0x1Fu) {
        result = mantissa == 0 ? std::numeric_limits<float>::infinity()
                               : std::numeric_limits<float>::quiet_NaN();
    } else if (exponent == 0) {
        result = mantissa == 0 ? 0.0f : std::ldexp(static_cast<float>(mantissa), -24);
    } else {
        result = std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f,
                            static_cast<int>(exponent) - 15);
    }
    return negative ? -result : result;
}

static double ForensicReadFloatChannel(const uint8_t* pixel, std::size_t channel, DXGI_FORMAT format) {
    if (format == DXGI_FORMAT_R16_FLOAT || format == DXGI_FORMAT_R16G16_FLOAT) {
        const auto* values = reinterpret_cast<const uint16_t*>(pixel);
        return ForensicHalfToFloat(values[channel]);
    }
    float value = 0.0f;
    std::memcpy(&value, pixel + channel * sizeof(float), sizeof(value));
    return value;
}

static UINT ForensicBytesPerPixel(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM: return 4;
        case DXGI_FORMAT_R16_FLOAT: return 2;
        case DXGI_FORMAT_R16G16_FLOAT: return 4;
        case DXGI_FORMAT_R16G16B16A16_FLOAT: return 8;
        case DXGI_FORMAT_R32_FLOAT: return 4;
        case DXGI_FORMAT_R32G32_FLOAT: return 8;
        case DXGI_FORMAT_R32G32B32A32_FLOAT: return 16;
        default: return 0;
    }
}

static const char* ForensicFormatName(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
        case DXGI_FORMAT_R16_FLOAT: return "R16_FLOAT";
        case DXGI_FORMAT_R16G16_FLOAT: return "R16G16_FLOAT";
        case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
        case DXGI_FORMAT_R32_FLOAT: return "R32_FLOAT";
        case DXGI_FORMAT_R32G32_FLOAT: return "R32G32_FLOAT";
        case DXGI_FORMAT_R32G32B32A32_FLOAT: return "R32G32B32A32_FLOAT";
        default: return "UNKNOWN";
    }
}

static size_t ForensicQuantileIndex(size_t count, double quantile) {
    if (count == 0) return 0;
    return std::min(count - 1, static_cast<size_t>(quantile * static_cast<double>(count - 1)));
}

static bool ForensicTimelineDue(int f) {
    return f >= kTimelineFirst && f <= kTimelineLast && (f % kTimelineStride) == 0;
}
static bool ForensicInCaptureWindow(int f) {
    if (DxrDiagnosticSequenceEnabled() && DxrDiagnosticSequenceStartFrame() < 0) return false;
    if (SceneHarnessCaptureRequested()) return true;
    static int requestedFrame=-1;
    auto context=Ship::Context::GetInstance();
    auto cvars=context ? context->GetConsoleVariables() : nullptr;
    if (cvars && cvars->GetInteger("gEnhancements.Graphics.DXR.CaptureFrame",0)) {
        if (requestedFrame<0) requestedFrame=f;
        if (f<requestedFrame+4) return true;
        cvars->SetInteger("gEnhancements.Graphics.DXR.CaptureFrame",0);
        requestedFrame=-1;
    }
    if (DxrDiagnosticSequenceEnabled())
        return Fast::DxrSequenceCaptureDue(DxrSequenceModeFromEnvironment(),
                                           f - DxrDiagnosticSequenceStartFrame());
    const char* start=getenv("COUCH_CAPTURE_START_FRAME");
    if (start) { const int frame=atoi(start); return frame>0 && f>=frame && f<frame+4; }
    return (f >= 600 && f < 604) || (f >= 2400 && f < 2404);
}
static const int kInFrameStartFrame = 600;

static void ForensicLog(const std::string& line) {
    SPDLOG_INFO(line);
    const char* outputRoot = getenv("COUCH_DXR_OUTPUT");
    const char* harness = getenv("COUCH_DXR_HARNESS");
    if (harness && strcmp(harness, "1") == 0 && outputRoot && *outputRoot) {
        std::filesystem::create_directories(outputRoot);
        std::ofstream ofs(std::filesystem::path(outputRoot) / "harness-backend.log", std::ios::app);
        if (ofs.is_open()) ofs << line << "\n";
        return;
    }
    std::ofstream ofs(couch_diagnostics_path(), std::ios::app);
    if (ofs.is_open()) ofs << line << "\n";
}

static bool SceneHarnessActive() {
    const char* value = std::getenv("COUCH_DXR_HARNESS");
    return value && std::strcmp(value, "1") == 0;
}

static std::filesystem::path SceneHarnessRunRoot() {
    const char* value = std::getenv("COUCH_DXR_OUTPUT");
    return value && *value ? std::filesystem::path(value) : std::filesystem::path();
}

static std::filesystem::path SceneHarnessCaptureRoot() {
    if (SceneHarnessActive()) return SceneHarnessRunRoot() / "captures";
    return std::filesystem::path("logs") / "captures";
}

static bool SceneHarnessCaptureRequested() {
    if (!SceneHarnessActive()) return false;
    const auto root = SceneHarnessRunRoot();
    if (root.empty() || std::filesystem::exists(root / "capture.ack.json")) return false;
    return std::filesystem::exists(root / "capture.request.json");
}

static bool ForensicCaptureWindow(int frame) {
    if (!ForensicCapturesEnabled() || !ForensicInCaptureWindow(frame)) return false;
    return !SceneHarnessActive() || SceneHarnessCaptureRequested();
}

static bool SceneHarnessReadCaptureRequest(std::string& request) {
    if (!SceneHarnessCaptureRequested()) return false;
    std::ifstream requestFile(SceneHarnessRunRoot() / "capture.request.json", std::ios::binary);
    request.assign(std::istreambuf_iterator<char>(requestFile), std::istreambuf_iterator<char>());
    return !request.empty();
}

static bool SceneHarnessNaviIsolation() {
    const char* isolation = std::getenv("COUCH_DXR_NAVI_ISOLATION");
    return CouchNaviPilotActive() && (!isolation || std::strcmp(isolation, "0") != 0);
}

static const char* SceneHarnessStageName(int slot) {
    if (CouchNaviPilotActive() && slot == 17) return "navi_point_direct";
    if (CouchNaviPilotActive() && slot == 18) return "navi_point_off_sdr";
    if (CouchNaviPilotActive() && slot == 19) return "navi_point_on_sdr";
    if (SceneHarnessNaviIsolation()) {
        switch (slot) {
            case 8: return "navi_a_hdr";
            case 9: return "navi_c_hdr";
            case 15: return "navi_d_hdr";
            case 11: return "navi_b_sdr";
            case 13: return "navi_a_sdr";
            case 14: return "navi_c_sdr";
            case 16: return "navi_d_sdr";
        }
    }
    static const char* names[] = {"raster_hdr", "composite_pre_tonemap", "raster_scene_color_copy",
                                  "post_aces_pre_present", "final_sdr_pre_present", "dxr_reflection",
                                  "dxr_indirect", "emissive_mask", "surface_optics_off", "indirect_off",
                                  "comparison_dxr_off", "comparison_dxr_on", "comparison_shadows_only",
                                  "comparison_optics_off", "comparison_indirect_off",
                                  "relight_off", "comparison_relight_off", "navi_point_direct",
                                  "comparison_navi_point_off", "comparison_navi_point_on"};
    return slot >= 0 && slot < kInFrameStages ? names[slot] : "unknown";
}

// Visual-ablation controls are rendered into HDR targets and re-encoded to SDR
// by CaptureDxrComparisonImage. Slots 8/9 are the two original HDR controls;
// relight-off is appended at 15 so every previously published slot number and
// file name stays stable for the existing capture consumers.
static int SceneHarnessAblationSlot(int index) {
    static const int slots[] = {8, 9, 15};
    return index >= 0 && index < 3 ? slots[index] : -1;
}

// Comparison index -> slot. The mapping is not 10+index because slot 15 is
// the relight-off HDR control; comparison_relight_off therefore sits at 16.
static int SceneHarnessComparisonSlot(int index) {
    static const int slots[] = {10, 11, 12, 13, 14, 16, 18, 19};
    return index >= 0 && index < 8 ? slots[index] : -1;
}

static const char* SceneHarnessStageId(int slot) {
    if (SceneHarnessNaviIsolation() && (slot == 8 || slot == 9 || slot == 15))
        return SceneHarnessStageName(slot);
    if (slot == 8) return "SURFACE_OPTICS_OFF";
    if (slot == 9) return "INDIRECT_OFF";
    if (slot == 15) return "RELIGHT_OFF";
    if (slot >= 10 && slot < kInFrameStages) return SceneHarnessStageName(slot);
    return Fast::DxrCaptureStageId(slot);
}

static bool SceneHarnessWriteMetadata(int slot, int frame, const char* format, int width, int height,
                                      const std::string& payloadName) {
    if (!SceneHarnessActive()) return true;
    const auto root = SceneHarnessRunRoot();
    const auto captureRoot = SceneHarnessCaptureRoot();
    std::filesystem::create_directories(captureRoot);
    std::string request;
    if (!SceneHarnessReadCaptureRequest(request)) return false;
    const std::string metadataName = SceneHarnessStageName(slot) + std::string(".meta.json");
    std::ofstream metadata(captureRoot / metadataName, std::ios::binary | std::ios::trunc);
    if (!metadata) return false;
    const char* expectedSpace = std::strcmp(format, "R8G8B8A8_UNORM") == 0 ? "display-encoded-unorm"
                               : "linear-float";
    metadata << "{\"stage\":\"" << SceneHarnessStageId(slot) << "\",\"frame\":" << frame
             << ",\"format\":\"" << format << "\",\"width\":" << width << ",\"height\":" << height
             << ",\"expectedColorSpace\":\"" << expectedSpace << "\",\"payload\":\"" << payloadName
             << "\",\"request\":" << request << "}\n";
    return static_cast<bool>(metadata);
}

static bool SceneHarnessExpectedFormat(int slot, DXGI_FORMAT format) {
    switch (slot) {
        case 0:
        case 1:
        case 2:
        case 5:
        case 6: return format == DXGI_FORMAT_R16G16B16A16_FLOAT;
        case 3:
        case 4: return format == DXGI_FORMAT_R8G8B8A8_UNORM;
        case 7: return format == DXGI_FORMAT_R16_FLOAT || format == DXGI_FORMAT_R32_FLOAT;
        default: return false;
    }
}

static uint32_t SceneHarnessReadBigEndian32(std::istream& input, bool& ok) {
    uint8_t bytes[4]{};
    input.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
    ok = static_cast<bool>(input);
    return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
}

static bool SceneHarnessValidatePng(const std::filesystem::path& path, int width, int height) {
    std::ifstream png(path, std::ios::binary);
    if (!png) return false;
    uint8_t signature[8]{};
    png.read(reinterpret_cast<char*>(signature), sizeof(signature));
    static constexpr uint8_t expectedSignature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (!png || std::memcmp(signature, expectedSignature, sizeof(signature)) != 0) return false;

    bool sawHeader = false;
    bool sawData = false;
    bool sawEnd = false;
    while (png && !sawEnd) {
        bool ok = false;
        const uint32_t length = SceneHarnessReadBigEndian32(png, ok);
        if (!ok || length > (1u << 30)) return false;
        char type[4]{};
        png.read(type, sizeof(type));
        if (!png) return false;
        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (sawHeader || length != 13) return false;
            const uint32_t pngWidth = SceneHarnessReadBigEndian32(png, ok);
            if (!ok) return false;
            const uint32_t pngHeight = SceneHarnessReadBigEndian32(png, ok);
            if (!ok || pngWidth != static_cast<uint32_t>(width) ||
                pngHeight != static_cast<uint32_t>(height)) return false;
            png.seekg(5, std::ios::cur);
            if (!png) return false;
            sawHeader = true;
        } else {
            if (std::memcmp(type, "IDAT", 4) == 0 && length > 0) sawData = true;
            if (std::memcmp(type, "IEND", 4) == 0) {
                if (length != 0) return false;
                sawEnd = true;
            }
            png.seekg(static_cast<std::streamoff>(length), std::ios::cur);
            if (!png) return false;
        }
        png.seekg(4, std::ios::cur); // CRC
        if (!png) return false;
    }
    return sawHeader && sawData && sawEnd;
}

static bool SceneHarnessValidatePayload(int slot, const std::filesystem::path& path,
                                        int width, int height, DXGI_FORMAT format) {
    if (width <= 0 || height <= 0 || !SceneHarnessExpectedFormat(slot, format)) return false;
    if (format == DXGI_FORMAT_R8G8B8A8_UNORM) return SceneHarnessValidatePng(path, width, height);
    const UINT bytesPerPixel = ForensicBytesPerPixel(format);
    if (bytesPerPixel == 0) return false;
    std::error_code error;
    const uint64_t actualSize = std::filesystem::file_size(path, error);
    const uint64_t expectedSize = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * bytesPerPixel;
    std::ifstream payload(path, std::ios::binary);
    return !error && actualSize == expectedSize && payload.good();
}

static int SceneHarnessRequestSequence(const std::string& request) {
    const std::string key = "\"sequence\"";
    const auto keyPosition = request.find(key);
    if (keyPosition == std::string::npos) return 0;
    const auto colon = request.find(':', keyPosition + key.size());
    if (colon == std::string::npos) return 0;
    char* end = nullptr;
    const long value = std::strtol(request.c_str() + colon + 1, &end, 10);
    return end != request.c_str() + colon + 1 && value > 0 && value <= INT_MAX
        ? static_cast<int>(value) : 0;
}

static void SceneHarnessMarkCaptured(int slot, const std::string& payloadName, int frame,
                                     int width, int height, DXGI_FORMAT format,
                                     bool payloadCompleteAndReadable, bool finiteSignals) {
    if (!SceneHarnessActive() || slot < 0 || slot >= 8) return;
    gHarnessCaptureAttemptMask |= 1u << slot;
    std::string request;
    const bool requestReadable = SceneHarnessReadCaptureRequest(request);
    if (gHarnessCaptureRequestSnapshot.empty() && requestReadable) gHarnessCaptureRequestSnapshot = request;
    const int requestSequence = requestReadable ? SceneHarnessRequestSequence(request) : 0;
    Fast::DxrCaptureEvidence sample{};
    sample.frame = frame;
    sample.requestSequence = requestSequence;
    sample.dimensionsValid = width > 0 && height > 0;
    sample.formatValid = SceneHarnessExpectedFormat(slot, format);
    sample.payloadCompleteAndReadable = payloadCompleteAndReadable &&
        SceneHarnessValidatePayload(slot, SceneHarnessCaptureRoot() / payloadName, width, height, format);
    if (format == DXGI_FORMAT_R8G8B8A8_UNORM && sample.payloadCompleteAndReadable) {
        const std::string rawSuffix = "_raw.png";
        if (payloadName.size() < rawSuffix.size() ||
            payloadName.compare(payloadName.size() - rawSuffix.size(), rawSuffix.size(), rawSuffix) != 0) {
            sample.payloadCompleteAndReadable = false;
        } else {
            const std::string previewName = payloadName.substr(0, payloadName.size() - rawSuffix.size()) +
                                            "_opaque_preview.png";
            sample.payloadCompleteAndReadable = SceneHarnessValidatePng(
                SceneHarnessCaptureRoot() / previewName, width, height);
        }
    }
    sample.finiteSignals = finiteSignals;
    sample.requestMatches = requestReadable && requestSequence == 1 &&
        request == gHarnessCaptureRequestSnapshot;
    gHarnessCaptureEvidence[slot] = sample;
    if (sample.dimensionsValid && sample.formatValid && sample.payloadCompleteAndReadable &&
        sample.finiteSignals && sample.requestMatches) {
        gHarnessCapturedMask |= 1u << slot;
        gHarnessCaptureFiles[slot] = payloadName;
    } else {
        gHarnessCaptureFiles[slot].clear();
        ForensicLog("[SCENE-HARNESS][CAPTURE_REJECTED] stage=" + std::string(SceneHarnessStageId(slot)) +
                    " frame=" + std::to_string(frame) +
                    " dimensionsValid=" + std::to_string(sample.dimensionsValid ? 1 : 0) +
                    " formatValid=" + std::to_string(sample.formatValid ? 1 : 0) +
                    " payloadValid=" + std::to_string(sample.payloadCompleteAndReadable ? 1 : 0) +
                    " finiteSignals=" + std::to_string(sample.finiteSignals ? 1 : 0) +
                    " requestMatches=" + std::to_string(sample.requestMatches ? 1 : 0));
    }
    if (gHarnessCaptureAttemptMask != 0xFFu) return;

    const auto root = SceneHarnessRunRoot();
    const bool captureComplete = Fast::DxrCaptureSetComplete(gHarnessCaptureEvidence);
    int captureFrame = gHarnessCaptureEvidence[0].frame;
    const Fast::DxrFrameEvidence dxr = gSelf ? gSelf->GetDxrFrameEvidence() : Fast::DxrFrameEvidence{};
    const bool dxrFrameMatches = dxr.frame == captureFrame;
    const bool dxrDispatchExecuted = dxrFrameMatches && dxr.DispatchExecuted();
    const bool dxrSignalsProduced = dxrFrameMatches && dxr.SignalsProduced();
    const bool dxrCompositionExecuted = dxrFrameMatches && dxr.CompositionExecuted();
    const char* dxrExecutionStatus = dxrDispatchExecuted && dxrSignalsProduced ? "PASS"
        : dxrFrameMatches && dxr.requested ? "PARTIAL" : "NOT_RUN";
    const char* dxrCompositionStatus = dxrCompositionExecuted ? "PASS"
        : dxrFrameMatches && dxr.requested ? "PARTIAL" : "NOT_RUN";
    const bool dxrVisualCertified = Fast::DxrVisualContributionVerified(false, dxr);
    const char* dxrVisualStatus = dxrVisualCertified ? "DXR_VISUAL_VERIFIED"
        : dxrFrameMatches && dxr.requested ? "PARTIAL" : "NOT_RUN";
    std::ofstream ack(root / "capture.ack.json", std::ios::binary | std::ios::trunc);
    if (!ack) return;
    ack << "{\"schema_version\":2,\"sequence\":1,\"result\":\""
        << (captureComplete ? "CAPTURE_COMPLETE" : "PARTIAL") << "\",\"capture_mask\":"
        << gHarnessCapturedMask << ",\"capture_frame\":" << captureFrame << ",\"captures\":{";
    for (int i = 0; i < 8; ++i) {
        if (i) ack << ',';
        ack << "\"" << SceneHarnessStageId(i) << "\":\"";
        if (!gHarnessCaptureFiles[i].empty()) ack << "captures/" << gHarnessCaptureFiles[i];
        ack << "\"";
    }
    ack << "},\"dxr_frame\":{\"frame\":" << dxr.frame
        << ",\"request_matches_capture\":" << (dxrFrameMatches ? "true" : "false")
        << ",\"requested\":" << (dxr.requested ? "true" : "false")
        << ",\"adapter_compatible\":" << (dxr.adapterCompatible ? "true" : "false")
        << ",\"tlas_valid\":" << (dxr.tlasValid ? "true" : "false")
        << ",\"dispatch_calls\":" << dxr.dispatchCalls
        << ",\"dispatch_executed\":" << (dxrDispatchExecuted ? "true" : "false")
        << ",\"signal_outputs_written\":" << dxr.signalOutputsWritten
        << ",\"signals_produced\":" << (dxrSignalsProduced ? "true" : "false")
        << ",\"composite_draws\":" << dxr.compositeDraws
        << ",\"composition_executed\":" << (dxrCompositionExecuted ? "true" : "false")
        << ",\"execution_status\":\"" << dxrExecutionStatus
        << "\",\"composition_status\":\"" << dxrCompositionStatus
        << "\",\"visual_status\":\"" << dxrVisualStatus
        << "\",\"visual_reason\":\"causal_same_frame_ablation_not_run\"}"
        << ",\"presentation_status\":\"PRE_PRESENT_CAPTURE\",\"request\":"
        << (requestReadable ? request : "null") << "}\n";
    ack.close();
    ForensicLog("[SCENE-HARNESS][CAPTURE_ACK] sequence=1 mask=" +
                std::to_string(gHarnessCapturedMask) + " result=" +
                (captureComplete ? "CAPTURE_COMPLETE" : "PARTIAL"));
}

static bool SceneHarnessWriteFloatPayload(int slot, const InFrameReadback& rb, const void* mapped,
                                          std::string& writtenFilename) {
    if (!SceneHarnessActive() || !SceneHarnessCaptureRequested() || !mapped || rb.bytesPerPixel < 2 ||
        (rb.bytesPerPixel % 2) != 0) return false;
    const auto captureRoot = SceneHarnessCaptureRoot();
    std::filesystem::create_directories(captureRoot);
    const char* extension = rb.format == DXGI_FORMAT_R16G16B16A16_FLOAT ? ".rgba16f"
                            : rb.format == DXGI_FORMAT_R16G16_FLOAT ? ".rg16f"
                            : rb.format == DXGI_FORMAT_R16_FLOAT ? ".r16f"
                            : rb.format == DXGI_FORMAT_R32G32B32A32_FLOAT ? ".rgba32f"
                            : rb.format == DXGI_FORMAT_R32G32_FLOAT ? ".rg32f"
                            : rb.format == DXGI_FORMAT_R32_FLOAT ? ".r32f"
                            : nullptr;
    if (!extension) {
        ForensicLog("[SCENE-HARNESS][CAPTURE_UNSUPPORTED_FORMAT] stage=" + std::string(SceneHarnessStageId(slot)) +
                    " format=" + ForensicFormatName(rb.format));
        return false;
    }
    char frameSuffix[32]{};
    snprintf(frameSuffix, sizeof(frameSuffix), "_frame%06d", rb.frame);
    const std::string filename = std::string(SceneHarnessStageName(slot)) + frameSuffix + extension;
    writtenFilename = filename;
    const auto finalPath = captureRoot / filename;
    const auto tmpPath = captureRoot / (filename + ".tmp");
    std::ofstream payload(tmpPath, std::ios::binary | std::ios::trunc);
    const auto* source = static_cast<const std::uint8_t*>(mapped);
    const std::size_t rowBytes = static_cast<std::size_t>(rb.W) * rb.bytesPerPixel;
    for (int y = 0; y < rb.H && payload; ++y) {
        payload.write(reinterpret_cast<const char*>(source + static_cast<std::size_t>(y) * rb.rowPitch),
                      static_cast<std::streamsize>(rowBytes));
    }
    if (!payload) return false;
    payload.flush();
    payload.close();

    std::error_code ec;
    const auto writtenBytes = std::filesystem::file_size(tmpPath, ec);
    const std::size_t expectedBytes = rowBytes * static_cast<std::size_t>(rb.H);
    if (ec || writtenBytes != expectedBytes) {
        ForensicLog("[SCENE-HARNESS][CAPTURE_WRITE_SIZE_MISMATCH] stage=" + std::string(SceneHarnessStageId(slot)) +
                    " written=" + std::to_string(writtenBytes) + " expected=" + std::to_string(expectedBytes));
        std::filesystem::remove(tmpPath, ec);
        return false;
    }
    std::filesystem::rename(tmpPath, finalPath, ec);
    if (ec) {
        ForensicLog("[SCENE-HARNESS][CAPTURE_RENAME_FAILED] stage=" + std::string(SceneHarnessStageId(slot)) +
                    " error=" + ec.message());
        return false;
    }
    if (!SceneHarnessWriteMetadata(slot, rb.frame, ForensicFormatName(rb.format), rb.W, rb.H, filename)) return false;
    return true;
}

static bool ForensicWriteCapturePair(const char* requestedPath, int width, int height,
                                     const std::vector<uint8_t>& rgba) {
    std::string base = requestedPath ? requestedPath : "logs/captures/diagnostic.png";
    const size_t extension = base.rfind(".png");
    if (extension != std::string::npos) base.resize(extension);
    const std::string rawPath = base + "_raw.png";
    const std::string previewPath = base + "_opaque_preview.png";
    const std::string rawTmp = rawPath + ".tmp";
    const std::string previewTmp = previewPath + ".tmp";
    const bool rawWritten = pngw::WritePng(rawTmp.c_str(), width, height, rgba.data());
    std::vector<uint8_t> preview = rgba;
    for (size_t pixel = 3; pixel < preview.size(); pixel += 4) preview[pixel] = 255;
    const bool previewWritten = pngw::WritePng(previewTmp.c_str(), width, height, preview.data());
    if (rawWritten && previewWritten) {
        std::error_code ec;
        std::filesystem::rename(rawTmp, rawPath, ec);
        std::filesystem::rename(previewTmp, previewPath, ec);
    } else {
        std::error_code ec;
        std::filesystem::remove(rawTmp, ec);
        std::filesystem::remove(previewTmp, ec);
    }
    ForensicLog("[COUCH-FORENSIC][CAPTURE_PAIR_WRITTEN] raw=" + rawPath + " opaquePreview=" + previewPath +
        " rawOK=" + std::to_string(rawWritten ? 1 : 0) + " previewOK=" + std::to_string(previewWritten ? 1 : 0));
    return rawWritten && previewWritten;
}

static void DxrDiagnosticSceneGateUpdate(int frame, int sceneId) {
    if (!DxrDiagnosticSequenceEnabled() || gDxrDiagnosticSequenceComplete) return;
    static int stableFrames = 0;
    static int lastObservedScene = -1;
    const int targetScene = DxrDiagnosticTargetScene();
    auto sceneHex = [targetScene]() {
        char value[16]{};
        sprintf_s(value, "%02X", targetScene);
        return std::string(value);
    };
    if (sceneId != targetScene) {
        if (gDxrDiagnosticSequenceStartFrame >= 0) {
            ForensicLog("[COUCH-DXR-FORENSIC][SCENE_GATE] SCENE_CHANGED during diagnostic; sequence aborted frame=" +
                        std::to_string(frame) + " sceneId=" + std::to_string(sceneId));
            gDxrDiagnosticSequenceStartFrame = -1;
        }
        stableFrames = 0;
        if (sceneId != lastObservedScene) {
            ForensicLog("[COUCH-DXR-FORENSIC][SCENE_GATE] WAITING_FOR_TARGET_SCENE target=0x" +
                        sceneHex() + " current=" + std::to_string(sceneId));
            lastObservedScene = sceneId;
        }
        return;
    }
    if (stableFrames == 0) {
        ForensicLog("[COUCH-DXR-FORENSIC][SCENE_GATE] TARGET_SCENE_DETECTED scene=0x" +
                    sceneHex() + " frame=" + std::to_string(frame));
    }
    ++stableFrames;
    lastObservedScene = sceneId;
    if (stableFrames == 120) {
        ForensicLog("[COUCH-DXR-FORENSIC][SCENE_GATE] TARGET_SCENE_STABLE sceneFrames=120 frame=" +
                    std::to_string(frame));
        gDxrDiagnosticSequenceStartFrame = frame;
        ForensicLog("[COUCH-DXR-FORENSIC][SCENE_GATE] RTX_DIAGNOSTIC_ARMED frame=" +
                    std::to_string(frame) + " sceneId=" + std::to_string(sceneId));
    }
    // The policy audit samples NORMAL at +0 and candidate at +1, then
    // completes after the candidate readback window.
    const int completionFrames = Fast::DxrSequenceDuration(DxrSequenceModeFromEnvironment());
    if (gDxrDiagnosticSequenceStartFrame >= 0 &&
        frame - gDxrDiagnosticSequenceStartFrame >= completionFrames) {
        gDxrDiagnosticSequenceComplete = true;
        ForensicLog("[COUCH-DXR-FORENSIC][SCENE_GATE] RTX_DIAGNOSTIC_SEQUENCE_COMPLETE frame=" +
                    std::to_string(frame));
    }
}

// ==== R4-B — Valid-Vertex Canonical Draw protocol ====
// Mode/env helpers are defined in namespace Fast next to LogCouchD3D12.
// COUCH_D3D12_R4B=<mode>: 0=off, 20=C0 solid PS/original states, 21=C1 depth OFF,
// 22=C2 cull OFF, 23=C3 full viewport/scissor, 24=C4 known SV_VertexID VS.
// The canonical draw is captured ONCE (frame>=min frame, area>0, finite) and then
// frozen: every diagnostic pass draws EXACTLY the same vertices.

static void ForensicEnqueueInFrameCopy(ID3D12GraphicsCommandList* cmdList,
                                       ID3D12Device* device,
                                       ID3D12Resource* backbuffer,
                                       int slot, const char* stage, int frame,
                                       D3D12_RESOURCE_STATES stateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET) {
    InFrameReadback& rb = gInFrame[slot];
    if (rb.pending) return;
    const auto captureDesc = backbuffer->GetDesc();
    const UINT bytesPerPixel = ForensicBytesPerPixel(captureDesc.Format);
    if (bytesPerPixel == 0) {
        ForensicLog("[COUCH-FORENSIC][INFRAME_SKIP] stage=" + std::string(stage) +
                    " format=" + std::to_string(static_cast<unsigned>(captureDesc.Format)) +
                    " reason=unsupported_readback_format");
        return;
    }
    const UINT rowPitch = static_cast<UINT>((captureDesc.Width * bytesPerPixel + 255) & ~UINT64(255));
    const UINT64 captureSize = static_cast<UINT64>(rowPitch) * captureDesc.Height;
    if (rb.buf && rb.buf->GetDesc().Width < captureSize) rb.buf.Reset();
    if (!rb.buf) {
        D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rbd = {};
        rbd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rbd.Width = captureSize;
        rbd.Height = 1; rbd.DepthOrArraySize = 1; rbd.MipLevels = 1;
        rbd.SampleDesc.Count = 1; rbd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rbd,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rb.buf)))) return;
    }
    D3D12_RESOURCE_DESC d = backbuffer->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
    fp.Footprint.Width = (UINT)d.Width;
    fp.Footprint.Height = (UINT)d.Height;
    fp.Footprint.Depth = 1;
    fp.Footprint.Format = captureDesc.Format;
    fp.Footprint.RowPitch = rowPitch;
    rb.W = (int)d.Width;
    rb.H = (int)d.Height;
    rb.format = captureDesc.Format;
    rb.rowPitch = rowPitch;
    rb.bytesPerPixel = bytesPerPixel;
    rb.wantPng = (strncmp(stage, "tl_", 3) != 0) || (gTimelinePngs < 3);
    D3D12_RESOURCE_BARRIER b[2] = {};
    b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b[0].Transition.pResource = backbuffer;
    b[0].Transition.StateBefore = stateBefore;
    b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b[1].Transition.pResource = backbuffer;
    b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b[1].Transition.StateAfter = stateBefore;
    b[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    D3D12_TEXTURE_COPY_LOCATION src = {}; src.pResource = backbuffer;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION dst = {}; dst.pResource = rb.buf.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = fp;
    cmdList->ResourceBarrier(1, &b[0]);
    cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    cmdList->ResourceBarrier(1, &b[1]);
    rb.pending = true;
    rb.frame = frame;
    rb.stage = stage;
}

static void ForensicResolveInFrameCaptures(ID3D12Device* device, ID3D12CommandQueue* queue) {
    bool any = false;
    for (auto& rb : gInFrame) if (rb.pending) any = true;
    if (!any) return;
    if (!gInFrameFence) {
        if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gInFrameFence)))) return;
        gInFrameEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        if (!gInFrameEvent) return;
    }
    ++gInFrameFenceVal;
    HRESULT signalResult = queue->Signal(gInFrameFence.Get(), gInFrameFenceVal);
    if (FAILED(signalResult)) { ForensicLog("[COUCH-FORENSIC][READBACK_FENCE_SIGNAL_FAILED] hr=" + std::to_string(static_cast<unsigned long>(signalResult))); return; }
    if (gInFrameFence->GetCompletedValue() < gInFrameFenceVal) {
        if (FAILED(gInFrameFence->SetEventOnCompletion(gInFrameFenceVal, gInFrameEvent))) return;
        if (WaitForSingleObject(gInFrameEvent, 5000) != WAIT_OBJECT_0 ||
            gInFrameFence->GetCompletedValue() < gInFrameFenceVal) {
            ForensicLog("[COUCH-FORENSIC][READBACK_FENCE_TIMEOUT] expected=" + std::to_string(gInFrameFenceVal) +
                " completed=" + std::to_string(gInFrameFence->GetCompletedValue()));
            return;
        }
    }
    ForensicLog("[COUCH-FORENSIC][READBACK_FENCE_COMPLETED] value=" + std::to_string(gInFrameFenceVal) +
        " completed=" + std::to_string(gInFrameFence->GetCompletedValue()));
    static std::vector<uint8_t> rawPairPixels;
    static int rawPairWidth = 0, rawPairHeight = 0, rawPairFrame = -1;
    // Resolve optional controls before required slot 7 writes capture.ack.
    // The request must remain open until their payload/metadata is saved.
    for (int pass = 0; pass < kInFrameStages; ++pass) {
        const int optionalCount = kInFrameStages - 8;
        const int i = pass < optionalCount ? 8 + pass : pass - optionalCount;
        InFrameReadback& rb = gInFrame[i];
        if (!rb.pending || !rb.buf) continue;
        D3D12_RANGE mr = { 0, (SIZE_T)rb.buf->GetDesc().Width };
        void* mapped = nullptr;
        if (SUCCEEDED(rb.buf->Map(0, &mr, &mapped)) && mapped) {
            int W = rb.W, H = rb.H;
            UINT pitch = rb.rowPitch;
            if (rb.format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
                double rgbSum[3] = {};
                double rgbMin[3] = { std::numeric_limits<double>::infinity(),
                                     std::numeric_limits<double>::infinity(),
                                     std::numeric_limits<double>::infinity() };
                double rgbMax[3] = { -std::numeric_limits<double>::infinity(),
                                     -std::numeric_limits<double>::infinity(),
                                     -std::numeric_limits<double>::infinity() };
                uint64_t rgbCount[3] = {};
                uint64_t nanChannels = 0, infChannels = 0, negativeChannels = 0;
                uint64_t above1 = 0, above2 = 0, above4 = 0, above8 = 0;
                double luminanceMax = -std::numeric_limits<double>::infinity();
                std::vector<float> luminance;
                luminance.reserve(static_cast<size_t>(W) * H);
                for (int y = 0; y < H; ++y) {
                    const auto* row = reinterpret_cast<const uint16_t*>(
                        static_cast<const uint8_t*>(mapped) + static_cast<size_t>(y) * pitch);
                    for (int x = 0; x < W; ++x) {
                        const uint16_t* pixel = row + static_cast<size_t>(x) * 4;
                        double channel[3];
                        bool pixelFinite = true;
                        for (int c = 0; c < 4; ++c) {
                            const float value = ForensicHalfToFloat(pixel[c]);
                            if (std::isnan(value)) { ++nanChannels; pixelFinite = false; }
                            else if (std::isinf(value)) { ++infChannels; pixelFinite = false; }
                            else if (c < 3) {
                                channel[c] = value;
                                if (value < 0.0f) ++negativeChannels;
                                rgbMin[c] = std::min(rgbMin[c], static_cast<double>(value));
                                rgbMax[c] = std::max(rgbMax[c], static_cast<double>(value));
                                rgbSum[c] += value;
                                ++rgbCount[c];
                            }
                        }
                        if (!pixelFinite) continue;
                        const double luma = 0.2126 * channel[0] + 0.7152 * channel[1] + 0.0722 * channel[2];
                        if (!std::isfinite(luma)) continue;
                        luminance.push_back(static_cast<float>(luma));
                        luminanceMax = std::max(luminanceMax, luma);
                        if (luma > 1.0) ++above1;
                        if (luma > 2.0) ++above2;
                        if (luma > 4.0) ++above4;
                        if (luma > 8.0) ++above8;
                    }
                }
                const auto quantile = [&luminance](double q) {
                    if (luminance.empty()) return 0.0f;
                    const size_t index = ForensicQuantileIndex(luminance.size(), q);
                    std::nth_element(luminance.begin(), luminance.begin() + index, luminance.end());
                    return luminance[index];
                };
                const float p50 = quantile(0.50);
                const float p95 = quantile(0.95);
                const float p99 = quantile(0.99);
                const double pixelCount = static_cast<double>(W) * H;
                char stats[1536];
                snprintf(stats, sizeof(stats),
                    "[COUCH-FORENSIC][HDR_STATS] stage=%s frame=%d format=R16G16B16A16_FLOAT space=linear "
                    "R[min=%.6f max=%.6f mean=%.6f] G[min=%.6f max=%.6f mean=%.6f] "
                    "B[min=%.6f max=%.6f mean=%.6f] "
                    "luma[mean=%.6f p50=%.6f p95=%.6f p99=%.6f max=%.6f] "
                    "pixels_gt_1=%llu pixels_gt_2=%llu pixels_gt_4=%llu pixels_gt_8=%llu "
                    "nan_channels=%llu inf_channels=%llu nonfinite_luma_pixels=%llu total_pixels=%.0f",
                    rb.stage, rb.frame,
                    rgbCount[0] ? rgbMin[0] : 0.0, rgbCount[0] ? rgbMax[0] : 0.0, rgbCount[0] ? rgbSum[0] / rgbCount[0] : 0.0,
                    rgbCount[1] ? rgbMin[1] : 0.0, rgbCount[1] ? rgbMax[1] : 0.0, rgbCount[1] ? rgbSum[1] / rgbCount[1] : 0.0,
                    rgbCount[2] ? rgbMin[2] : 0.0, rgbCount[2] ? rgbMax[2] : 0.0, rgbCount[2] ? rgbSum[2] / rgbCount[2] : 0.0,
                    luminance.empty() ? 0.0 : std::accumulate(luminance.begin(), luminance.end(), 0.0) / luminance.size(),
                    p50, p95, p99, luminance.empty() ? 0.0 : luminanceMax,
                    static_cast<unsigned long long>(above1), static_cast<unsigned long long>(above2),
                    static_cast<unsigned long long>(above4), static_cast<unsigned long long>(above8),
                    static_cast<unsigned long long>(nanChannels), static_cast<unsigned long long>(infChannels),
                    static_cast<unsigned long long>(static_cast<uint64_t>(pixelCount) - luminance.size()), pixelCount);
                ForensicLog(stats);
                if (SceneHarnessActive()) {
                    std::string floatFilename;
                    const bool payloadWritten = SceneHarnessWriteFloatPayload(i, rb, mapped, floatFilename);
                    const auto statsPath = SceneHarnessCaptureRoot() /
                        (std::string(SceneHarnessStageName(i)) + "_frame" + std::to_string(rb.frame) + ".stats.json");
                    std::ofstream statsJson(statsPath, std::ios::binary | std::ios::trunc);
                    std::string request;
                    bool statsWritten = false;
                    if (statsJson && SceneHarnessReadCaptureRequest(request)) {
                        statsJson << "{\"stage\":\"" << SceneHarnessStageId(i) << "\",\"frame\":" << rb.frame
                                  << ",\"format\":\"R16G16B16A16_FLOAT\",\"space\":\"linear\",\"width\":"
                                  << W << ",\"height\":" << H
                                  << ",\"rgb_min\":[" << (rgbCount[0] ? rgbMin[0] : 0.0) << "," << (rgbCount[1] ? rgbMin[1] : 0.0) << "," << (rgbCount[2] ? rgbMin[2] : 0.0) << "]"
                                  << ",\"rgb_max\":[" << (rgbCount[0] ? rgbMax[0] : 0.0) << "," << (rgbCount[1] ? rgbMax[1] : 0.0) << "," << (rgbCount[2] ? rgbMax[2] : 0.0) << "]"
                                  << ",\"rgb_mean\":[" << (rgbCount[0] ? rgbSum[0] / rgbCount[0] : 0.0) << "," << (rgbCount[1] ? rgbSum[1] / rgbCount[1] : 0.0) << "," << (rgbCount[2] ? rgbSum[2] / rgbCount[2] : 0.0) << "]"
                                  << ",\"luma_mean\":" << (luminance.empty() ? 0.0 : std::accumulate(luminance.begin(), luminance.end(), 0.0) / luminance.size())
                                  << ",\"luma_p50\":" << p50
                                  << ",\"luma_p95\":" << p95 << ",\"luma_p99\":" << p99
                                  << ",\"luma_max\":" << (luminance.empty() ? 0.0 : luminanceMax)
                                  << ",\"pixels_above_1\":" << above1 << ",\"pixels_above_2\":" << above2
                                  << ",\"pixels_above_4\":" << above4 << ",\"pixels_above_8\":" << above8
                                  << ",\"negative_channels\":" << negativeChannels
                                  << ",\"nan_channels\":" << nanChannels << ",\"inf_channels\":" << infChannels
                                  << ",\"finite_signals\":" << ((nanChannels == 0 && infChannels == 0) ? "true" : "false")
                                  << ",\"payload_complete\":" << (payloadWritten ? "true" : "false")
                                  << ",\"request\":" << request << "}\n";
                        statsJson.flush();
                        statsWritten = static_cast<bool>(statsJson);
                    }
                    SceneHarnessMarkCaptured(i, floatFilename, rb.frame, W, H, rb.format,
                                             payloadWritten && statsWritten,
                                             nanChannels == 0 && infChannels == 0);
                }
                D3D12_RANGE er{ 0, 0 };
                rb.buf->Unmap(0, &er);
                rb.pending = false;
                continue;
            }
            if (rb.format != DXGI_FORMAT_R8G8B8A8_UNORM) {
                if (SceneHarnessActive()) {
                    std::string floatFilename;
                    const bool payloadWritten = SceneHarnessWriteFloatPayload(i, rb, mapped, floatFilename);
                    const bool halfPrecision = rb.format == DXGI_FORMAT_R16_FLOAT ||
                        rb.format == DXGI_FORMAT_R16G16_FLOAT;
                    const std::size_t bytesPerChannel = halfPrecision ? sizeof(uint16_t) : sizeof(float);
                    const std::size_t channelCount = rb.bytesPerPixel / bytesPerChannel;
                    double minimum = std::numeric_limits<double>::infinity();
                    double maximum = -std::numeric_limits<double>::infinity();
                    double sum = 0.0;
                    uint64_t finiteCount = 0, nanCount = 0, infCount = 0;
                    for (int y = 0; y < H; ++y) {
                        const auto* row = static_cast<const uint8_t*>(mapped) + static_cast<size_t>(y) * pitch;
                        for (int x = 0; x < W; ++x) {
                            const auto* pixel = row + static_cast<size_t>(x) * rb.bytesPerPixel;
                            for (std::size_t c = 0; c < channelCount; ++c) {
                                const double value = ForensicReadFloatChannel(pixel, c, rb.format);
                                if (std::isnan(value)) ++nanCount;
                                else if (std::isinf(value)) ++infCount;
                                else { minimum = std::min(minimum, value); maximum = std::max(maximum, value); sum += value; ++finiteCount; }
                            }
                        }
                    }
                    const auto statsPath = SceneHarnessCaptureRoot() /
                        (std::string(SceneHarnessStageName(i)) + "_frame" + std::to_string(rb.frame) + ".stats.json");
                    std::ofstream statsJson(statsPath, std::ios::binary | std::ios::trunc);
                    std::string request;
                    bool statsWritten = false;
                    if (statsJson) {
                        const bool requestRead = SceneHarnessReadCaptureRequest(request);
                        statsJson << "{\"stage\":\"" << SceneHarnessStageId(i) << "\",\"frame\":" << rb.frame
                                  << ",\"format\":\"" << ForensicFormatName(rb.format) << "\",\"space\":\"linear\",\"width\":"
                                  << W << ",\"height\":" << H << ",\"min\":" << (finiteCount ? minimum : 0.0)
                                  << ",\"max\":" << (finiteCount ? maximum : 0.0)
                                  << ",\"mean\":" << (finiteCount ? sum / finiteCount : 0.0)
                                  << ",\"nan_channels\":" << nanCount << ",\"inf_channels\":" << infCount
                                  << ",\"finite_signals\":" << ((nanCount == 0 && infCount == 0) ? "true" : "false")
                                  << ",\"payload_complete\":" << (payloadWritten ? "true" : "false");
                        if (requestRead) statsJson << ",\"request\":" << request;
                        statsJson << "}\n";
                        statsJson.flush();
                        statsWritten = static_cast<bool>(statsJson) && requestRead;
                    }
                    SceneHarnessMarkCaptured(i, floatFilename, rb.frame, W, H, rb.format,
                                             payloadWritten && statsWritten,
                                             nanCount == 0 && infCount == 0);
                }
                D3D12_RANGE er{ 0, 0 };
                rb.buf->Unmap(0, &er);
                rb.pending = false;
                continue;
            }
            UINT nz = 0, tot = 0; UINT mx = 0;
            for (int y = 0; y < H; y += 4) {
                const uint8_t* row = (const uint8_t*)mapped + (size_t)y * pitch;
                for (int x = 0; x < (int)W * 4; x += 8) {
                    UINT r = row[x], g = row[x+1], b = row[x+2];
                    UINT v = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b);
                    tot++; if (v) nz++; if (v > mx) mx = v;
                }
            }
            if (SceneHarnessActive()) std::filesystem::create_directories(SceneHarnessCaptureRoot());
            else { CreateDirectoryA("logs", nullptr); CreateDirectoryA("logs\\captures", nullptr); }
            bool wrote = false;
            if (strncmp(rb.stage, "tl_", 3) == 0) {
                // Timeline sample: stats only (PNG for the first samples with content)
                if (nz > 0 && gTimelinePngs < 3) {
                    char path[160];
                    snprintf(path, sizeof(path), "logs/captures/inframe_%s_frame%06d.png", rb.stage, rb.frame);
                    std::vector<uint8_t> rgba((size_t)W * H * 4);
                    for (int y = 0; y < H; ++y) {
                        const uint8_t* row = (const uint8_t*)mapped + (size_t)y * pitch;
                        memcpy(rgba.data() + (size_t)y * W * 4, row, (size_t)W * 4);
                    }
                    wrote = ForensicWriteCapturePair(path, W, H, rgba);
                    ++gTimelinePngs;
                }
                char sb[256];
                snprintf(sb, sizeof(sb), "[COUCH-FORENSIC][TIMELINE] frame=%d nonzero=%.2f%% max=%u png=%d",
                    rb.frame, tot ? 100.0 * nz / tot : 0.0, mx, (int)wrote);
                ForensicLog(sb);
                D3D12_RANGE er{ 0, 0 };
                rb.buf->Unmap(0, &er);
                rb.pending = false;
                continue;
            }
            char path[512];
            if (SceneHarnessActive()) {
                const auto pathValue = (SceneHarnessCaptureRoot() /
                    (std::string(SceneHarnessStageName(i)) + "_frame" + std::to_string(rb.frame) + ".png")).string();
                snprintf(path, sizeof(path), "%s", pathValue.c_str());
            } else {
                snprintf(path, sizeof(path), "logs/captures/inframe_%s_frame%06d.png", rb.stage, rb.frame);
            }
            std::vector<uint8_t> rgba((size_t)W * H * 4);
            for (int y = 0; y < H; ++y) {
                const uint8_t* row = (const uint8_t*)mapped + (size_t)y * pitch;
                memcpy(rgba.data() + (size_t)y * W * 4, row, (size_t)W * 4);
            }
            if (i == 3 || i == 4) {
                uint64_t alphaZero = 0, alphaPartial = 0, alphaFull = 0, zeroRgba = 0;
                for (size_t p = 0; p < rgba.size(); p += 4) {
                    const uint8_t r = rgba[p], g = rgba[p + 1], b = rgba[p + 2], a = rgba[p + 3];
                    if (a == 0) { ++alphaZero; if (r == 0 && g == 0 && b == 0) ++zeroRgba; }
                    else if (a == 255) ++alphaFull;
                    else ++alphaPartial;
                }
                char stats[640];
                const double px = static_cast<double>(W) * H;
                snprintf(stats, sizeof(stats),
                    "[COUCH-FORENSIC][FINAL_ALPHA] capture=%s frame=%d alpha0=%llu(%.4f%%) alphaPartial=%llu(%.4f%%) alpha255=%llu(%.4f%%) rgba0000=%llu containsTransparent=%s",
                    i == 3 ? "capture_raw_readback_pre_present" : "capture_backbuffer_pre_present", rb.frame,
                    (unsigned long long)alphaZero, px ? 100.0 * alphaZero / px : 0.0,
                    (unsigned long long)alphaPartial, px ? 100.0 * alphaPartial / px : 0.0,
                    (unsigned long long)alphaFull, px ? 100.0 * alphaFull / px : 0.0,
                    (unsigned long long)zeroRgba, alphaZero ? "YES" : "NO");
                ForensicLog(stats);
                if (SceneHarnessActive()) {
                    // These are both command-list checkpoints before the DXGI Present call.
                    rawPairPixels = rgba;
                    rawPairWidth = W; rawPairHeight = H; rawPairFrame = rb.frame;
                } else if (i == 3) {
                    rawPairPixels = rgba;
                    rawPairWidth = W; rawPairHeight = H; rawPairFrame = rb.frame;
                    // Frame suffix keeps every capture window (e.g. each DXR
                    // Truth stage) instead of overwriting a single file.
                    snprintf(path, sizeof(path), "logs/captures/capture_raw_readback_frame%06d.png", rb.frame);
                } else {
                    const bool same = rawPairFrame == rb.frame && rawPairWidth == W && rawPairHeight == H && rawPairPixels == rgba;
                    ForensicLog(std::string("[COUCH-FORENSIC][CAPTURE_COMPARE] frame=") + std::to_string(rb.frame) +
                        " pixelWiseEqual=" + (same ? "YES" : "NO") + " rawFrame=" + std::to_string(rawPairFrame));
                    snprintf(path, sizeof(path), "logs/captures/capture_backbuffer_pre_present_frame%06d.png", rb.frame);
                }
            }
            wrote = ForensicWriteCapturePair(path, W, H, rgba);
            if (SceneHarnessActive() && (i == 3 || i == 4 || i >= 10)) {
                const std::string payloadName = std::string(SceneHarnessStageName(i)) + "_frame" +
                                                std::to_string(rb.frame) + "_raw.png";
                const bool metadataWritten = wrote && SceneHarnessWriteMetadata(
                    i, rb.frame, ForensicFormatName(rb.format), W, H, payloadName);
                SceneHarnessMarkCaptured(i, payloadName, rb.frame, W, H, rb.format,
                                         wrote && metadataWritten, true);
            }
            char sb[256];
            snprintf(sb, sizeof(sb), "[COUCH-FORENSIC][INFRAME] stage=%s frame=%d nonzero=%.2f%% max=%u png=%d",
                rb.stage, rb.frame, tot ? 100.0 * nz / tot : 0.0, mx, (int)wrote);
            ForensicLog(sb);
            D3D12_RANGE er = { 0, 0 };
            rb.buf->Unmap(0, &er);
        }
        rb.pending = false;
    }
}
} // namespace forensic

void ForensicGlobalCompositeCapture(int frame) {
    if (forensic::gSelf) forensic::gSelf->ForensicCompositeCapture(frame);
}


namespace forensic {
struct Captures {
    bool doneRaster = false, doneMotion = false, doneComposite = false;
    int nRaster = 0, nMotion = 0, nComposite = 0;
    const int max = 6; // captures per stage
    bool lastFrameTemporal = false; // previous frame executed temporal GPU path
};
static Captures g;
static std::string activeInterval; // "" | "TEMPORAL_MOTION" | "TEMPORAL_REPROJECTION"
}

#include "fast/interpreter.h"

#include "ship/Context.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/window/Window.h"
#include "ship/window/gui/Gui.h"
#include "spdlog/spdlog.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <functional>

using Microsoft::WRL::ComPtr;

namespace Fast {

typedef HRESULT(WINAPI* PFN_D3D12_CREATE_DEVICE)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
typedef HRESULT(WINAPI* PFN_D3D12_GET_DEBUG_INTERFACE)(REFIID, void**);
typedef HRESULT(WINAPI* PFN_D3D12_SERIALIZE_ROOT_SIGNATURE)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);

static PFN_D3D12_CREATE_DEVICE sD3D12CreateDevice = nullptr;
static PFN_D3D12_GET_DEBUG_INTERFACE sD3D12GetDebugInterface = nullptr;
static PFN_D3D12_SERIALIZE_ROOT_SIGNATURE sD3D12SerializeRootSignature = nullptr;

static HMODULE sD3D12Module = nullptr;

static void LogCouchD3D12(const std::string& line) {
    SPDLOG_INFO(line);
    if (forensic::SceneHarnessActive() && !forensic::SceneHarnessRunRoot().empty()) {
        std::filesystem::create_directories(forensic::SceneHarnessRunRoot());
        std::ofstream ofs(forensic::SceneHarnessRunRoot() / "harness-d3d12.log", std::ios::app);
        if (ofs.is_open()) ofs << line << "\n";
        return;
    }
    std::ofstream ofs(couch_diagnostics_path(), std::ios::app);
    if (ofs.is_open()) {
        ofs << line << "\n";
    }
}

void GfxRenderingAPIDX12::CaptureDxrComparisonImage(unsigned index, ID3D12Resource* source,
                                                  D3D12_RESOURCE_STATES sourceState, int frame) {
    const char* comparison = std::getenv("COUCH_DXR_VISUAL_ABLATION");
    const char* mode = std::getenv("COUCH_DXR_MODE");
    if (index >= 8 || !source || !mCommandListOpen || !forensic::SceneHarnessCaptureRequested() ||
        !comparison || std::strcmp(comparison, "1") != 0 || !mode || std::strcmp(mode, "NORMAL") != 0) return;
    const int comparisonSlot = forensic::SceneHarnessComparisonSlot(int(index));
    if (comparisonSlot < 0) return;
    const auto sourceDesc = source->GetDesc();
    if (sourceDesc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT || sourceDesc.SampleDesc.Count != 1 ||
        !CreateFramebufferCopyPipeline(DXGI_FORMAT_R8G8B8A8_UNORM)) return;
    if (!mComparisonRtvHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; heap.NumDescriptors = 8;
        if (FAILED(mDevice->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&mComparisonRtvHeap)))) return;
    }
    auto& output = mComparisonSdr[index];
    if (!output || output->GetDesc().Width != sourceDesc.Width || output->GetDesc().Height != sourceDesc.Height) {
        if (output) mFrameContexts[mFrameIndex].stagingBuffers.push_back(output);
        output.Reset();
        auto desc = sourceDesc;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_CLEAR_VALUE clear{}; clear.Format = desc.Format; clear.Color[3] = 1;
        if (FAILED(mDevice->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&output)))) return;
    }
    auto rtv = mComparisonRtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += index * mRtvDescriptorSize;
    mDevice->CreateRenderTargetView(output.Get(), nullptr, rtv);
    const auto sourceSrv = AllocateSrvDescriptor();
    auto cpu = mSrvHeap->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += size_t(sourceSrv) * mSrvDescriptorSize;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = sourceDesc.Format; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels = 1;
    mDevice->CreateShaderResourceView(source, &srv, cpu);
    const auto depthSrv = AllocateSrvDescriptor();
    auto depthCpu = mSrvHeap->GetCPUDescriptorHandleForHeapStart();
    depthCpu.ptr += size_t(depthSrv) * mSrvDescriptorSize;
    srv.Format = DXGI_FORMAT_R32_FLOAT;
    ID3D12Resource* depth = FramebufferDepth(mCurrentFramebuffer);
    mDevice->CreateShaderResourceView(depth, &srv, depthCpu);
    auto& depthState = mCurrentFramebuffer == 0 ? mBackbufferDepthState : mFrameBuffers.at(mCurrentFramebuffer).depthState;
    const auto previousDepth = depthState;
    if (depth) TransitionResource(depth, previousDepth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    TransitionResource(source, sourceState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ID3D12DescriptorHeap* heaps[] = {mSrvHeap.Get()};
    mCommandList->SetDescriptorHeaps(1, heaps);
    mCommandList->SetGraphicsRootSignature(mFramebufferCopyRoot.Get());
    mCommandList->SetPipelineState(mFramebufferCopyPso[0].Get());
    auto gpu = mSrvHeap->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += size_t(sourceSrv) * mSrvDescriptorSize;
    mCommandList->SetGraphicsRootDescriptorTable(0, gpu);
    gpu = mSrvHeap->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += size_t(depthSrv) * mSrvDescriptorSize;
    mCommandList->SetGraphicsRootDescriptorTable(1, gpu);
    // Reuse both the production shader and the exact parameters published
    // before BeginUiOverlayPass: linear FP16 -> ACES -> scene fog -> gamma22.
    const float constants[] = {0, 0, 1, 1, 1, 1, 0, 0,
        mFramebufferFog[0], mFramebufferFog[1], mFramebufferFog[2], mFramebufferFog[3],
        mFramebufferFog[4], mFramebufferFog[5], 0, 0};
    mCommandList->SetGraphicsRoot32BitConstants(2, 16, constants, 0);
    mCommandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT viewport{0, 0, float(sourceDesc.Width), float(sourceDesc.Height), 0, 1};
    D3D12_RECT scissor{0, 0, LONG(sourceDesc.Width), LONG(sourceDesc.Height)};
    mCommandList->RSSetViewports(1, &viewport); mCommandList->RSSetScissorRects(1, &scissor);
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mCommandList->DrawInstanced(3, 1, 0, 0);
    TransitionResource(source, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, sourceState);
    if (depth) TransitionResource(depth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, previousDepth);
    mFrameContexts[mFrameIndex].retiredSrvIndices.push_back(sourceSrv);
    mFrameContexts[mFrameIndex].retiredSrvIndices.push_back(depthSrv);
    forensic::ForensicEnqueueInFrameCopy(mCommandList.Get(), mDevice.Get(), output.Get(),
        comparisonSlot, forensic::SceneHarnessStageName(comparisonSlot), frame, D3D12_RESOURCE_STATE_RENDER_TARGET);
    LogCouchD3D12("[COUCH-DXR-COMPARISON] frame=" + std::to_string(frame) + " stage=" +
        forensic::SceneHarnessStageName(comparisonSlot) +
        " source=GPU sameCommandList=1 tone=PRODUCTION_FRAMEBUFFER_ACES_GAMMA22 sameToneParameters=1 hud=EXCLUDED");
    RestoreRasterState();
}

static bool DxrVerboseDiagnostics() {
    static const bool enabled = [] {
        const char* value = std::getenv("COUCH_DXR_VERBOSE");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

// ==== R4-B helpers (namespace Fast) ====
static int R4bMinFrame() {
    static int mf = -1;
    if (mf < 0) {
        const char* e = getenv("COUCH_D3D12_R4B_MIN_FRAME");
        mf = e ? atoi(e) : 5;
        if (mf < 5) mf = 5;
    }
    return mf;
}
static float R4bMinArea() {
    static float ma = -1.0f;
    if (ma < 0.0f) {
        const char* e = getenv("COUCH_D3D12_R4B_MIN_AREA");
        ma = e ? (float)atof(e) : 0.001f;
    }
    return ma;
}
// Test result tags emitted from the GPU frame so run scripts can grep a single token.
static void R4bLogResult(const char* test, bool visible) {
    LogCouchD3D12(std::string("[COUCH-R4B][RESULT] ") + test + "=" + (visible ? "VISIBLE" : "BLACK"));
}

GfxRenderingAPIDX12::GfxRenderingAPIDX12(GfxWindowBackendDXGI* backend)
    : mWindowBackend(backend), mInitialized(false), mVendorId(0), mDeviceId(0),
      mDedicatedVideoMemory(0), mFeatureLevel(D3D_FEATURE_LEVEL_11_0),
      mDxrTierString("NOT_SUPPORTED"), mFrameIndex(0), mFenceEvent(nullptr),
      mCurrentFenceValue(1), mRtvDescriptorSize(0), mDsvDescriptorSize(0),
      mSrvDescriptorSize(0), mNextSrvIndex(0),
      mCurrentFramebuffer(0), mCurrentTile(0), mCurrentFilterMode(FILTER_NONE),
      mCurrentShaderProgram(nullptr), mViewportDirty(true), mScissorDirty(true),
      mCurrentDepthTest(false), mCurrentDepthMask(false), mCurrentZmodeDecal(false),
      mCurrentUseAlpha(false), mCompilerModule(nullptr), mD3dCompile(nullptr),
      mPresentedFrames(0), mDrawCount(0), mVertexCount(0), mTriangleCount(0),
      mInvalidDrawCount(0), mCommandListOpen(false), mDeviceRemoved(false),
      mDebugLayerEnabled(false), mUploadBufferMapped(nullptr),
      mPixelIsoMode(0), mDiagnosticPSOsCreated(false), mDiagnosticDrawCount(0) {
      // R4-B — Valid-Vertex Canonical Draw protocol
      memset(&mR4b, 0, sizeof(mR4b));
    for (uint32_t i = 0; i < SHADER_MAX_TEXTURES; ++i) {
        mCurrentTextureIds[i] = 0;
        mCurrentSamplerIndices[i] = 0;
    }
    memset(&mViewport, 0, sizeof(mViewport));
    memset(&mScissorRect, 0, sizeof(mScissorRect));
    for (uint32_t i = 0; i < kFrameBufferCount; ++i) {
        mBackbufferState[i] = D3D12_RESOURCE_STATE_PRESENT;
    }
}

GfxRenderingAPIDX12::~GfxRenderingAPIDX12() {
    TemporalTrace::Instance().Shutdown();
    LogCouchD3D12("[COUCH-D3D12][TRACE] TemporalTrace shutdown complete");
    LogCouchD3D12("[COUCH-D3D12] Final Session Counters:");
    LogCouchD3D12("[COUCH-D3D12] Total Frames Presented = " + std::to_string(mPresentedFrames));
    LogCouchD3D12("[COUCH-D3D12] Total Fast3D Draws = " + std::to_string(mDrawCount));
    LogCouchD3D12("[COUCH-D3D12] Total Vertices Submitted = " + std::to_string(mVertexCount));
    LogCouchD3D12("[COUCH-D3D12] Total Triangles Submitted = " + std::to_string(mTriangleCount));
    LogCouchD3D12("[COUCH-D3D12] Total Invalid Draws Skipped = " + std::to_string(mInvalidDrawCount));

    if (mInitialized) {
        WaitForGpu();
    }

    if (mUploadBuffer && mUploadBufferMapped) {
        mUploadBuffer->Unmap(0, nullptr);
        mUploadBufferMapped = nullptr;
    }

    if (mFenceEvent) {
        CloseHandle(mFenceEvent);
        mFenceEvent = nullptr;
    }

    if (mCompilerModule) {
        FreeLibrary(mCompilerModule);
        mCompilerModule = nullptr;
    }

    if (sD3D12Module) {
        FreeLibrary(sD3D12Module);
        sD3D12Module = nullptr;
    }
}

GfxClipParameters GfxRenderingAPIDX12::GetClipParameters() {
    return { true, false };
}

void GfxRenderingAPIDX12::WaitForGpu() {
    if (!mCommandQueue || !mFence || !mFenceEvent) return;

    const uint64_t waitValue = mCurrentFenceValue++;
    if (FAILED(mCommandQueue->Signal(mFence.Get(), waitValue))) return;

    if (mFence->GetCompletedValue() < waitValue) {
        if (SUCCEEDED(mFence->SetEventOnCompletion(waitValue, mFenceEvent))) {
            WaitForSingleObject(mFenceEvent, INFINITE);
        }
    }

    for (uint32_t i = 0; i < kFrameBufferCount; ++i) {
        if (mCommandListOpen && i == mFrameIndex) continue;
        mFrameContexts[i].stagingBuffers.clear();
        ReleaseRetiredSrvDescriptors(i);
    }
}

uint32_t GfxRenderingAPIDX12::AllocateSrvDescriptor() {
    if (!mFreeSrvIndices.empty()) {
        const uint32_t index = mFreeSrvIndices.back();
        mFreeSrvIndices.pop_back();
        return index;
    }

    if (mNextSrvIndex < kSrvHeapSize) {
        return mNextSrvIndex++;
    }

    LogCouchD3D12("[COUCH-D3D12][ERROR] SRV heap exhausted; refusing unsafe descriptor overwrite");
    return kNullSrvIndex;
}

void GfxRenderingAPIDX12::ReleaseRetiredSrvDescriptors(uint32_t frameIndex) {
    auto& retired = mFrameContexts[frameIndex].retiredSrvIndices;
    mFreeSrvIndices.insert(mFreeSrvIndices.end(), retired.begin(), retired.end());
    retired.clear();
}

void GfxRenderingAPIDX12::WaitForFrame(uint32_t frameIndex) {
    if (!mFence || !mFenceEvent) return;

    if (mFence->GetCompletedValue() < mFrameContexts[frameIndex].fenceValue) {
        if (SUCCEEDED(mFence->SetEventOnCompletion(mFrameContexts[frameIndex].fenceValue, mFenceEvent))) {
            WaitForSingleObject(mFenceEvent, INFINITE);
        }
    }

    mFrameContexts[frameIndex].stagingBuffers.clear();
    ReleaseRetiredSrvDescriptors(frameIndex);
}

void GfxRenderingAPIDX12::ReleaseFrameStagingBuffers(uint32_t frameIndex) {
    if (mFence->GetCompletedValue() >= mFrameContexts[frameIndex].fenceValue) {
        mFrameContexts[frameIndex].stagingBuffers.clear();
    }
}

void GfxRenderingAPIDX12::MoveToNextFrame() {
    const uint64_t fenceValueForSignal = mCurrentFenceValue;
    mCommandQueue->Signal(mFence.Get(), fenceValueForSignal);
    mFrameContexts[mFrameIndex].fenceValue = fenceValueForSignal;
    mCurrentFenceValue++;

    mFrameIndex = mSwapChain->GetCurrentBackBufferIndex();

    WaitForFrame(mFrameIndex);
}

void GfxRenderingAPIDX12::TransitionResource(ID3D12Resource* res, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    if (from == to || !res || !mCommandList) return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = res;
    barrier.Transition.StateBefore = from;
    barrier.Transition.StateAfter = to;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    mCommandList->ResourceBarrier(1, &barrier);
}

bool GfxRenderingAPIDX12::CreateRootSignature() {
    constexpr uint32_t kPhysicalRootParameter = 2;
    constexpr uint32_t kPhysicalConstantDwords = 40;
    constexpr uint32_t kRootSignatureDwordCost = 4 + 8 + kPhysicalConstantDwords +
                                                  (2 * SHADER_MAX_TEXTURES);
    static_assert(kRootSignatureDwordCost <= 64,
                  "D3D12 graphics root signature exceeds the 64-DWORD limit");
    constexpr uint32_t kTextureRootParameter = kPhysicalRootParameter + 1;
    constexpr uint32_t kSamplerRootParameter = kTextureRootParameter + SHADER_MAX_TEXTURES;
    D3D12_ROOT_PARAMETER rootParams[kSamplerRootParameter + SHADER_MAX_TEXTURES] = {};

    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].Constants.ShaderRegister = 0;
    rootParams[0].Constants.RegisterSpace = 0;
    rootParams[0].Constants.Num32BitValues = 4;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[1].Constants.ShaderRegister = 1;
    rootParams[1].Constants.RegisterSpace = 0;
    rootParams[1].Constants.Num32BitValues = 8;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Per-draw material identity and physical flags. This is deliberately a
    // separate root constant block so the legacy Fast3D payload remains intact.
    rootParams[kPhysicalRootParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[kPhysicalRootParameter].Constants.ShaderRegister = 2;
    rootParams[kPhysicalRootParameter].Constants.RegisterSpace = 0;
    // 40 DWORDs here plus the other constants and twelve descriptor tables
    // exactly consume D3D12's 64-DWORD root-signature budget.
    rootParams[kPhysicalRootParameter].Constants.Num32BitValues = kPhysicalConstantDwords;
    rootParams[kPhysicalRootParameter].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Fast3D has six independent texture/sampler units: base textures 0-1,
    // masks 2-3, and replacement/blend textures 4-5. Keep one descriptor
    // table per register so the cached descriptor handles can be bound
    // without repacking the shader-visible heaps each draw.
    D3D12_DESCRIPTOR_RANGE srvRanges[SHADER_MAX_TEXTURES] = {};
    D3D12_DESCRIPTOR_RANGE samplerRanges[SHADER_MAX_TEXTURES] = {};
    for (uint32_t i = 0; i < SHADER_MAX_TEXTURES; ++i) {
        srvRanges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRanges[i].NumDescriptors = 1;
        srvRanges[i].BaseShaderRegister = i;
        srvRanges[i].RegisterSpace = 0;
        srvRanges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        rootParams[kTextureRootParameter + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[kTextureRootParameter + i].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[kTextureRootParameter + i].DescriptorTable.pDescriptorRanges = &srvRanges[i];
        rootParams[kTextureRootParameter + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        samplerRanges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        samplerRanges[i].NumDescriptors = 1;
        samplerRanges[i].BaseShaderRegister = i;
        samplerRanges[i].RegisterSpace = 0;
        samplerRanges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        rootParams[kSamplerRootParameter + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[kSamplerRootParameter + i].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[kSamplerRootParameter + i].DescriptorTable.pDescriptorRanges = &samplerRanges[i];
        rootParams[kSamplerRootParameter + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = kSamplerRootParameter + SHADER_MAX_TEXTURES;
    rsDesc.pParameters = rootParams;
    rsDesc.NumStaticSamplers = 0;
    rsDesc.pStaticSamplers = nullptr;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signatureBlob;
    ComPtr<ID3DBlob> errorBlob;
    if (!sD3D12SerializeRootSignature) {
        LogCouchD3D12("[COUCH-D3D12][ROOT_SIGNATURE_FAILED] serializer function unavailable");
        return false;
    }
    const HRESULT serializeResult = sD3D12SerializeRootSignature(
        &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(serializeResult) || !signatureBlob) {
        const std::string details = errorBlob && errorBlob->GetBufferPointer()
            ? std::string(static_cast<const char*>(errorBlob->GetBufferPointer()))
            : std::string("no serializer diagnostic available");
        LogCouchD3D12("[COUCH-D3D12][ROOT_SIGNATURE_SERIALIZE_FAILED] hr=" +
                      std::to_string(static_cast<uint32_t>(serializeResult)) + " " + details);
        return false;
    }

    const HRESULT createResult = mDevice->CreateRootSignature(
        0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
        IID_PPV_ARGS(&mRootSignature));
    if (FAILED(createResult) || !mRootSignature) {
        LogCouchD3D12("[COUCH-D3D12][ROOT_SIGNATURE_CREATE_FAILED] hr=" +
                      std::to_string(static_cast<uint32_t>(createResult)) +
                      " dwordCost=" + std::to_string(kRootSignatureDwordCost));
        return false;
    }

    LogCouchD3D12("[COUCH-D3D12][ROOT_SIGNATURE_READY] dwordCost=" +
                  std::to_string(kRootSignatureDwordCost));
    return true;
}

void GfxRenderingAPIDX12::CreateDescriptorHeaps() {
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = 2 * kFrameBufferCount + 256;
    rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    mDevice->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&mRtvHeap));
    mRtvDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.NumDescriptors = 257;
    dsvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    mDevice->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&mDsvHeap));
    mDsvDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    // Heap de CPU para el Depth SRV (no shader visible, usado como fuente para DXR)
    D3D12_DESCRIPTOR_HEAP_DESC depthSrvDesc = {};
    depthSrvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    depthSrvDesc.NumDescriptors = 257;
    depthSrvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    mDevice->CreateDescriptorHeap(&depthSrvDesc, IID_PPV_ARGS(&mDepthSrvHeap));

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = kSrvHeapSize;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    mDevice->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&mSrvHeap));
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    mDevice->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&mSrvCpuHeap));
    mSrvDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC samplerDesc = {};
    samplerDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    samplerDesc.NumDescriptors = 64;
    samplerDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    mDevice->CreateDescriptorHeap(&samplerDesc, IID_PPV_ARGS(&mSamplerHeap));
    mSamplerDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    mNextSamplerIndex = 0;
    for (uint32_t i = 0; i < SHADER_MAX_TEXTURES; ++i) {
        mCurrentSamplerIndices[i] = 0;
    }

    // Slot 0 es el Null Descriptor universal de Texture2D (kNullSrvIndex = 0)
    D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv = {};
    nullSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullSrv.Texture2D.MipLevels = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE nullHandle = mSrvHeap->GetCPUDescriptorHandleForHeapStart();
    mDevice->CreateShaderResourceView(nullptr, &nullSrv, nullHandle);
    mDevice->CreateShaderResourceView(nullptr, &nullSrv, mSrvCpuHeap->GetCPUDescriptorHandleForHeapStart());

    mNextSrvIndex = 1;
}

void GfxRenderingAPIDX12::CreateRenderTargetsAndDepth() {
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (uint32_t i = 0; i < kFrameBufferCount; ++i) {
        mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mRenderTargets[i]));
        mDevice->CreateRenderTargetView(mRenderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += mRtvDescriptorSize;
        mBackbufferState[i] = D3D12_RESOURCE_STATE_PRESENT;
    }

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    mSwapChain->GetDesc1(&desc);
    char swapInfo[512];
    snprintf(swapInfo, sizeof(swapInfo),
        "[COUCH-DXR-FORENSIC][SWAPCHAIN_DESC1] width=%u height=%u format=%u stereo=%u sampleCount=%u sampleQuality=%u bufferCount=%u scaling=%u swapEffect=%u alphaMode=%u flags=0x%08X",
        desc.Width, desc.Height, static_cast<unsigned>(desc.Format), desc.Stereo, desc.SampleDesc.Count,
        desc.SampleDesc.Quality, desc.BufferCount, static_cast<unsigned>(desc.Scaling),
        static_cast<unsigned>(desc.SwapEffect), static_cast<unsigned>(desc.AlphaMode), desc.Flags);
    LogCouchD3D12(swapInfo);
    for (uint32_t i = 0; i < kFrameBufferCount; ++i) {
        const auto targetDesc = mRenderTargets[i]->GetDesc();
        char targetInfo[320];
        snprintf(targetInfo, sizeof(targetInfo), "[COUCH-DXR-FORENSIC][BACKBUFFER] index=%u resource=%p width=%llu height=%u format=%u sampleCount=%u flags=0x%08X",
            i, mRenderTargets[i].Get(), static_cast<unsigned long long>(targetDesc.Width), targetDesc.Height,
            static_cast<unsigned>(targetDesc.Format), targetDesc.SampleDesc.Count, static_cast<unsigned>(targetDesc.Flags));
        LogCouchD3D12(targetInfo);
    }

    mRenderWidth = (desc.Width > 0) ? desc.Width : 640;
    mRenderHeight = (desc.Height > 0) ? desc.Height : 480;

    // Hard Gate RT3: Recurso Depth Typeless para soportar DSV y SRV DXR
    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = mRenderWidth;
    depthDesc.Height = mRenderHeight;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearVal = {};
    clearVal.Format = DXGI_FORMAT_D32_FLOAT;
    clearVal.DepthStencil.Depth = 1.0f;
    clearVal.DepthStencil.Stencil = 0;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC hdrDesc = {};
    hdrDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    hdrDesc.Width = mRenderWidth;
    hdrDesc.Height = mRenderHeight;
    hdrDesc.DepthOrArraySize = 1;
    hdrDesc.MipLevels = 1;
    hdrDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    hdrDesc.SampleDesc.Count = 1;
    hdrDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE hdrClear = {};
    hdrClear.Format = hdrDesc.Format;
    hdrClear.Color[3] = 1.0f;
    for (uint32_t i = 0; i < kFrameBufferCount; ++i) {
        if (SUCCEEDED(mDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &hdrDesc,
                D3D12_RESOURCE_STATE_RENDER_TARGET, &hdrClear, IID_PPV_ARGS(&mHdrSceneTargets[i])))) {
            auto hdrRtv = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
            hdrRtv.ptr += size_t(kFrameBufferCount + i) * mRtvDescriptorSize;
            mDevice->CreateRenderTargetView(mHdrSceneTargets[i].Get(), nullptr, hdrRtv);
            mHdrSceneState[i] = D3D12_RESOURCE_STATE_RENDER_TARGET;
        } else {
            mHdrSceneActive = false;
            for (auto& target : mHdrSceneTargets) target.Reset();
            break;
        }
    }

    mDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc,
                                    D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearVal,
                                    IID_PPV_ARGS(&mDepthStencilBuffer));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvViewDesc = {};
    dsvViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    mDevice->CreateDepthStencilView(mDepthStencilBuffer.Get(), &dsvViewDesc, mDsvHeap->GetCPUDescriptorHandleForHeapStart());

    // Crear SRV R32_FLOAT para lectura shader DXR
    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
    depthSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrvDesc.Texture2D.MipLevels = 1;

    mDepthSrvCpuHandle = mDepthSrvHeap->GetCPUDescriptorHandleForHeapStart();
    mDevice->CreateShaderResourceView(mDepthStencilBuffer.Get(), &depthSrvDesc, mDepthSrvCpuHandle);
}

void GfxRenderingAPIDX12::CleanUpRenderTargetsAndDepth() {
    mHdrSceneActive = false;
    for (uint32_t i = 0; i < kFrameBufferCount; ++i) {
        mRenderTargets[i].Reset();
        mHdrSceneTargets[i].Reset();
        mHdrSceneState[i] = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    mDepthStencilBuffer.Reset();
}

void GfxRenderingAPIDX12::Init() {
    if (mInitialized) return;

    FILE* initMarker = nullptr;
    if (forensic::SceneHarnessActive() && !forensic::SceneHarnessRunRoot().empty()) {
        std::filesystem::create_directories(forensic::SceneHarnessRunRoot());
        initMarker = fopen((forensic::SceneHarnessRunRoot() / "harness-d3d12.log").string().c_str(), "a");
    } else {
        initMarker = fopen(couch_diagnostics_path(), "a");
    }
    if (initMarker) {
        fprintf(initMarker, "[COUCH-INIT] GfxRenderingAPIDX12::Init entered\n");
        fclose(initMarker);
    }

    sD3D12Module = LoadLibraryW(L"d3d12.dll");
    if (!sD3D12Module) {
        SPDLOG_ERROR("[COUCH-D3D12] d3d12.dll not found");
        return;
    }

    sD3D12CreateDevice = (PFN_D3D12_CREATE_DEVICE)GetProcAddress(sD3D12Module, "D3D12CreateDevice");
    sD3D12GetDebugInterface = (PFN_D3D12_GET_DEBUG_INTERFACE)GetProcAddress(sD3D12Module, "D3D12GetDebugInterface");
    sD3D12SerializeRootSignature = (PFN_D3D12_SERIALIZE_ROOT_SIGNATURE)GetProcAddress(sD3D12Module, "D3D12SerializeRootSignature");

    mCompilerModule = LoadLibraryW(L"D3DCompiler_47.dll");
    if (mCompilerModule) {
        mD3dCompile = (pD3DCompile)GetProcAddress(mCompilerModule, "D3DCompile");
    }

    ComPtr<ID3D12Debug> debugController;
    ComPtr<ID3D12Debug1> debugController1;
    const char* debugLayerRequest=getenv("COUCH_D3D12_DEBUG");
    if (sD3D12GetDebugInterface && debugLayerRequest && strcmp(debugLayerRequest,"1")==0) {
        if (SUCCEEDED(sD3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
            debugController->EnableDebugLayer();
            mDebugLayerEnabled = true;
            LogCouchD3D12("[COUCH-D3D12] Debug Layer ENABLED");

            const char* gbvEnv = getenv("COUCH_D3D12_GBV");
            if (gbvEnv && strcmp(gbvEnv, "1") == 0) {
                if (SUCCEEDED(debugController->QueryInterface(IID_PPV_ARGS(&debugController1)))) {
                    debugController1->SetEnableGPUBasedValidation(TRUE);
                    LogCouchD3D12("[COUCH-D3D12] GPU Based Validation ENABLED (slow!)");
                }
            }
        }
    }

    typedef HRESULT(WINAPI* PFN_CREATE_DXGI_FACTORY1)(REFIID, void**);
    HMODULE hDxgi = GetModuleHandleA("dxgi.dll");
    if (!hDxgi) hDxgi = LoadLibraryA("dxgi.dll");
    PFN_CREATE_DXGI_FACTORY1 pfnCreateDXGIFactory1 = hDxgi ? (PFN_CREATE_DXGI_FACTORY1)GetProcAddress(hDxgi, "CreateDXGIFactory1") : nullptr;

    ComPtr<IDXGIFactory4> factory;
    if (!pfnCreateDXGIFactory1 || FAILED(pfnCreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        SPDLOG_ERROR("[COUCH-D3D12] Failed to create DXGI Factory4");
        return;
    }

    ComPtr<IDXGIAdapter1> bestAdapter;
    DXGI_ADAPTER_DESC1 bestDesc = {};
    size_t maxVram = 0;
    bool bestAdapterSupportsDxr = false;

    for (UINT i = 0; ; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) break;

        DXGI_ADAPTER_DESC1 desc = {};
        adapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

        ComPtr<ID3D12Device> adapterProbe;
        bool adapterSupportsDxr = false;
        if (SUCCEEDED(sD3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                        IID_PPV_ARGS(&adapterProbe)))) {
            D3D12_FEATURE_DATA_D3D12_OPTIONS5 adapterOptions = {};
            adapterSupportsDxr = SUCCEEDED(adapterProbe->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS5, &adapterOptions, sizeof(adapterOptions))) &&
                adapterOptions.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
            // Prefer an actual DXR adapter over a larger non-RTX adapter. This
            // keeps the adapter used by the renderer aligned with the RTX
            // capability check in Fast3dWindow on hybrid-GPU systems.
            const bool betterDxrMatch = adapterSupportsDxr && !bestAdapterSupportsDxr;
            const bool sameDxrClass = adapterSupportsDxr == bestAdapterSupportsDxr;
            if (!bestAdapter || betterDxrMatch || (sameDxrClass && desc.DedicatedVideoMemory > maxVram)) {
                bestAdapter = adapter;
                bestDesc = desc;
                maxVram = desc.DedicatedVideoMemory;
                bestAdapterSupportsDxr = adapterSupportsDxr;
            }
        }
    }

    if (!bestAdapter) {
        SPDLOG_ERROR("[COUCH-D3D12] No compatible Direct3D 12 hardware adapter found");
        return;
    }

    char descA[128];
    WideCharToMultiByte(CP_UTF8, 0, bestDesc.Description, -1, descA, sizeof(descA), nullptr, nullptr);
    bestAdapter.As(&mQualityAdapter);
    mAdapterName = descA;
    mVendorId = bestDesc.VendorId;
    mDeviceId = bestDesc.DeviceId;
    mDedicatedVideoMemory = bestDesc.DedicatedVideoMemory;

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };

    HRESULT hrDevice = E_FAIL;
    for (auto fl : featureLevels) {
        hrDevice = sD3D12CreateDevice(bestAdapter.Get(), fl, IID_PPV_ARGS(&mDevice));
        if (SUCCEEDED(hrDevice)) {
            mFeatureLevel = fl;
            break;
        }
    }

    if (FAILED(hrDevice)) {
        SPDLOG_ERROR("[COUCH-D3D12] D3D12CreateDevice failed");
        return;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    const HRESULT dxrOptionsHr = mDevice->CheckFeatureSupport(
        D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5));
    if (SUCCEEDED(dxrOptionsHr)) {
        const unsigned int raytracingTier = static_cast<unsigned int>(options5.RaytracingTier);
        if (raytracingTier >= 12) {
            // Windows SDK 10.0.26100 adds D3D12_RAYTRACING_TIER_1_2.
            mDxrTierString = "TIER_1_2";
        } else if (raytracingTier >= 11) {
            mDxrTierString = "TIER_1_1";
        } else if (raytracingTier >= 10) {
            mDxrTierString = "TIER_1_0";
        } else {
            mDxrTierString = "NOT_SUPPORTED";
        }
    } else {
        mDxrTierString = "NOT_SUPPORTED";
    }
    {
        std::stringstream dxrProbe;
        dxrProbe << "[COUCH-D3D12] DXR Probe HRESULT=0x" << std::hex
                 << static_cast<unsigned long>(dxrOptionsHr)
                 << " RaytracingTier=" << std::dec
                 << static_cast<unsigned int>(options5.RaytracingTier);
        LogCouchD3D12(dxrProbe.str());
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    if (FAILED(mDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue)))) {
        SPDLOG_ERROR("[COUCH-D3D12] Failed to create CommandQueue");
        return;
    }

    for (uint32_t i = 0; i < kFrameBufferCount; ++i) {
        mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mFrameContexts[i].commandAllocator));
        mFrameContexts[i].fenceValue = 0;
        mFrameContexts[i].ringBufferBase = i * kRingBufferSliceSize;
        mFrameContexts[i].ringBufferUsed = 0;
    }

    mDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, mFrameContexts[0].commandAllocator.Get(),
                              nullptr, IID_PPV_ARGS(&mCommandList));
    mCommandList->Close();
    mCommandListOpen = false;

    mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence));
    mFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    mWindowBackend->CreateSwapChain(mCommandQueue.Get(), [this]() {
        WaitForGpu();
        CleanUpRenderTargetsAndDepth();
    });

    ComPtr<IDXGISwapChain1> sc1 = mWindowBackend->GetSwapChain();
    if (!sc1 || FAILED(sc1.As(&mSwapChain))) {
        SPDLOG_ERROR("[COUCH-D3D12] Failed to acquire IDXGISwapChain3");
        return;
    }

    CreateDescriptorHeaps();
    CreateRenderTargetsAndDepth();
    if (!CreateRootSignature()) {
        SPDLOG_ERROR("[COUCH-D3D12] Root signature creation failed; aborting D3D12 backend initialization");
        return;
    }

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = kUploadBufferSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    mDevice->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                    IID_PPV_ARGS(&mUploadBuffer));
    mUploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mUploadBufferMapped));

    mInitialized = true;
    CreateFramebuffer(); // framebuffer 0 always denotes the current swapchain image.
    Ship::Context::GetInstance()->GetConsoleVariables()->SetInteger(CVAR_MSAA_VALUE, 1);

    LogCouchD3D12("[COUCH-D3D12] Backend = D3D12");
    LogCouchD3D12("[COUCH-D3D12] Adapter = " + mAdapterName);
    std::stringstream ssFl;
    ssFl << "0x" << std::hex << (uint32_t)mFeatureLevel;
    std::string flStr = "UNKNOWN";
    if (mFeatureLevel == D3D_FEATURE_LEVEL_12_2) flStr = "12_2";
    else if (mFeatureLevel == D3D_FEATURE_LEVEL_12_1) flStr = "12_1";
    else if (mFeatureLevel == D3D_FEATURE_LEVEL_12_0) flStr = "12_0";
    else if (mFeatureLevel == D3D_FEATURE_LEVEL_11_1) flStr = "11_1";
    else if (mFeatureLevel == D3D_FEATURE_LEVEL_11_0) flStr = "11_0";
    LogCouchD3D12("[COUCH-D3D12] FeatureLevel Requested = D3D_FEATURE_LEVEL_12_2..11_0");
    LogCouchD3D12("[COUCH-D3D12] FeatureLevel Obtained = " + ssFl.str() + " (" + flStr + ")");
    LogCouchD3D12("[COUCH-D3D12] FeatureLevel = " + ssFl.str() + " (" + flStr + ")");
    LogCouchD3D12("[COUCH-D3D12] ShaderModel = 5.0 (DXBC)");
    LogCouchD3D12("[COUCH-D3D12] DXR Tier = " + mDxrTierString);
    LogCouchD3D12("[COUCH-D3D12] Debug Layer = " + std::string(mDebugLayerEnabled ? "ON" : "OFF"));
    LogCouchD3D12("[COUCH-D3D12] Upload Buffer Size = " + std::to_string(kUploadBufferSize / (1024*1024)) + " MB");
    LogCouchD3D12("[COUCH-D3D12] SRV Heap Slots = " + std::to_string(kSrvHeapSize));
    LogCouchD3D12("[COUCH-D3D12] Frames in Flight = " + std::to_string(kFrameBufferCount));
    LogCouchD3D12("[COUCH-D3D12] Device created OK");
    LogCouchD3D12("[COUCH-D3D12] Swapchain created OK");

    Ship::GuiWindowInitData window_impl = {};
    window_impl.Dx11.Window = mWindowBackend->GetWindowHandle();
    window_impl.Dx11.Device = nullptr;
    window_impl.Dx11.DeviceContext = nullptr;
    window_impl.Dx12Renderer = this;
    Ship::Context::GetInstance()->GetWindow()->GetGui()->Init(window_impl);

    DxrAccelerationStructureManager::Instance().Init(mDevice.Get());
    DxrShadowRenderer::Instance().Init(mDevice.Get());
    DxrSurfaceRenderer::Instance().Init(mDevice.Get());

    // Phase 1: Log effective env vars at startup for test identification
    {
        const char* eD3d12 = getenv("COUCH_D3D12");
        const char* eDxrAs = getenv("COUCH_DXR_AS");
        const char* eDxrShadow = getenv("COUCH_DXR_SHADOW");
        const char* eTemporal = getenv("COUCH_TEMPORAL");
        const char* eDispatch = getenv("COUCH_DXR_DISPATCH_ENABLE");
        const char* eComposite = getenv("COUCH_DXR_COMPOSITE_ENABLE");
        const char* eWhite = getenv("COUCH_WHITE_COMPOSITE_TEST");
        LogCouchD3D12("[COUCH-ENV-EFFECTIVE] COUCH_D3D12=" + std::string(eD3d12 ? eD3d12 : "(unset)") +
                      " COUCH_DXR_AS=" + std::string(eDxrAs ? eDxrAs : "(unset)") +
                      " COUCH_DXR_SHADOW=" + std::string(eDxrShadow ? eDxrShadow : "(unset)") +
                      " COUCH_TEMPORAL=" + std::string(eTemporal ? eTemporal : "(unset)") +
                      " COUCH_DXR_DISPATCH_ENABLE=" + std::string(eDispatch ? eDispatch : "(unset)") +
                      " COUCH_DXR_COMPOSITE_ENABLE=" + std::string(eComposite ? eComposite : "(unset)") +
                      " COUCH_WHITE_COMPOSITE_TEST=" + std::string(eWhite ? eWhite : "(unset)"));
    }

    // RT4-A: GPU Profiler y Temporal Manager
    GpuProfilerD3D12::Instance().Init(mDevice.Get(), mCommandQueue.Get());
    TemporalManager::Instance().Init(mDevice.Get(), mCommandQueue.Get());

    // Forensic phase 2: register composite capture callback
    forensic::gSelf = this;
    TemporalManager::Instance().RegisterCompositeCaptureCallback(
        [](int frame) {
            ForensicGlobalCompositeCapture(frame);
        });
    // Initialize trace with config name based on temporal enabled state
    const char* traceConfig = TemporalManager::Instance().IsEnabled() ? "C_extra_corrected" : "A_corrected";
    TemporalTrace::Instance().Initialize(traceConfig);
    LogCouchD3D12("[COUCH-D3D12][TRACE] TemporalTrace initialized for config: " + std::string(traceConfig));
    LogCouchD3D12("[COUCH-FORENSIC] build=" + std::string(__DATE__) + " " + __TIME__ +
                  " mask=" + std::to_string(DxrShadowRenderer::Instance().GetMaskWidth()) +
                  "x" + std::to_string(DxrShadowRenderer::Instance().GetMaskHeight()) +
                  " render=" + std::to_string(mRenderWidth) + "x" + std::to_string(mRenderHeight));
    LogCouchD3D12("[COUCH-D3D12][TRACE] Init order: DxrAS=" + std::to_string(DxrAccelerationStructureManager::Instance().IsEnabled() ? 1 : 0) +
                  " DxrShadow=" + std::to_string(DxrShadowRenderer::Instance().IsEnabled() ? 1 : 0) +
                  " Profiler=OK" +
                  " Temporal=" + std::to_string(TemporalManager::Instance().IsEnabled() ? 1 : 0));
}

void GfxRenderingAPIDX12::PrepareInitFallback() {
    WaitForGpu(); CleanUpRenderTargetsAndDepth(); mSwapChain.Reset();
    mWindowBackend->ReleaseSwapChainForInitFallback();
}

void GfxRenderingAPIDX12::OnResize() {
    if (!mInitialized || !mSwapChain) return;

    DxrAccelerationStructureManager::Instance().HandleSceneTransition();

    WaitForGpu();
    CleanUpRenderTargetsAndDepth();

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    mSwapChain->GetDesc1(&desc);
    mSwapChain->ResizeBuffers(kFrameBufferCount, 0, 0, DXGI_FORMAT_R8G8B8A8_UNORM, desc.Flags);

    mFrameIndex = mSwapChain->GetCurrentBackBufferIndex();
    CreateRenderTargetsAndDepth();

    DxrShadowRenderer::Instance().OnResize(mRenderWidth, mRenderHeight);
    DxrSurfaceRenderer::Instance().OnResize(mRenderWidth, mRenderHeight);

    // RT4-A: Resize temporal resources and invalidate history
    TemporalManager::Instance().OnResize(mRenderWidth, mRenderHeight);

    mViewportDirty = true;
    mScissorDirty = true;
}

// ==== FORENSIC PHASE 2: real backbuffer readback capture (T0/R8G8B8A8) ====
#include <cstring>
#include <vector>
#include <cstdint>
static bool ForensicCaptureBackbuffer(
    ID3D12Device* device, ID3D12CommandQueue* queue,
    ID3D12Resource* backbuffer, bool presentState,
    const char* stageName, int frame) {
    if (!forensic::ForensicCapturesEnabled()) return false;
    if (!device || !queue || !backbuffer) return false;
    D3D12_RESOURCE_DESC desc = backbuffer->GetDesc();
    if (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM) return false;

    // Dedicated capture allocator + command list (NEVER touch the main frame cmdList)
    static Microsoft::WRL::ComPtr<ID3D12CommandAllocator> sCapAlloc;
    static Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> sCapCmd;
    if (!sCapAlloc) {
        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&sCapAlloc)))) return false;
        if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, sCapAlloc.Get(), nullptr, IID_PPV_ARGS(&sCapCmd)))) return false;
        sCapCmd->Close();
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT64 rowSizeInBytes = 0; UINT rowCount = 0; UINT64 totalBytes = 0;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rowCount, &rowSizeInBytes, &totalBytes);
    if (totalBytes == 0) return false;

    D3D12_HEAP_PROPERTIES readbackHeap = {};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC readbackDesc = {};
    readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDesc.Width = totalBytes;
    readbackDesc.Height = 1;
    readbackDesc.DepthOrArraySize = 1;
    readbackDesc.MipLevels = 1;
    readbackDesc.Format = DXGI_FORMAT_UNKNOWN;
    readbackDesc.SampleDesc.Count = 1;
    readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    if (FAILED(device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) return false;

    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return false;
    HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!event) return false;

    D3D12_RESOURCE_STATES stateBefore = presentState ? D3D12_RESOURCE_STATE_PRESENT : D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = backbuffer;
    barriers[0].Transition.StateBefore = stateBefore;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = backbuffer;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[1].Transition.StateAfter = stateBefore;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = backbuffer;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = readback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = footprint;

    if (FAILED(sCapAlloc->Reset())) { CloseHandle(event); return false; }
    if (FAILED(sCapCmd->Reset(sCapAlloc.Get(), nullptr))) { CloseHandle(event); return false; }
    sCapCmd->ResourceBarrier(1, &barriers[0]);
    sCapCmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    sCapCmd->ResourceBarrier(1, &barriers[1]);
    sCapCmd->Close();
    ID3D12CommandList* lists[] = { sCapCmd.Get() };
    queue->ExecuteCommandLists(1, lists);
    queue->Signal(fence.Get(), 1);
    fence->SetEventOnCompletion(1, event);
    WaitForSingleObject(event, 5000);
    CloseHandle(event);

    D3D12_RANGE mapRange = { 0, (SIZE_T)totalBytes };
    void* mapped = nullptr;
    if (FAILED(readback->Map(0, &mapRange, &mapped)) || !mapped) return false;

    bool ok = false;
    int w = (int)desc.Width;
    int h = (int)desc.Height;
    int rowPitch = (int)footprint.Footprint.RowPitch;
    std::filesystem::path backbufferCaptureRoot;
    if (forensic::SceneHarnessActive()) {
        // This legacy side-channel is not part of the request/acknowledged stage
        // protocol. Keep its screenshots out of captures/ so they cannot be
        // mistaken for evidence requested by the scenario manifest.
        backbufferCaptureRoot = forensic::SceneHarnessRunRoot() / "diagnostics" / "legacy_backbuffer";
        std::filesystem::create_directories(backbufferCaptureRoot);
    } else {
        CreateDirectoryA("logs", nullptr);
        CreateDirectoryA("logs\\captures", nullptr);
    }
    char path[512];
    if (forensic::SceneHarnessActive()) {
        const auto pathValue = (backbufferCaptureRoot /
            ("frame" + std::to_string(frame) + "_" + stageName + ".png")).string();
        snprintf(path, sizeof(path), "%s", pathValue.c_str());
    } else {
        snprintf(path, sizeof(path), "logs/captures/frame%06d_%s.png", frame, stageName);
    }
    if (w > 0 && h > 0 && rowSizeInBytes >= (UINT64)w * 4) {
        const uint8_t* srcRow = (const uint8_t*)mapped;
        std::vector<uint8_t> rgba((size_t)w * h * 4);
        for (int y = 0; y < h; ++y) {
            memcpy(rgba.data() + (size_t)y * w * 4, srcRow + (size_t)y * rowPitch, (size_t)w * 4);
        }
        ok = forensic::ForensicWriteCapturePair(path, w, h, rgba);
    }
    D3D12_RANGE empty = { 0, 0 };
    readback->Unmap(0, &empty);
    return ok;
}

void GfxRenderingAPIDX12::ForensicCompositeCapture(int frame) {
    if (forensic::SceneHarnessActive()) return; // The harness uses the in-frame, sequence-acked composite tap.
    if (forensic::g.nComposite >= forensic::g.max) return;
    if (!mInitialized || !mCommandList || !mFrameContexts[mFrameIndex].commandAllocator) return;
    ID3D12Resource* bb = mRenderTargets[mFrameIndex].Get();
    if (!bb) return;
    if (ForensicCaptureBackbuffer(mDevice.Get(), mCommandQueue.Get(),
        bb, false, "after_composite", frame)) {
        ++forensic::g.nComposite;
        LogCouchD3D12("[COUCH-FORENSIC] capture after_composite frame=" + std::to_string(frame));
    }
}

// ==== FORENSIC: R8_UNORM mask readback (e.g. shadow mask after DispatchRays) ====
static bool ForensicCaptureR8Resource(
    ID3D12Device* device, ID3D12CommandQueue* queue,
    ID3D12Resource* resource, const char* stageName, int frame) {
    if (!device || !queue || !resource) return false;
    D3D12_RESOURCE_DESC desc = resource->GetDesc();
    if (desc.Format != DXGI_FORMAT_R8_UNORM) return false;

    static Microsoft::WRL::ComPtr<ID3D12CommandAllocator> sCapAlloc;
    static Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> sCapCmd;
    if (!sCapAlloc) {
        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&sCapAlloc)))) return false;
        if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, sCapAlloc.Get(), nullptr, IID_PPV_ARGS(&sCapCmd)))) return false;
        sCapCmd->Close();
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT64 rowSize = 0; UINT rowCount = 0; UINT64 totalBytes = 0;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rowCount, &rowSize, &totalBytes);
    if (totalBytes == 0) return false;

    D3D12_HEAP_PROPERTIES rbHeap = {}; rbHeap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rbDesc = {};
    rbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rbDesc.Width = totalBytes;
    rbDesc.Height = 1; rbDesc.DepthOrArraySize = 1; rbDesc.MipLevels = 1;
    rbDesc.SampleDesc.Count = 1; rbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    if (FAILED(device->CreateCommittedResource(&rbHeap, D3D12_HEAP_FLAG_NONE, &rbDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) return false;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return false;
    HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!event) return false;

    D3D12_RESOURCE_BARRIER b[2] = {};
    b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b[0].Transition.pResource = resource;
    b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b[1].Transition.pResource = resource;
    b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    D3D12_TEXTURE_COPY_LOCATION src = {}; src.pResource = resource;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION dst = {}; dst.pResource = readback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = footprint;

    if (FAILED(sCapAlloc->Reset())) { CloseHandle(event); return false; }
    if (FAILED(sCapCmd->Reset(sCapAlloc.Get(), nullptr))) { CloseHandle(event); return false; }
    sCapCmd->ResourceBarrier(1, &b[0]);
    sCapCmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    sCapCmd->ResourceBarrier(1, &b[1]);
    sCapCmd->Close();
    ID3D12CommandList* lists[] = { sCapCmd.Get() };
    queue->ExecuteCommandLists(1, lists);
    queue->Signal(fence.Get(), 1);
    fence->SetEventOnCompletion(1, event);
    WaitForSingleObject(event, 5000);
    CloseHandle(event);

    D3D12_RANGE mr = { 0, (SIZE_T)totalBytes };
    void* mapped = nullptr;
    if (FAILED(readback->Map(0, &mr, &mapped)) || !mapped) return false;
    int w = (int)desc.Width, h = (int)desc.Height;
    int rowPitch = (int)footprint.Footprint.RowPitch;
    UINT mn = 255, mx = 0; double sum = 0; UINT nz = 0; UINT sampled = 0;
    for (int y = 0; y < h; y += 4) {
        const uint8_t* row = (const uint8_t*)mapped + (size_t)y * rowPitch;
        for (int x = 0; x < w; x += 4) {
            UINT v = row[x];
            if (v < mn) mn = v; if (v > mx) mx = v; sum += v; ++sampled;
            if (v != 0) ++nz;
        }
    }
    if (forensic::SceneHarnessActive()) std::filesystem::create_directories(forensic::SceneHarnessCaptureRoot());
    else { CreateDirectoryA("logs", nullptr); CreateDirectoryA("logs\\captures", nullptr); }
    char path[512];
    if (forensic::SceneHarnessActive()) {
        const auto pathValue = (forensic::SceneHarnessCaptureRoot() /
            ("mask_frame" + std::to_string(frame) + "_" + stageName + ".png")).string();
        snprintf(path, sizeof(path), "%s", pathValue.c_str());
    } else {
        snprintf(path, sizeof(path), "logs/captures/mask_frame%06d_%s.png", frame, stageName);
    }
    bool wrote = false;
    if (w > 0 && h > 0) {
        std::vector<uint8_t> rgba((size_t)w * h * 4);
        for (int y = 0; y < h; ++y) {
            const uint8_t* row = (const uint8_t*)mapped + (size_t)y * rowPitch;
            for (int x = 0; x < w; ++x) {
                uint8_t g = row[x];
                size_t o = ((size_t)y * w + x) * 4;
                rgba[o] = g; rgba[o+1] = g; rgba[o+2] = g; rgba[o+3] = 255;
            }
        }
        wrote = forensic::ForensicWriteCapturePair(path, w, h, rgba);
    }
    D3D12_RANGE er = { 0, 0 }; readback->Unmap(0, &er);
    char sb[256];
    snprintf(sb, sizeof(sb), "[COUCH-FORENSIC][MASK_STATS] stage=%s frame=%d min=%u max=%u avg=%.3f nonzero=%.2f%% sampled=%u png=%d",
        stageName, frame, mn, mx, sampled ? sum / sampled : 0.0, sampled ? 100.0 * nz / sampled : 0.0, sampled, (int)wrote);
    LogCouchD3D12(sb);
    return true;
}

void GfxRenderingAPIDX12::StartFrame() {
    ++forensic::gForensicFrame;
    if (!mInitialized) return;
    mDxrPassesRunThisFrame = false;
    mDxrWorkAttemptedThisFrame = false;
    mDxrFrameEvidence = {};
    mDxrFrameEvidence.frame = forensic::gForensicFrame;

    static bool pixelIsoChecked = false;
    if (!pixelIsoChecked) {
        // R4B is the canonical diagnostic switch. Keep the older name as a
        // compatibility alias for existing forensic scripts.
        const char* isoEnv = getenv("COUCH_D3D12_R4B");
        const char* legacyIsoEnv = getenv("COUCH_D3D12_PIXEL_ISO");
        if (!isoEnv) {
            isoEnv = legacyIsoEnv;
        }
        if (isoEnv) {
            mPixelIsoMode = atoi(isoEnv);
            LogCouchD3D12("[COUCH-R4B] COUCH_D3D12_R4B=" + std::to_string(mPixelIsoMode));
        }
        pixelIsoChecked = true;
    }

    static bool r7c5A5Checked = false;
    if (!r7c5A5Checked) {
        const char* a5Env = getenv("COUCH_R7C5_A5");
        mR7c5A5Enabled = a5Env && std::string(a5Env) == "1";
        if (mR7c5A5Enabled) LogCouchD3D12("[COUCH-R7C5-A5] ENABLED");
        r7c5A5Checked = true;
    }

    mDiagnosticDrawCount = 0;

    if (mR7c5A5Enabled) BeginR4bFrameStats();

    WaitForFrame(mFrameIndex);
    UpdateAutomaticQuality();

    auto& ctx = mFrameContexts[mFrameIndex];
    ctx.commandAllocator->Reset();

    mCommandList->Reset(ctx.commandAllocator.Get(), nullptr);
    mCommandListOpen = true;

    const auto consoleVariables = Ship::Context::GetInstance()->GetConsoleVariables();
    mHdrSceneActive = MaterialRegistry::IsEnhancedVisualProfile() &&
                      consoleVariables && consoleVariables->GetInteger("gEnhancements.Graphics.DXR.Tonemapping", 1) &&
                      mHdrSceneTargets[mFrameIndex] != nullptr;

    // RT4-A: CPU frame timing + GPU profiler begin
    auto now = std::chrono::high_resolution_clock::now();
    double cpuMs = std::chrono::duration<double, std::milli>(now.time_since_epoch()).count();
    GpuProfilerD3D12::Instance().SetCpuFrameStart(cpuMs);
    GpuProfilerD3D12::Instance().BeginFrame(mCommandList.Get(), mFrameIndex, mFence.Get(), mCurrentFenceValue);
    TEMPORAL_TRACE_ISENABLED("StartFrame:IsEnabled", TemporalManager::Instance().IsEnabled());
    TemporalManager::Instance().BeginFrame(mCommandList.Get(), mFrameIndex);

    // ==== FORENSIC PHASE 2: FRAME_BEGIN snapshot ====
    {
        TEMPORAL_TRACE_STAGE("FRAME_BEGIN",
            mRenderTargets[mFrameIndex].Get(), mBackbufferState[mFrameIndex],
            mDepthStencilBuffer.Get(), (unsigned)D3D12_RESOURCE_STATE_DEPTH_WRITE,
            DxrShadowRenderer::Instance().GetShadowMaskForTrace(),
            DxrShadowRenderer::Instance().GetShadowMaskStateForTrace(),
            TemporalManager::Instance().GetMotionVectorBuffer(),
            (unsigned)D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            TemporalManager::Instance().GetHistoryResourceForTrace(0),
            TemporalManager::Instance().GetHistoryResourceStateForTrace(0),
            TemporalManager::Instance().GetHistoryResourceForTrace(1),
            TemporalManager::Instance().GetHistoryResourceStateForTrace(1),
            TemporalManager::Instance().GetTemporalOutputBufferForTrace(),
            TemporalManager::Instance().GetTemporalOutputBufferStateForTrace(),
            mSrvHeap.Get(), mSrvHeap.Get(), mRootSignature.Get(), nullptr,
            mRenderTargets[mFrameIndex].Get(),
            (void*)(uintptr_t)mDsvHeap->GetCPUDescriptorHandleForHeapStart().ptr,
            mCommandList.Get(), mCommandListOpen);

        // Forensic capture: previous frame content after RASTER_END (backbuffer still PRESENT)
        if (!forensic::g.doneRaster && forensic::g.nRaster < forensic::g.max) {
            ID3D12Resource* bb = mRenderTargets[mFrameIndex].Get();
            if (bb) {
                int f = (int)TemporalManager::Instance().GetFrameCounter();
                if (ForensicCaptureBackbuffer(mDevice.Get(), mCommandQueue.Get(),
                    bb, true, "previous_present_raster_label", f)) {
                    ++forensic::g.nRaster;
                    LogCouchD3D12("[COUCH-FORENSIC] capture after_raster frame=" + std::to_string(f));
                }
            }
        }

        // Optional visual gate: capture the real title-screen frame after the
        // Spanish title path has rendered. This is disabled unless explicitly
        // requested and has no effect on the normal render path.
        static const bool titleCaptureEnabled = [] {
            const char* e = getenv("COUCH_TITLE_CAPTURE");
            return e && strcmp(e, "1") == 0;
        }();
        static bool titleCaptureDone = false;
        if (titleCaptureEnabled && !titleCaptureDone) {
            ID3D12Resource* bb = mRenderTargets[mFrameIndex].Get();
            int f = forensic::gForensicFrame;
            if (bb && f >= 2400 && f < 2404 &&
                ForensicCaptureBackbuffer(mDevice.Get(), mCommandQueue.Get(), bb, true, "title_spanish", f)) {
                titleCaptureDone = true;
                LogCouchD3D12("[COUCH-FORENSIC] capture title_spanish frame=" + std::to_string(f));
            }
        }

        // Forensic capture: previous frame content AFTER the temporal GPU path
        // (backbuffer still PRESENT from last frame's PRESENT_TRANSITION)
        if (forensic::g.lastFrameTemporal &&
            TemporalManager::Instance().GetMotionVectorBuffer() != nullptr &&
            forensic::g.nMotion < forensic::g.max) {
            ID3D12Resource* bb = mRenderTargets[mFrameIndex].Get();
            if (bb) {
                int f = (int)TemporalManager::Instance().GetFrameCounter();
                if (ForensicCaptureBackbuffer(mDevice.Get(), mCommandQueue.Get(),
                    bb, true, "after_temporal", f)) {
                    ++forensic::g.nMotion;
                    forensic::g.doneMotion = (forensic::g.nMotion >= 2);
                    LogCouchD3D12("[COUCH-FORENSIC] capture after_temporal frame=" + std::to_string(f));
                }
            }
        }
    }

    // RT4-A: Begin GPU raster timing scope
    GpuProfilerD3D12::Instance().BeginScope(mCommandList.Get(), "GPU_RASTER");

    ID3D12Resource* frameColor = FramebufferColor(0);
    auto& frameColorState = FramebufferColorState(0);
    TransitionResource(frameColor, frameColorState, D3D12_RESOURCE_STATE_RENDER_TARGET);
    frameColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = FramebufferRtv(0);

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = mDsvHeap->GetCPUDescriptorHandleForHeapStart();

    mCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    if (mSrvHeap) {
        ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get(), mSamplerHeap.Get() };
        mCommandList->SetDescriptorHeaps(mSamplerHeap ? 2 : 1, heaps);
    }

    if (mRootSignature) {
        mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
    }

    ctx.ringBufferBase = mFrameIndex * kRingBufferSliceSize;
    ctx.ringBufferUsed = 0;
}

void GfxRenderingAPIDX12::BeginUiOverlayPass() {
    if (!mInitialized || !mCommandListOpen || mDxrWorkAttemptedThisFrame || mDeviceRemoved) return;
    // OCZ-RTX-REPAIR REPAIR-01: explicit profile gate. CLASSIC (0) and
    // ENHANCED (1) never run DXR; HYBRID_RT (2) and PATH_TRACED_EXPERIMENTAL
    // (3) may run DXR only when the adapter proved compatible. The user
    // profile is never silently altered here; environment requests that
    // conflict with the effective profile are reported and ignored.
    const int effectiveProfile = MaterialRegistry::Instance().GetProfile();
    const bool profileSupportsHybridRt = DxrProfileSelected(effectiveProfile);
    if (effectiveProfile == static_cast<int>(VisualProfileId::CLASSIC)) {
        static bool loggedClassicBypass = false;
        if (!loggedClassicBypass) {
            LogCouchD3D12("[COUCH-DXR][PROFILE] DXR bypassed: Classic raster profile active (" +
                          std::to_string(effectiveProfile) + "); no BLAS/TLAS or DispatchRays issued");
            loggedClassicBypass = true;
        }
        return;
    }
    if (effectiveProfile == static_cast<int>(VisualProfileId::ENHANCED)) {
        static bool loggedEnhancedBypass = false;
        if (!loggedEnhancedBypass) {
            LogCouchD3D12("[COUCH-DXR][PROFILE] DXR bypassed: Enhanced raster profile active (" +
                          std::to_string(effectiveProfile) + "); no BLAS/TLAS or DispatchRays issued");
            loggedEnhancedBypass = true;
        }
        const char* envForce = getenv("COUCH_DXR_SURFACE_COMPOSITE");
        if (envForce && strcmp(envForce, "1") == 0) {
            static bool loggedEnvConflict = false;
            if (!loggedEnvConflict) {
                LogCouchD3D12("[COUCH-DXR][PROFILE] COUCH_DXR_SURFACE_COMPOSITE=1 ignored: effective profile is '" +
                              std::string(MaterialRegistry::Instance().GetProfileName()) +
                              "'; select Hybrid RT to enable DXR");
                loggedEnvConflict = true;
            }
        }
        return;
    }
    if (!profileSupportsHybridRt) {
        static bool loggedUnsupportedProfile = false;
        if (!loggedUnsupportedProfile) {
            LogCouchD3D12("[COUCH-DXR][PROFILE] DXR bypassed: unsupported visual profile id=" +
                          std::to_string(effectiveProfile));
            loggedUnsupportedProfile = true;
        }
        return;
    }
    {
        // Path-traced experimental shares the hybrid DXR activation path.
        static int loggedDxrProfile = -1;
        if (effectiveProfile != loggedDxrProfile) {
            LogCouchD3D12("[COUCH-DXR][PROFILE] DXR profile active: '" +
                          std::string(MaterialRegistry::Instance().GetProfileName()) + "' (" +
                          std::to_string(effectiveProfile) + ")");
            loggedDxrProfile = effectiveProfile;
        }
    }
    mDxrFrameEvidence.requested = profileSupportsHybridRt;
    mDxrWorkAttemptedThisFrame = true;

    const uint32_t sceneWidth=FramebufferWidth(mCurrentFramebuffer), sceneHeight=FramebufferHeight(mCurrentFramebuffer);
    if (sceneWidth!=mLastDxrWidth || sceneHeight!=mLastDxrHeight) {
        WaitForGpu();
        DxrShadowRenderer::Instance().OnResize(sceneWidth,sceneHeight);
        DxrSurfaceRenderer::Instance().OnResize(sceneWidth,sceneHeight);
        TemporalManager::Instance().OnResize(sceneWidth,sceneHeight);
        mLastDxrWidth=sceneWidth; mLastDxrHeight=sceneHeight;
    }
    // The renderer backend can be selected independently from the visual
    // profile. Only the explicit Hybrid RT profile may build acceleration
    // structures or run ray-traced effects; Enhanced remains raster-only.
    // (IsHybridRtProfile() covers both HYBRID_RT and PATH_TRACED_EXPERIMENTAL.)
    const int currentSceneId = GeometryCapture::Instance().GetCurrentSceneId();
    forensic::DxrDiagnosticSceneGateUpdate(forensic::gForensicFrame, currentSceneId);
    const int diagnosticStage = forensic::DxrDiagnosticSequenceStage(forensic::gForensicFrame);
    const bool diagnosticSequenceRequested = forensic::DxrDiagnosticSequenceEnabled();
    if (diagnosticSequenceRequested && forensic::DxrGateCounterSequenceEnabled()) {
        _putenv_s("COUCH_DXR_FORCE_ANY_HIT_COVERAGE", "0");
        if (diagnosticStage >= 6 && diagnosticStage <= 7) {
            LogCouchD3D12("[COUCH-DXR-GATE] FORCE_ANY_HIT_DISABLED=1 mode=" +
                          std::string(diagnosticStage == 6 ? "NORMAL" : "CANDIDATE_ALPHA_POLICY") +
                          " frame=" + std::to_string(forensic::gForensicFrame));
        }
    }
    const bool diagnosticSequenceActive = diagnosticStage >= 0;
    const bool diagnosticSequenceWaiting = diagnosticSequenceRequested &&
        forensic::DxrDiagnosticSequenceStartFrame() < 0;
    static int lastLoggedSceneId = -1;
    if (currentSceneId != lastLoggedSceneId || (forensic::gForensicFrame % 60) == 0) {
        LogCouchD3D12("[COUCH-DXR-FORENSIC][SCENE] frame=" +
                      std::to_string(forensic::gForensicFrame) + " sceneId=" +
                      std::to_string(currentSceneId) + " hex=0x" + [&]() {
                          char value[16]{};
                          sprintf_s(value, "%02X", currentSceneId & 0xFF);
                          return std::string(value);
                      }() + " room=" + std::to_string(GeometryCapture::Instance().GetCurrentRoomId()));
        lastLoggedSceneId = currentSceneId;
    }
    static int lastDiagnosticStage = -2;
    if (diagnosticSequenceActive && diagnosticStage != lastDiagnosticStage) {
        if (diagnosticStage == 5 && lastDiagnosticStage == 4) {
            TemporalManager::Instance().InvalidateHistory();
            LogCouchD3D12("[COUCH-FORENSIC][RTX_STAGE] invalidated temporal history before normal-composite stage");
        }
        static const char* stageNames[] = {
            "A_RAW_SHADOW_MASK", "B_SURFACE_COVERAGE", "C_RAW_REFLECTION",
            "D_RAW_INDIRECT", "E_RED_SURFACE_MASK_COMPOSITE", "F_NORMAL_COMPOSITE_TO_PRESENT",
            "G_PRIMARY_GATE_CHAIN", "H_CANDIDATE_FAST3D_ALPHA_POLICY"
        };
        static const char* truthStageNames[] = {
            "T_OFF", "T_PRIMARY_HIT_MISS", "T_WORLD_NORMAL",
            "T_HIT_DISTANCE", "T_IDS", "T_SYNTHETIC_LAMBERT"
        };
        static const char* truthV2StageNames[] = {
            "T2_WORLD_NORMAL", "T2_SYNTHETIC_LAMBERT", "T2_GAME_LIGHT_LAMBERT", "T2_OFF"
        };
        const bool isTruthStage = diagnosticStage >= 100 && diagnosticStage <= 105;
        const bool isTruthV2Stage = diagnosticStage >= 200 && diagnosticStage <= 203;
        const char* stageName = isTruthStage ? truthStageNames[diagnosticStage - 100]
            : (isTruthV2Stage ? truthV2StageNames[diagnosticStage - 200]
            : (diagnosticStage >= 0 && diagnosticStage <= 7 ? stageNames[diagnosticStage] : "UNKNOWN"));
        LogCouchD3D12("[COUCH-FORENSIC][RTX_STAGE] frame=" +
                      std::to_string(forensic::gForensicFrame) + " stage=" +
                      std::to_string(diagnosticStage) + " name=" + stageName);
        lastDiagnosticStage = diagnosticStage;
    }
    // OCZ-RTX-REPAIR REPAIR-01: DXR runs only on compatible adapters. The
    // acceleration-structure manager enforces HARD GATEs (raytracing tier >=
    // 1.0, ID3D12Device5) at Init; require both support and enable flags here
    // so no environment variable can force ray dispatches on incompatible
    // hardware. BLAS/TLAS are always processed before any effect that
    // consumes them (shadows, surfaces), in this same command list.
    const auto& asManager = DxrAccelerationStructureManager::Instance();
    const bool dxrAdapterReady = asManager.IsSupported() && asManager.IsEnabled();
    mDxrFrameEvidence.adapterCompatible = dxrAdapterReady;
    const bool dxrAsEnabled = profileSupportsHybridRt && dxrAdapterReady;
    const bool runShadowStage = !diagnosticSequenceWaiting &&
        (!diagnosticSequenceRequested || diagnosticStage == 0 || diagnosticStage == 5 ||
         (diagnosticStage >= 100 && diagnosticStage <= 105) ||
         (diagnosticStage >= 200 && diagnosticStage <= 203));
    const bool runSurfaceStage = !diagnosticSequenceWaiting &&
        (!diagnosticSequenceRequested || (diagnosticStage >= 1 && diagnosticStage <= 8) ||
         (diagnosticStage >= 100 && diagnosticStage <= 105) ||
         (diagnosticStage >= 200 && diagnosticStage <= 203));
    const bool dxrShadowEnabled = profileSupportsHybridRt && DxrShadowRenderer::Instance().IsEnabled() && runShadowStage;
    const bool dxrSurfaceEnabled = profileSupportsHybridRt && DxrSurfaceRenderer::Instance().IsEnabled() && runSurfaceStage;
    if (!dxrAsEnabled && !dxrShadowEnabled && !dxrSurfaceEnabled) return;

    // DXR renderers change the command list's graphics state (heaps, root
    // signature, viewport/scissor and render targets). Fast3D continues with
    // HUD/menu draws immediately after this hook, so restore its tracked state
    // before returning to the interpreter.
    const auto restoreRasterState = [this]() { RestoreRasterState(); };

    // GeometryCapture has ingested every world draw up to this point. Build
    // the frame TLAS now, while the command list still contains only the 3D
    // scene; subsequent Fast3D UI draws are then naturally layered on top.
    // OCZ-RTX-REPAIR REPAIR-01: every BeginScope below is paired with exactly
    // one EndScope on every path, and mDxrPassesRunThisFrame is set only when
    // ray work was actually recorded (a valid TLAS feeding at least one
    // effect stage). Raster scope pairing is preserved by re-opening
    // GPU_RASTER after the DXR section so EndFrame keeps its match.
    bool rasterScopeOpen = true;
    GpuProfilerD3D12::Instance().EndScope(mCommandList.Get(), "GPU_RASTER");
    rasterScopeOpen = false;
    if (dxrAsEnabled) {
        GpuProfilerD3D12::Instance().BeginScope(mCommandList.Get(), "GPU_BLAS_TLAS");
        DxrAccelerationStructureManager::Instance().ProcessFrame(
            mDevice.Get(), mCommandList.Get(), mFrameIndex, mCurrentFenceValue, mFence->GetCompletedValue());
        GpuProfilerD3D12::Instance().EndScope(mCommandList.Get(), "GPU_BLAS_TLAS");
    }

    if (forensic::ForensicCaptureWindow(forensic::gForensicFrame)) {
        forensic::ForensicEnqueueInFrameCopy(mCommandList.Get(), mDevice.Get(),
            FramebufferColor(mCurrentFramebuffer), 0, "world_raster", forensic::gForensicFrame);
        CaptureDxrComparisonImage(0, FramebufferColor(mCurrentFramebuffer),
            FramebufferColorState(mCurrentFramebuffer), forensic::gForensicFrame);
    }
    const uint64_t tlasGpuVA = DxrAccelerationStructureManager::Instance().GetTlasGpuVA();
    mDxrFrameEvidence.tlasValid = tlasGpuVA != 0;
    if (tlasGpuVA == 0) {
        // OCZ-RTX-REPAIR REPAIR-01: unavailable resources must not cause
        // invalid access or device loss. No ray stage can run without a TLAS;
        // leave the raster image intact and keep profiler pairing exact.
        static bool loggedTlasUnavailable = false;
        if (dxrAsEnabled && !loggedTlasUnavailable) {
            LogCouchD3D12("[COUCH-DXR][TLAS] no valid TLAS this frame; ray stages skipped, raster preserved");
            loggedTlasUnavailable = true;
        }
        restoreRasterState();
        if (!rasterScopeOpen) {
            GpuProfilerD3D12::Instance().BeginScope(mCommandList.Get(), "GPU_RASTER");
            rasterScopeOpen = true;
        }
        return;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = FramebufferRtv(mCurrentFramebuffer);
    const uint64_t shadowDispatchesBefore = DxrShadowRenderer::Instance().GetStats().dispatchRaysCalls;
    const uint64_t shadowSignalsBefore = DxrShadowRenderer::Instance().GetStats().signalOutputsProduced;
    const uint64_t shadowCompositesBefore = DxrShadowRenderer::Instance().GetStats().compositePasses;
    const uint64_t surfaceDispatchesBefore = DxrSurfaceRenderer::Instance().GetStats().dispatchRaysCalls;
    const uint64_t surfaceSignalsBefore = DxrSurfaceRenderer::Instance().GetStats().signalOutputsProduced;
    const uint64_t surfaceCompositesBefore = DxrSurfaceRenderer::Instance().GetStats().compositeExecutions;

    if (dxrShadowEnabled) {
        GpuProfilerD3D12::Instance().BeginScope(mCommandList.Get(), "GPU_DXR_SHADOW");
        if (TemporalManager::Instance().IsEnabled()) {
            TemporalManager::Instance().DispatchMotionVectors(
                mCommandList.Get(), FramebufferDepth(mCurrentFramebuffer), FramebufferDepthSrv(mCurrentFramebuffer),
                FramebufferWidth(mCurrentFramebuffer), FramebufferHeight(mCurrentFramebuffer));
        }
        DxrShadowRenderer::Instance().RenderShadows(
            mDevice.Get(), mCommandList.Get(), FramebufferColor(mCurrentFramebuffer), rtvHandle,
            FramebufferDepth(mCurrentFramebuffer), FramebufferDepthSrv(mCurrentFramebuffer), tlasGpuVA,
            FramebufferWidth(mCurrentFramebuffer), FramebufferHeight(mCurrentFramebuffer), mFrameIndex, mCurrentFenceValue,
            DxrAccelerationStructureManager::Instance().GetSurfaceFrameBuffers(mFrameIndex),
            mSrvCpuHeap.Get(), mSrvDescriptorSize,
            diagnosticSequenceActive && diagnosticStage == 0,
            diagnosticSequenceActive && diagnosticStage >= 0 && diagnosticStage <= 4);
        GpuProfilerD3D12::Instance().EndScope(mCommandList.Get(), "GPU_DXR_SHADOW");
    }
    if (dxrSurfaceEnabled) {
        GpuProfilerD3D12::Instance().BeginScope(mCommandList.Get(), "GPU_DXR_SURFACE");
        const auto& surfaceData = DxrAccelerationStructureManager::Instance().GetSurfaceFrameBuffers(mFrameIndex);
        // DXR Truth Mode (V1) view selection. Forensic sequence stages map
        // 1:1 to legacy diagnosticViews; truth auto-stages 100..105 map to
        // DxrTruthView OFF/10..14; V2 auto-stages 200..203 map to 11/14/15/0;
        // with no sequence active, the runtime TruthMode cvar (or
        // COUCH_DXR_TRUTH_MODE) selects OFF or 10..15.
        // No path here recompiles anything; OFF keeps the exact normal path.
        int surfaceDiagnosticView = 0;
        if (diagnosticSequenceActive && diagnosticStage >= 1 && diagnosticStage <= 4)
            surfaceDiagnosticView = diagnosticStage;
        else if (diagnosticStage == 6)
            surfaceDiagnosticView = 5;
        else if (diagnosticStage == 7)
            surfaceDiagnosticView = 8;
        else if (diagnosticStage >= 100 && diagnosticStage <= 105)
            surfaceDiagnosticView = diagnosticStage == 100 ? 0 : diagnosticStage - 91;
        else if (diagnosticStage >= 200 && diagnosticStage <= 203) {
            // V2 sweep: 200=NORMAL(11), 201=SYNTHETIC(14), 202=GAME_LIGHT(15), 203=OFF(0).
            static const int v2Views[] = { 11, 14, 15, 0 };
            surfaceDiagnosticView = v2Views[diagnosticStage - 200];
        }
        else if (!diagnosticSequenceActive) {
            surfaceDiagnosticView = forensic::DxrTruthManualView();
            // Sample the gate only on the harness capture frame. This view
            // deliberately bypasses AnyHit alpha to report structural hits;
            // it must never replace the normal-path Truth measurements.
            const char* harness = getenv("COUCH_DXR_HARNESS");
            const char* manualGate = getenv("COUCH_DXR_PRIMARY_GATE_COUNTERS");
            if (surfaceDiagnosticView == 0 && forensic::SceneHarnessCaptureRequested() &&
                harness && strcmp(harness, "1") == 0 &&
                manualGate && strcmp(manualGate, "1") == 0 &&
                forensic::DxrGateCounterSequenceEnabled()) {
                surfaceDiagnosticView = 5;
            }
        }
        DxrSurfaceRenderer::Instance().RenderReflections(
            mDevice.Get(), mCommandList.Get(), FramebufferColor(mCurrentFramebuffer), rtvHandle,
            FramebufferDepth(mCurrentFramebuffer), FramebufferDepthSrv(mCurrentFramebuffer), tlasGpuVA,
            FramebufferWidth(mCurrentFramebuffer), FramebufferHeight(mCurrentFramebuffer), mFrameIndex, mCurrentFenceValue,
            surfaceData, mSrvCpuHeap.Get(), mSrvDescriptorSize,
            surfaceDiagnosticView,
            forensic::gForensicFrame, currentSceneId,
            GeometryCapture::Instance().GetCurrentRoomId(),
            DxrAccelerationStructureManager::Instance().GetStats().tlasInstanceCount);
        GpuProfilerD3D12::Instance().EndScope(mCommandList.Get(), "GPU_DXR_SURFACE");
    }
    const auto& shadowStatsAfter = DxrShadowRenderer::Instance().GetStats();
    const auto& surfaceStatsAfter = DxrSurfaceRenderer::Instance().GetStats();
    const uint64_t shadowDispatchDelta = shadowStatsAfter.dispatchRaysCalls >= shadowDispatchesBefore
        ? shadowStatsAfter.dispatchRaysCalls - shadowDispatchesBefore : 0;
    const uint64_t surfaceDispatchDelta = surfaceStatsAfter.dispatchRaysCalls >= surfaceDispatchesBefore
        ? surfaceStatsAfter.dispatchRaysCalls - surfaceDispatchesBefore : 0;
    const uint64_t shadowSignalDelta = shadowStatsAfter.signalOutputsProduced >= shadowSignalsBefore
        ? shadowStatsAfter.signalOutputsProduced - shadowSignalsBefore : 0;
    const uint64_t surfaceSignalDelta = surfaceStatsAfter.signalOutputsProduced >= surfaceSignalsBefore
        ? surfaceStatsAfter.signalOutputsProduced - surfaceSignalsBefore : 0;
    const uint64_t shadowCompositeDelta = shadowDispatchDelta > 0 && shadowStatsAfter.compositePasses >= shadowCompositesBefore
        ? shadowStatsAfter.compositePasses - shadowCompositesBefore : 0;
    const uint64_t surfaceCompositeDelta = surfaceStatsAfter.compositeExecutions >= surfaceCompositesBefore
        ? surfaceStatsAfter.compositeExecutions - surfaceCompositesBefore : 0;
    mDxrFrameEvidence.dispatchCalls = shadowDispatchDelta + surfaceDispatchDelta;
    mDxrFrameEvidence.signalOutputsWritten = shadowSignalDelta + surfaceSignalDelta;
    mDxrFrameEvidence.compositeDraws = shadowCompositeDelta + surfaceCompositeDelta;
    mDxrPassesRunThisFrame = mDxrFrameEvidence.DispatchExecuted();
    forensic::ForensicLog("[COUCH-DXR-FRAME] frame=" + std::to_string(mDxrFrameEvidence.frame) +
                " requested=" + std::to_string(mDxrFrameEvidence.requested ? 1 : 0) +
                " adapterCompatible=" + std::to_string(mDxrFrameEvidence.adapterCompatible ? 1 : 0) +
                " tlasValid=" + std::to_string(mDxrFrameEvidence.tlasValid ? 1 : 0) +
                " dispatchCalls=" + std::to_string(mDxrFrameEvidence.dispatchCalls) +
                " signalOutputsWritten=" + std::to_string(mDxrFrameEvidence.signalOutputsWritten) +
                " compositeDraws=" + std::to_string(mDxrFrameEvidence.compositeDraws) +
                " dispatchExecuted=" + std::to_string(mDxrFrameEvidence.DispatchExecuted() ? 1 : 0) +
                " signalsProduced=" + std::to_string(mDxrFrameEvidence.SignalsProduced() ? 1 : 0) +
                " compositionExecuted=" + std::to_string(mDxrFrameEvidence.CompositionExecuted() ? 1 : 0));
    ID3D12Resource* surfaceSceneColor = DxrSurfaceRenderer::Instance().GetRasterSceneColorCopyForCapture();
    if (forensic::ForensicCaptureWindow(forensic::gForensicFrame) && dxrSurfaceEnabled && surfaceSceneColor) {
        forensic::ForensicEnqueueInFrameCopy(mCommandList.Get(), mDevice.Get(),
            surfaceSceneColor, 2, "raster_scene_color_copy", forensic::gForensicFrame,
            static_cast<D3D12_RESOURCE_STATES>(DxrSurfaceRenderer::Instance().GetSceneColorCopyState()));
        CaptureDxrComparisonImage(2, surfaceSceneColor,
            static_cast<D3D12_RESOURCE_STATES>(DxrSurfaceRenderer::Instance().GetSceneColorCopyState()), forensic::gForensicFrame);
    }
    if (forensic::ForensicCaptureWindow(forensic::gForensicFrame) && dxrSurfaceEnabled) {
        auto& surfaceRenderer = DxrSurfaceRenderer::Instance();
        if (!surfaceRenderer.IsNaviPilotDispatch()) {
            if (ID3D12Resource* direct = surfaceRenderer.GetNativePointDirectForCapture()) {
                forensic::ForensicEnqueueInFrameCopy(mCommandList.Get(), mDevice.Get(), direct, 17,
                    "navi_point_direct", forensic::gForensicFrame,
                    static_cast<D3D12_RESOURCE_STATES>(surfaceRenderer.GetNativePointDirectState()));
            }
        }
        if (ID3D12Resource* reflection = surfaceRenderer.GetReflectionColorForCapture()) {
            forensic::ForensicEnqueueInFrameCopy(mCommandList.Get(), mDevice.Get(), reflection, 5,
                "dxr_reflection_color", forensic::gForensicFrame,
                static_cast<D3D12_RESOURCE_STATES>(surfaceRenderer.GetReflectionColorState()));
        }
        if (ID3D12Resource* indirect = surfaceRenderer.GetIndirectMaskForCapture()) {
            forensic::ForensicEnqueueInFrameCopy(mCommandList.Get(), mDevice.Get(), indirect, 6,
                "dxr_indirect_mask", forensic::gForensicFrame,
                static_cast<D3D12_RESOURCE_STATES>(surfaceRenderer.GetIndirectMaskState()));
        }
        if (ID3D12Resource* emissive = surfaceRenderer.GetEmissiveMaskForCapture()) {
            forensic::ForensicEnqueueInFrameCopy(mCommandList.Get(), mDevice.Get(), emissive, 7,
                "emissive_mask", forensic::gForensicFrame,
                static_cast<D3D12_RESOURCE_STATES>(surfaceRenderer.GetEmissiveMaskState()));
        }
        const unsigned controlCount = surfaceRenderer.IsNaviPilotDispatch() ? 3u : 4u;
        for (unsigned i = 0; i < controlCount; ++i) {
            if (auto* control = surfaceRenderer.GetVisualAblationForCapture(i, forensic::gForensicFrame)) {
                const int ablationSlot = i == 3 ? forensic::SceneHarnessComparisonSlot(6)
                                                : forensic::SceneHarnessAblationSlot(int(i));
                if (ablationSlot < 0) continue;
                const char* controlName = i == 3 ? "navi_point_off" :
                    i == 0 ? "surface_optics_off" : (i == 1 ? "indirect_off" : "relight_off");
                // Slot 18 is the toned production comparison image. Keep its
                // only readback here; the point-off HDR target remains in the
                // renderer and must not occupy the same slot ahead of SDR.
                if (i != 3) {
                    forensic::ForensicEnqueueInFrameCopy(mCommandList.Get(), mDevice.Get(), control,
                        ablationSlot, controlName,
                        forensic::gForensicFrame, D3D12_RESOURCE_STATE_RENDER_TARGET);
                }
                CaptureDxrComparisonImage(3 + i, control, D3D12_RESOURCE_STATE_RENDER_TARGET, forensic::gForensicFrame);
            }
        }
        if (!surfaceRenderer.IsNaviPilotDispatch()) {
            // Capture the normal production compositor output alongside the
            // same-frame point-off ablation; both use the production ACES path.
            CaptureDxrComparisonImage(7, FramebufferColor(mCurrentFramebuffer),
                FramebufferColorState(mCurrentFramebuffer), forensic::gForensicFrame);
        }
    }

    // This hook runs before EndFrame's legacy post-composite readback, whose
    // separate immediate command-list capture can observe a stale buffer. Use
    // the same in-frame copy path as the raster capture so DXR output is
    // recorded from this command list after the compositor has completed.
    if (forensic::ForensicCaptureWindow(forensic::gForensicFrame) &&
        forensic::gInFrameCompositeCaptures < forensic::kInFrameMaxCaptures) {
        forensic::ForensicEnqueueInFrameCopy(mCommandList.Get(), mDevice.Get(),
            FramebufferColor(mCurrentFramebuffer), 1, "after_composite",
            forensic::gForensicFrame);
        ++forensic::gInFrameCompositeCaptures;
        CaptureDxrComparisonImage(1, FramebufferColor(mCurrentFramebuffer),
            FramebufferColorState(mCurrentFramebuffer), forensic::gForensicFrame);
    }

    restoreRasterState();
    if (!rasterScopeOpen) {
        // OCZ-RTX-REPAIR REPAIR-01: re-open the raster scope so EndFrame's
        // EndScope("GPU_RASTER") keeps its match and HUD/menu draws that
        // follow this hook stay inside measured raster time.
        GpuProfilerD3D12::Instance().BeginScope(mCommandList.Get(), "GPU_RASTER");
    }
}

void GfxRenderingAPIDX12::EndFrame() {
    if (!mInitialized || !mCommandListOpen) return;
    if (mHdrSceneActive) RunTonemappingPass(0.40f, 0.42f, 0.45f, 0.0f, 0.3f, 0.7f);
    mLastFrameHadDxr = mDxrPassesRunThisFrame;
    GpuProfilerD3D12::Instance().EndScope(mCommandList.Get(), "GPU_RASTER");
    if (mPixelIsoMode >= 20 || mR7c5A5Enabled) {
        R4bFinalizeStatsQuery(); R4bFrameFlushAndReport();
    }
    // Final backbuffer checkpoint after scene, compositor, and UI draws. This
    // copy is recorded before Present and does not prove display scanout.
    if (forensic::ForensicCaptureWindow(forensic::gForensicFrame)) {
        forensic::ForensicEnqueueInFrameCopy(mCommandList.Get(),mDevice.Get(),
            mRenderTargets[mFrameIndex].Get(),3,"capture_raw_readback",forensic::gForensicFrame,
            mBackbufferState[mFrameIndex]);
        char trace[256];
        snprintf(trace,sizeof(trace),"[COUCH-READBACK] frame=%llu index=%u source=%p cmdlist=%p fence=%llu",
            (unsigned long long)(mPresentedFrames+1),mFrameIndex,mRenderTargets[mFrameIndex].Get(),mCommandList.Get(),
            (unsigned long long)mCurrentFenceValue);
        LogCouchD3D12(trace);
    }
    const bool capturePresentPair = forensic::ForensicCaptureWindow(forensic::gForensicFrame);
    if (capturePresentPair) {
        const auto finalDesc = mRenderTargets[mFrameIndex]->GetDesc();
        auto* rasterColor = FramebufferColor(mCurrentFramebuffer);
        const DXGI_FORMAT rasterFormat = rasterColor ? rasterColor->GetDesc().Format : DXGI_FORMAT_UNKNOWN;
        LogCouchD3D12("[COUCH-FORENSIC][FINAL_DRAW_RECORDED] frame=" + std::to_string(forensic::gForensicFrame) +
            " preCompositeRTFormat=" + std::to_string(static_cast<unsigned>(rasterFormat)) +
            " postCompositeTargetFormat=" + std::to_string(static_cast<unsigned>(finalDesc.Format)) +
            " target=" + std::to_string(reinterpret_cast<uintptr_t>(mRenderTargets[mFrameIndex].Get())) +
            " cmdlist=" + std::to_string(reinterpret_cast<uintptr_t>(mCommandList.Get())));
    }
    TransitionResource(mRenderTargets[mFrameIndex].Get(),mBackbufferState[mFrameIndex],D3D12_RESOURCE_STATE_PRESENT);
    mBackbufferState[mFrameIndex]=D3D12_RESOURCE_STATE_PRESENT;
    if (capturePresentPair) {
        forensic::ForensicEnqueueInFrameCopy(mCommandList.Get(), mDevice.Get(),
            mRenderTargets[mFrameIndex].Get(), 4, "capture_backbuffer_pre_present", forensic::gForensicFrame,
            D3D12_RESOURCE_STATE_PRESENT);
        LogCouchD3D12("[COUCH-FORENSIC][FINAL_READBACK_COPY_RECORDED] frame=" + std::to_string(forensic::gForensicFrame) +
            " stage=pre_present stateBefore=PRESENT stateAfter=PRESENT resource=" +
            std::to_string(reinterpret_cast<uintptr_t>(mRenderTargets[mFrameIndex].Get())) +
            " cmdlist=" + std::to_string(reinterpret_cast<uintptr_t>(mCommandList.Get())));
    }
    GpuProfilerD3D12::Instance().EndFrame(mCommandList.Get(),mCurrentFenceValue);
    const HRESULT closeResult = mCommandList->Close();
    if (FAILED(closeResult)) {
        std::ostringstream closeFailure;
        closeFailure << "[COUCH-D3D12][COMMAND_LIST_CLOSE_FAILED] hr=0x" << std::hex
                     << static_cast<unsigned long>(closeResult)
                     << " deviceRemoved=0x"
                     << static_cast<unsigned long>(mDevice->GetDeviceRemovedReason());
        LogCouchD3D12(closeFailure.str());
        DumpDebugMessages();
        throw std::runtime_error("D3D12 command list close failed");
    }
    mCommandListOpen=false;
    TemporalManager::Instance().EndFrame(nullptr,mFrameIndex);
    mWindowBackend->RecordD3D12Submission(mPresentedFrames+1,mRenderTargets[mFrameIndex].Get(),
        FramebufferRtv(0).ptr,mCommandList.Get(),mCurrentFenceValue,capturePresentPair,forensic::gForensicFrame);
    ID3D12CommandList* lists[]={mCommandList.Get()};
    mCommandQueue->ExecuteCommandLists(1,lists);
    DxrSurfaceRenderer::Instance().ResolvePrimaryGateCounters(mCommandQueue.Get());
    if (capturePresentPair) LogCouchD3D12("[COUCH-FORENSIC][COMMAND_LIST_EXECUTED] frame=" + std::to_string(forensic::gForensicFrame) +
        " cmdlist=" + std::to_string(reinterpret_cast<uintptr_t>(mCommandList.Get())) +
        " fence=" + std::to_string(mCurrentFenceValue));
    // Resolve readbacks after command execution and before DXGI Present.
    if (capturePresentPair) forensic::ForensicResolveInFrameCaptures(mDevice.Get(), mCommandQueue.Get());
    const HRESULT reason=mDevice->GetDeviceRemovedReason();
    if (reason!=S_OK) {
        mDeviceRemoved=true;
        LogCouchD3D12("[COUCH-D3D12][DEVICE_REMOVED] HRESULT="+std::to_string(reason));
        DumpDebugMessages();
    }
}

void GfxRenderingAPIDX12::FinishRender() {
    if (!mInitialized || !mSwapChain) return;

    MoveToNextFrame();

    mPresentedFrames++;
    if (mPresentedFrames <= 10 || mPresentedFrames % 60 == 0) {
        LogCouchD3D12("[COUCH-D3D12] Frames presented = " + std::to_string(mPresentedFrames));
        LogCouchD3D12("[COUCH-D3D12] Fast3D draws = " + std::to_string(mDrawCount));
        LogCouchD3D12("[COUCH-D3D12] Vertices submitted = " + std::to_string(mVertexCount));
        LogCouchD3D12("[COUCH-D3D12] Triangles submitted = " + std::to_string(mTriangleCount));
        LogCouchD3D12("[COUCH-D3D12] Invalid draws skipped = " + std::to_string(mInvalidDrawCount));
        LogCouchD3D12("[COUCH-D3D12] SRV descriptors used = " + std::to_string(mNextSrvIndex));
        if (mDeviceRemoved) {
            LogCouchD3D12("[COUCH-D3D12] *** DEVICE WAS REMOVED - investigation required ***");
        }
    }
}

// ============================================================
// R4 — Diagnostic PSO creation for pixel output isolation
// ============================================================
void GfxRenderingAPIDX12::CreateDiagnosticPSOs() {
    if (mDiagnosticPSOsCreated) return;
    if (!mDevice || !mRootSignature || !mSavedVSBlob) return;
    if (mSavedInputElements.empty()) return;

    const char* psSrc =
        "float4 PSMain() : SV_Target {\n"
        "    return float4(1.0, 0.0, 1.0, 1.0);\n"
        "}\n";

    ComPtr<ID3DBlob> psBlob, errorBlob;
    HRESULT hr = mD3dCompile(psSrc, (UINT)strlen(psSrc), "DiagMagentaPS", nullptr, nullptr,
                              "PSMain", "ps_5_0", 0, 0, &psBlob, &errorBlob);
    if (FAILED(hr)) {
        LogCouchD3D12("[COUCH-R4] Magenta PS compile FAILED: " +
                      std::string(errorBlob ? (char*)errorBlob->GetBufferPointer() : "unknown"));
        return;
    }

    auto createPSO = [&](bool depthEnable, D3D12_CULL_MODE cullMode, bool blendEnable) -> ComPtr<ID3D12PipelineState> {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = mRootSignature.Get();
        desc.VS = { mSavedVSBlob->GetBufferPointer(), mSavedVSBlob->GetBufferSize() };
        desc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
        desc.InputLayout = { mSavedInputElements.data(), (UINT)mSavedInputElements.size() };
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.SampleMask = UINT_MAX;
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = cullMode;
        desc.RasterizerState.FrontCounterClockwise = FALSE;
        desc.RasterizerState.DepthClipEnable = TRUE;
        desc.DepthStencilState.DepthEnable = depthEnable ? TRUE : FALSE;
        desc.DepthStencilState.DepthWriteMask = depthEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        auto& rt = desc.BlendState.RenderTarget[0];
        rt.BlendEnable = blendEnable ? TRUE : FALSE;
        rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        if (blendEnable) {
            rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            rt.BlendOp = D3D12_BLEND_OP_ADD;
            rt.SrcBlendAlpha = D3D12_BLEND_ONE;
            rt.DestBlendAlpha = D3D12_BLEND_ZERO;
            rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        }
        ComPtr<ID3D12PipelineState> pso;
        if (SUCCEEDED(mDevice->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso)))) return pso;
        return nullptr;
    };

    // P0: solid magenta PS, depth ON (always pass), cull original, blend OFF
    mDiagnosticPSO_P0 = createPSO(true, D3D12_CULL_MODE_NONE, false);
    // P1: solid magenta PS, depth OFF, cull NONE, blend OFF
    mDiagnosticPSO_P1 = createPSO(false, D3D12_CULL_MODE_NONE, false);
    // P2 = P1 (same as P1 since both have depth OFF + cull NONE)
    mDiagnosticPSO_P2 = mDiagnosticPSO_P1;
    // P4: solid magenta PS, depth OFF, cull NONE — but needs known VS (created separately)

    if (mDiagnosticPSO_P0)
        LogCouchD3D12("[COUCH-R4] Diagnostic PSOs created: P0=OK P1=OK");
    else
        LogCouchD3D12("[COUCH-R4] Diagnostic PSO creation FAILED");

    mDiagnosticPSOsCreated = true;
}

// ============================================================
// R4-B — Valid-Vertex Canonical Draw protocol
// ============================================================
void GfxRenderingAPIDX12::CreateR4bDiagnosticPSOs() {
    if (mR4bPSO_C4) return;
    if (!mDevice || !mRootSignature || !mSavedVSBlob) return;
    if (mSavedInputElements.empty()) return;

    // Helper: build a diagnostic PSO from (VS blob, PS blob, state overrides)
    auto makeDiagPSO = [&](const void* vsData, size_t vsSize, const void* psData, size_t psSize,
                           bool depthEnable, D3D12_CULL_MODE cullMode, bool blendEnable) -> ComPtr<ID3D12PipelineState> {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = mRootSignature.Get();
        desc.VS = { vsData, vsSize };
        desc.PS = { psData, psSize };
        desc.InputLayout = { mSavedInputElements.data(), (UINT)mSavedInputElements.size() };
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.SampleMask = UINT_MAX;
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = cullMode;
        desc.RasterizerState.FrontCounterClockwise = FALSE;
        desc.RasterizerState.DepthClipEnable = TRUE;
        desc.DepthStencilState.DepthEnable = depthEnable ? TRUE : FALSE;
        desc.DepthStencilState.DepthWriteMask = depthEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        auto& rt = desc.BlendState.RenderTarget[0];
        rt.BlendEnable = blendEnable ? TRUE : FALSE;
        rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        if (blendEnable) {
            rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            rt.BlendOp = D3D12_BLEND_OP_ADD;
            rt.SrcBlendAlpha = D3D12_BLEND_ONE;
            rt.DestBlendAlpha = D3D12_BLEND_ZERO;
            rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        }
        ComPtr<ID3D12PipelineState> pso;
        if (SUCCEEDED(mDevice->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso)))) return pso;
        return nullptr;
    };

    auto createR4bPSOs = [&](void) {
        if (!mD3dCompile) return;
        ComPtr<ID3DBlob> psMagenta, errorBlob;
        const char* psSrc =
            "float4 PSMain() : SV_Target {\n"
            "    return float4(1.0, 0.0, 1.0, 1.0);\n"
            "}\n";
        if (FAILED(mD3dCompile(psSrc, (UINT)strlen(psSrc), "R4bMagentaPS", nullptr, nullptr,
                               "PSMain", "ps_5_0", 0, 0, &psMagenta, &errorBlob))) {
            LogCouchD3D12("[COUCH-R4B] Magenta PS compile FAILED: " +
                          std::string(errorBlob ? (char*)errorBlob->GetBufferPointer() : "unknown"));
            return;
        }

        // C0: solid magenta PS, VS original, estados originales (depth según canonical)
        mR4bPSO_C0 = makeDiagPSO(mSavedVSBlob->GetBufferPointer(), mSavedVSBlob->GetBufferSize(),
                                 psMagenta->GetBufferPointer(), psMagenta->GetBufferSize(),
                                 true, D3D12_CULL_MODE_NONE, false);
        // C1: igual que C0 pero DepthEnable=FALSE / DepthWriteMask=ZERO
        mR4bPSO_C1 = makeDiagPSO(mSavedVSBlob->GetBufferPointer(), mSavedVSBlob->GetBufferSize(),
                                 psMagenta->GetBufferPointer(), psMagenta->GetBufferSize(),
                                 false, D3D12_CULL_MODE_NONE, false);
        // C2: igual que C1 pero CullMode=NONE (C1 ya es NONE; se conserva el slot por claridad)
        mR4bPSO_C2 = makeDiagPSO(mSavedVSBlob->GetBufferPointer(), mSavedVSBlob->GetBufferSize(),
                                 psMagenta->GetBufferPointer(), psMagenta->GetBufferSize(),
                                 false, D3D12_CULL_MODE_NONE, false);
        // C3: igual que C2; el delta de C3 es el viewport/scissor forzado en runtime
        mR4bPSO_C3 = makeDiagPSO(mSavedVSBlob->GetBufferPointer(), mSavedVSBlob->GetBufferSize(),
                                 psMagenta->GetBufferPointer(), psMagenta->GetBufferSize(),
                                 false, D3D12_CULL_MODE_NONE, false);

        // C4: VS diagnóstico por SV_VertexID — triángulo fijo fullscreen-fraction
        ComPtr<ID3DBlob> vsKnown, vsErr;
        const char* vsSrc =
            "struct VSOut { float4 pos : SV_Position; };\n"
            "VSOut VSMain(uint vid : SV_VertexID) {\n"
            "    VSOut o;\n"
            "    float2 p = float2(0.0, 0.0);\n"
            "    if (vid == 0) p = float2(-0.5, -0.5);\n"
            "    if (vid == 1) p = float2( 0.0,  0.5);\n"
            "    if (vid == 2) p = float2( 0.5, -0.5);\n"
            "    o.pos = float4(p, 0.0, 1.0);\n"
            "    return o;\n"
            "}\n";
        if (FAILED(mD3dCompile(vsSrc, (UINT)strlen(vsSrc), "R4bKnownVS", nullptr, nullptr,
                               "VSMain", "vs_5_0", 0, 0, &vsKnown, &vsErr))) {
            LogCouchD3D12("[COUCH-R4B] Known VS compile FAILED: " +
                          std::string(vsErr ? (char*)vsErr->GetBufferPointer() : "unknown"));
            return;
        }
        mR4bKnownVSBlob = vsKnown;
        {
            // El known VS no consume input layout: PSO sin input elements
            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
            desc.pRootSignature = mRootSignature.Get();
            desc.VS = { vsKnown->GetBufferPointer(), vsKnown->GetBufferSize() };
            desc.PS = { psMagenta->GetBufferPointer(), psMagenta->GetBufferSize() };
            desc.InputLayout = { nullptr, 0 };
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            desc.NumRenderTargets = 1;
            desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            desc.SampleDesc.Count = 1;
            desc.SampleMask = UINT_MAX;
            desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            desc.RasterizerState.DepthClipEnable = TRUE;
            desc.DepthStencilState.DepthEnable = FALSE;
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            ComPtr<ID3D12PipelineState> pso;
            if (SUCCEEDED(mDevice->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso)))) {
                mR4bPSO_C4 = pso;
            } else {
                LogCouchD3D12("[COUCH-R4B] C4 PSO creation FAILED");
            }
        }

        int okCount = (mR4bPSO_C0 ? 1 : 0) + (mR4bPSO_C1 ? 1 : 0) + (mR4bPSO_C2 ? 1 : 0) +
                      (mR4bPSO_C3 ? 1 : 0) + (mR4bPSO_C4 ? 1 : 0);
        LogCouchD3D12("[COUCH-R4B] Diagnostic PSOs: C0=" + std::string(mR4bPSO_C0 ? "OK" : "FAIL") +
                      " C1=" + std::string(mR4bPSO_C1 ? "OK" : "FAIL") +
                      " C2=" + std::string(mR4bPSO_C2 ? "OK" : "FAIL") +
                      " C3=" + std::string(mR4bPSO_C3 ? "OK" : "FAIL") +
                      " C4=" + std::string(mR4bPSO_C4 ? "OK" : "FAIL") +
                      " (" + std::to_string(okCount) + "/5)");
    };

    createR4bPSOs();

    // MakeCanonicalLine: construye la línea de log de un vértice del canonical draw
    auto makeCanonicalLine = [](const float* v, int floatsPerVtx) -> std::string {
        std::string vtx;
        for (int f = 0; f < floatsPerVtx; f++) {
            vtx += "f" + std::to_string(f) + "=" + std::to_string(v[f]) + " ";
        }
        return vtx;
    };
    (void)makeCanonicalLine;
}

void GfxRenderingAPIDX12::BeginR4bFrameStats() {
    if (mPixelIsoMode < 20 && !mR7c5A5Enabled) return;
    if (!mDevice || !mCommandList || mR4bStatsHeap) return;

    mR4bStatsHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
    mR4bStatsHeapDesc.Count = 1;
    mR4bStatsHeapDesc.NodeMask = 0;
    HRESULT hrHeap = mDevice->CreateQueryHeap(&mR4bStatsHeapDesc, IID_PPV_ARGS(&mR4bStatsHeap));
    if (FAILED(hrHeap)) {
        LogCouchD3D12("[COUCH-R4B] PipelineStatistics query heap FAILED hr=0x" +
                      std::to_string((unsigned)hrHeap));
        return;
    }
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS) + 16;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(mDevice->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                IID_PPV_ARGS(&mR4bStatsReadback)))) {
        LogCouchD3D12("[COUCH-R4B] PipelineStatistics readback buffer FAILED");
        return;
    }
    mR4bStatsQueryStartFrame = 0;
}

void GfxRenderingAPIDX12::R4bFinalizeStatsQuery() {
    // EndQuery + ResolveQueryData for the per-frame pipeline statistics begun in StartFrame.
    // The actual readback + report happen in R4bFrameFlushAndReport after the GPU finishes.
    if (mPixelIsoMode < 20 && !mR7c5A5Enabled) return;
    if (!mCommandList || !mR4bStatsHeap || !mR4bStatsReadback) return;
    if (mR4bStatsQueryStartFrame == 0) return;
    if (!mR7c5A5Enabled || !mR7c5A5QueryDone) {
        mCommandList->EndQuery(mR4bStatsHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
    }
    mCommandList->ResolveQueryData(mR4bStatsHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS,
                                   0, 1, mR4bStatsReadback.Get(), 0);
    mR4bStatsPendingRead = true;
}

void GfxRenderingAPIDX12::R4bArmPresentCheck() {
    // Post-raster backbuffer readback. Only enqueues the copy; the execute + GPU wait +
    // readback + verdict happen in R4bFrameFlushAndReport (called right after this).
    if (mPixelIsoMode < 20) return;
    if (mR4b.presentChecked || !mR4b.presentCheckArmed) return;
    if (!mDevice || !mCommandList || !mRenderTargets[mFrameIndex].Get()) return;

    ID3D12Resource* bb = mRenderTargets[mFrameIndex].Get();
    D3D12_RESOURCE_DESC d = bb->GetDesc();
    if (!mR4bPresentReadback) {
        UINT pitch = ((UINT)d.Width * 4 + 255) & ~255;
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rbd = {};
        rbd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rbd.Width = (UINT64)pitch * d.Height;
        rbd.Height = 1;
        rbd.DepthOrArraySize = 1;
        rbd.MipLevels = 1;
        rbd.SampleDesc.Count = 1;
        rbd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(mDevice->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rbd,
                                                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                    IID_PPV_ARGS(&mR4bPresentReadback)))) return;
    }
    UINT pitch = ((UINT)d.Width * 4 + 255) & ~255;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
    fp.Footprint.Width = (UINT)d.Width;
    fp.Footprint.Height = (UINT)d.Height;
    fp.Footprint.Depth = 1;
    fp.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    fp.Footprint.RowPitch = pitch;
    D3D12_RESOURCE_BARRIER b[2] = {};
    b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b[0].Transition.pResource = bb;
    b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b[1].Transition.pResource = bb;
    b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = bb;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = mR4bPresentReadback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = fp;
    mCommandList->ResourceBarrier(1, &b[0]);
    mCommandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    mCommandList->ResourceBarrier(1, &b[1]);
}

void GfxRenderingAPIDX12::R4bFrameFlushAndReport() {
    // Executes + closes the current frame's command list, waits for the GPU, reads back
    // the pending pipeline-statistics resolve and the post-raster backbuffer copy, and
    // emits the C-test verdict. EndFrame then reopens the command list for the rest of
    // the post-raster work (profiler scopes, temporal, present transition).
    if (mPixelIsoMode < 20 && !mR7c5A5Enabled) return;
    if (!mInitialized || !mCommandListOpen || !mCommandList) return;
    if (!mR4b.captured) return;
    if (!mR4b.presentCheckArmed && !mR4bStatsPendingRead) return;

    bool anyPresent = mR4b.presentCheckArmed && !mR4b.presentChecked;
    bool anyStats = mR4bStatsPendingRead;
    if (!anyPresent && !anyStats) return;

    mCommandList->Close();
    mCommandListOpen = false;
    ID3D12CommandList* lists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(1, lists);

    if (mFence && mFenceEvent) {
        ++mCurrentFenceValue;
        mCommandQueue->Signal(mFence.Get(), mCurrentFenceValue);
        mFrameContexts[mFrameIndex].fenceValue = mCurrentFenceValue;
        if (mFence->GetCompletedValue() < mCurrentFenceValue) {
            if (SUCCEEDED(mFence->SetEventOnCompletion(mCurrentFenceValue, mFenceEvent))) {
                WaitForSingleObject(mFenceEvent, 5000);
            }
        }
    }

    if (anyStats) {
        D3D12_RANGE mr = { 0, sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS) };
        void* mapped = nullptr;
        if (SUCCEEDED(mR4bStatsReadback->Map(0, &mr, &mapped)) && mapped) {
            auto* st = (D3D12_QUERY_DATA_PIPELINE_STATISTICS*)mapped;
            char buf[320];
            snprintf(buf, sizeof(buf),
                     "[COUCH-R7C5-A5][PIPELINE_STATS] IAVertices=%llu IAPrimitives=%llu VSInvocations=%llu CInvocations=%llu CPrimitives=%llu PSInvocations=%llu",
                     (unsigned long long)st->IAVertices,
                     (unsigned long long)st->IAPrimitives,
                     (unsigned long long)st->VSInvocations,
                     (unsigned long long)st->CInvocations,
                     (unsigned long long)st->CPrimitives,
                     (unsigned long long)st->PSInvocations);
            LogCouchD3D12(buf);
            R4bLogResult("PS_INVOCATIONS", st->PSInvocations > 0);
            D3D12_RANGE er = { 0, 0 };
            mR4bStatsReadback->Unmap(0, &er);
        } else {
            LogCouchD3D12("[COUCH-R4B][STATS] readback Map FAILED");
        }
        mR4bStatsPendingRead = false;
        mR4bStatsQueryStartFrame = 0;
    }

    if (anyPresent) {
        ID3D12Resource* bb = mRenderTargets[mFrameIndex].Get();
        if (bb && mR4bPresentReadback) {
            D3D12_RESOURCE_DESC d = bb->GetDesc();
            UINT pitch = ((UINT)d.Width * 4 + 255) & ~255;
            D3D12_RANGE mr = { 0, (SIZE_T)((UINT64)pitch * d.Height) };
            void* mapped = nullptr;
            UINT nz = 0, tot = 0, mx = 0;
            if (SUCCEEDED(mR4bPresentReadback->Map(0, &mr, &mapped)) && mapped) {
                for (UINT y = 0; y < (UINT)d.Height; y += 4) {
                    const uint8_t* row = (const uint8_t*)mapped + (size_t)y * pitch;
                    for (UINT x = 0; x < (UINT)d.Width * 4; x += 8) {
                        UINT r = row[x], g = row[x + 1], bl = row[x + 2];
                        UINT v = (r > g) ? ((r > bl) ? r : bl) : ((g > bl) ? g : bl);
                        tot++;
                        if (v) nz++;
                        if (v > mx) mx = v;
                    }
                }
                CreateDirectoryA("logs", nullptr);
                CreateDirectoryA("logs\\captures", nullptr);
                char path[160];
                snprintf(path, sizeof(path), "logs/captures/r4b_mode%d_frame%06d.png", mPixelIsoMode, forensic::gForensicFrame);
                std::vector<uint8_t> rgba((size_t)d.Width * d.Height * 4);
                for (UINT y = 0; y < (UINT)d.Height; ++y) {
                    const uint8_t* row = (const uint8_t*)mapped + (size_t)y * pitch;
                    memcpy(rgba.data() + (size_t)y * (size_t)d.Width * 4, row, (size_t)d.Width * 4);
                }
                bool wrote = forensic::ForensicWriteCapturePair(path, (int)d.Width, (int)d.Height, rgba);
                char sb[256];
                snprintf(sb, sizeof(sb), "[COUCH-R4B][PRESENT] mode=%d frame=%d nonzero=%.2f%% max=%u png=%d",
                         mPixelIsoMode, forensic::gForensicFrame,
                         tot ? 100.0 * nz / tot : 0.0, mx, (int)wrote);
                LogCouchD3D12(sb);
                D3D12_RANGE er = { 0, 0 };
                mR4bPresentReadback->Unmap(0, &er);
            } else {
                LogCouchD3D12("[COUCH-R4B][PRESENT] readback Map FAILED");
            }
            bool visible = (nz > 0) && (mx > 0);
            switch (mPixelIsoMode) {
                case 20: R4bLogResult("C0_SOLID_PS", visible); break;
                case 21: R4bLogResult("C1_DEPTH_OFF", visible); break;
                case 22: R4bLogResult("C2_CULL_OFF", visible); break;
                case 23: R4bLogResult("C3_FULL_VIEWPORT_SCISSOR", visible); break;
                case 24: R4bLogResult("C4_KNOWN_VS", visible); break;
                default: break;
            }
            mR4b.presentChecked = true;
        }
    }

    // Reopen the command list for the remainder of EndFrame (profiler end, temporal, present)
    mCommandList->Reset(mFrameContexts[mFrameIndex].commandAllocator.Get(), nullptr);
    mCommandListOpen = true;
}

void GfxRenderingAPIDX12::ClearFramebuffer(bool color, bool depth) {
    if (!mInitialized || !mCommandListOpen) return;
    if (color) {
        const float clear[4] = { 0, 0, 0, 1 };
        mCommandList->ClearRenderTargetView(FramebufferRtv(mCurrentFramebuffer), clear, 0, nullptr);
    }
    if (depth && FramebufferDepth(mCurrentFramebuffer))
        mCommandList->ClearDepthStencilView(FramebufferDsv(mCurrentFramebuffer),
                                           D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

void GfxRenderingAPIDX12::SetViewport(int x, int y, int width, int height) {
    static int sVpLog = 0;
    const int flippedY = FramebufferHeight(mCurrentFramebuffer) - y - height;
    if (sVpLog < 10) {
        LogCouchD3D12("[COUCH-R4][VP] raw=(" + std::to_string(x) + "," + std::to_string(y) + "," +
                      std::to_string(width) + "," + std::to_string(height) +
                      ") d3d12=(" + std::to_string(x) + "," + std::to_string(flippedY) + "," +
                      std::to_string(width) + "," + std::to_string(height) +
                      ") renderH=" + std::to_string(mRenderHeight));
        sVpLog++;
    }
    mViewport.TopLeftX = static_cast<float>(x);
    // Fast3D and D3D11 use a bottom-origin logical viewport. D3D12 must
    // receive the equivalent top-origin rectangle expected by rasterization.
    mViewport.TopLeftY = static_cast<float>(flippedY);
    mViewport.Width = static_cast<float>(width);
    mViewport.Height = static_cast<float>(height);
    mViewport.MinDepth = 0.0f;
    mViewport.MaxDepth = 1.0f;
    mViewportDirty = true;
}

void GfxRenderingAPIDX12::SetScissor(int x, int y, int width, int height) {
    const int flippedTop = FramebufferHeight(mCurrentFramebuffer) - y - height;
    mScissorRect.left = x;
    mScissorRect.top = flippedTop;
    mScissorRect.right = x + width;
    mScissorRect.bottom = FramebufferHeight(mCurrentFramebuffer) - y;
    mScissorDirty = true;
}

void GfxRenderingAPIDX12::SetDepthTestAndMask(bool depth_test, bool z_upd) {
    mCurrentDepthTest = depth_test;
    mCurrentDepthMask = z_upd;
}

void GfxRenderingAPIDX12::SetZmodeDecal(bool decal) {
    mCurrentZmodeDecal = decal;
}

void GfxRenderingAPIDX12::SetUseAlpha(bool useAlpha) {
    mCurrentUseAlpha = useAlpha;
}

uint32_t GfxRenderingAPIDX12::NewTexture() {
    TextureDataD3D12 tex = {};
    tex.width = 0;
    tex.height = 0;
    tex.linearFiltering = true;
    tex.uploaded = false;
    tex.srvIndex = AllocateSrvDescriptor();
    tex.samplerIndex = 0;

    if (mDevice && mSrvHeap) {
        D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv = {};
        nullSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        nullSrv.Texture2D.MipLevels = 1;

        D3D12_CPU_DESCRIPTOR_HANDLE handle = mSrvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += tex.srvIndex * mSrvDescriptorSize;
        mDevice->CreateShaderResourceView(nullptr, &nullSrv, handle);
        auto mirror = mSrvCpuHeap->GetCPUDescriptorHandleForHeapStart();
        mirror.ptr += size_t(tex.srvIndex) * mSrvDescriptorSize;
        mDevice->CreateShaderResourceView(nullptr, &nullSrv, mirror);
    }

    mTextures.push_back(tex);
    return static_cast<uint32_t>(mTextures.size() - 1);
}

// ============================================================
// R4-B — Canonical draw selection + diagnostic override
// ============================================================
void GfxRenderingAPIDX12::SelectTexture(int tile, uint32_t textureId) {
    if (tile >= 0 && tile < SHADER_MAX_TEXTURES) {
        mCurrentTile = tile;
        mCurrentTextureIds[tile] = textureId;
        if (textureId < mTextures.size()) {
            // Match the D3D11 contract: sampler state belongs to the cached
            // texture and must be restored whenever that texture is selected.
            mCurrentSamplerIndices[tile] = mTextures[textureId].samplerIndex;
        }
    }
}

uint32_t GfxRenderingAPIDX12::GetTextureSrvIndex(int tile) const {
    if (tile < 0 || tile >= SHADER_MAX_TEXTURES) return UINT32_MAX;
    const uint32_t textureId = mCurrentTextureIds[tile];
    if (textureId >= mTextures.size() || !mTextures[textureId].uploaded) return UINT32_MAX;
    return mTextures[textureId].srvIndex;
}

void GfxRenderingAPIDX12::UploadTexture(const uint8_t* rgba32Buf, uint32_t width, uint32_t height) {
    uint32_t texId = mCurrentTextureIds[mCurrentTile];
    if (!mInitialized || texId >= mTextures.size()) return;

    if (rgba32Buf == nullptr || width == 0 || height == 0 ||
        width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION) {
        LogCouchD3D12("[COUCH-D3D12][INVALID_TEXTURE_UPLOAD] texId=" + std::to_string(texId) +
                      " width=" + std::to_string(width) + " height=" + std::to_string(height) +
                      " data=" + std::string(rgba32Buf ? "present" : "null") +
                      " resourceIdentity=" + std::to_string(mCurrentTextureResourceIdentity));
        return;
    }

    auto& tex = mTextures[texId];
    const bool replacingTexture = tex.resource != nullptr;
    const uint32_t replacementSrvIndex = replacingTexture ? AllocateSrvDescriptor() : tex.srvIndex;
    if (replacementSrvIndex == kNullSrvIndex) return;

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    UINT16 mipCount = 1;
    for (uint32_t extent = std::max(width, height); extent > 1; extent >>= 1) ++mipCount;

    D3D12_RESOURCE_DESC resDesc = {};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc.Width = width;
    resDesc.Height = height;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = mipCount;
    resDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resDesc.SampleDesc.Count = 1;

    ComPtr<ID3D12Resource> newTexture;
    HRESULT textureHr = mDevice->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &resDesc,
                                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                         IID_PPV_ARGS(&newTexture));
    if (FAILED(textureHr)) {
        if (replacingTexture) mFreeSrvIndices.push_back(replacementSrvIndex);
        LogCouchD3D12("[COUCH-D3D12][ERROR] Failed to create texture resource, HRESULT=" +
                      std::to_string(static_cast<long long>(textureHr)) +
                      " texId=" + std::to_string(texId) +
                      " width=" + std::to_string(width) + " height=" + std::to_string(height) +
                      " replacing=" + std::to_string(replacingTexture ? 1 : 0) +
                      " resourceIdentity=" + std::to_string(mCurrentTextureResourceIdentity));
        return;
    }

    std::vector<std::vector<uint8_t>> mipPixels(mipCount - 1);
    const uint8_t* previousPixels = rgba32Buf;
    uint32_t previousWidth = width;
    uint32_t previousHeight = height;
    for (UINT16 mip = 1; mip < mipCount; ++mip) {
        const uint32_t mipWidth = std::max(1u, width >> mip);
        const uint32_t mipHeight = std::max(1u, height >> mip);
        auto& pixels = mipPixels[mip - 1];
        pixels.resize(static_cast<size_t>(mipWidth) * mipHeight * 4);
        for (uint32_t y = 0; y < mipHeight; ++y) {
            for (uint32_t x = 0; x < mipWidth; ++x) {
                const uint32_t sx0 = std::min(x * 2, previousWidth - 1);
                const uint32_t sy0 = std::min(y * 2, previousHeight - 1);
                const uint32_t sx1 = std::min(sx0 + 1, previousWidth - 1);
                const uint32_t sy1 = std::min(sy0 + 1, previousHeight - 1);
                const size_t sourceOffsets[4] = {
                    (static_cast<size_t>(sy0) * previousWidth + sx0) * 4,
                    (static_cast<size_t>(sy0) * previousWidth + sx1) * 4,
                    (static_cast<size_t>(sy1) * previousWidth + sx0) * 4,
                    (static_cast<size_t>(sy1) * previousWidth + sx1) * 4,
                };
                const size_t destination = (static_cast<size_t>(y) * mipWidth + x) * 4;
                for (uint32_t channel = 0; channel < 4; ++channel) {
                    float average = 0.0f;
                    for (size_t source : sourceOffsets) {
                        float value = previousPixels[source + channel] / 255.0f;
                        if (channel < 3) {
                            value = value <= 0.04045f ? value / 12.92f
                                : std::pow((value + 0.055f) / 1.055f, 2.4f);
                        }
                        average += value * 0.25f;
                    }
                    if (channel < 3) {
                        average = average <= 0.0031308f ? average * 12.92f
                            : 1.055f * std::pow(average, 1.0f / 2.4f) - 0.055f;
                    }
                    pixels[destination + channel] = static_cast<uint8_t>(
                        std::clamp(std::lround(average * 255.0f), 0l, 255l));
                }
            }
        }
        previousPixels = pixels.data();
        previousWidth = mipWidth;
        previousHeight = mipHeight;
    }

    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(mipCount);
    std::vector<UINT> rowCounts(mipCount);
    std::vector<UINT64> rowSizes(mipCount);
    UINT64 uploadSize = 0;
    mDevice->GetCopyableFootprints(&resDesc, 0, mipCount, 0, layouts.data(), rowCounts.data(),
                                   rowSizes.data(), &uploadSize);

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC upBufferDesc = {};
    upBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    upBufferDesc.Width = uploadSize;
    upBufferDesc.Height = 1;
    upBufferDesc.DepthOrArraySize = 1;
    upBufferDesc.MipLevels = 1;
    upBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    upBufferDesc.SampleDesc.Count = 1;
    upBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> staging;
    HRESULT hr = mDevice->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &upBufferDesc,
                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                    IID_PPV_ARGS(&staging));
    if (FAILED(hr)) {
        LogCouchD3D12("[COUCH-D3D12][ERROR] Failed to create staging buffer for texture upload, HRESULT=0x" +
                      std::to_string(hr));
        return;
    }

    uint8_t* mapped = nullptr;
    if (FAILED(staging->Map(0, nullptr, reinterpret_cast<void**>(&mapped))) || !mapped) return;
    for (UINT16 mip = 0; mip < mipCount; ++mip) {
        const uint32_t mipWidth = std::max(1u, width >> mip);
        const uint32_t mipHeight = std::max(1u, height >> mip);
        const uint8_t* source = mip == 0 ? rgba32Buf : mipPixels[mip - 1].data();
        const UINT rowPitch = layouts[mip].Footprint.RowPitch;
        uint8_t* destination = mapped + layouts[mip].Offset;
        for (uint32_t y = 0; y < mipHeight; ++y) {
            memcpy(destination + static_cast<size_t>(y) * rowPitch,
                   source + static_cast<size_t>(y) * mipWidth * 4, static_cast<size_t>(mipWidth) * 4);
        }
    }
    staging->Unmap(0, nullptr);
    for (UINT16 mip = 0; mip < mipCount; ++mip) {
        D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
        srcLoc.pResource = staging.Get();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint = layouts[mip];
        D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
        dstLoc.pResource = newTexture.Get();
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = mip;
        mCommandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
    }
    TransitionResource(newTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    mFrameContexts[mFrameIndex].stagingBuffers.push_back(staging);

    if (replacingTexture) {
        // Preserve both the old resource and its descriptor until this frame's
        // fence completes. Rewriting tex.srvIndex here would retroactively
        // change every earlier draw that references that GPU descriptor.
        mFrameContexts[mFrameIndex].stagingBuffers.push_back(tex.resource);
        mFrameContexts[mFrameIndex].retiredSrvIndices.push_back(tex.srvIndex);
    }
    tex.resource = newTexture;
    tex.srvIndex = replacementSrvIndex;
    tex.width = width;
    tex.height = height;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = mipCount;

    D3D12_CPU_DESCRIPTOR_HANDLE handle = mSrvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += tex.srvIndex * mSrvDescriptorSize;
    mDevice->CreateShaderResourceView(tex.resource.Get(), &srvDesc, handle);
    auto mirrorHandle = mSrvCpuHeap->GetCPUDescriptorHandleForHeapStart();
    mirrorHandle.ptr += size_t(tex.srvIndex) * mSrvDescriptorSize;
    mDevice->CreateShaderResourceView(tex.resource.Get(), &srvDesc, mirrorHandle);
    tex.uploaded = true;
    MaterialRegistry::Instance().SetTextureSrvIndex(mCurrentTextureResourceIdentity, tex.srvIndex);
}

void GfxRenderingAPIDX12::SetSamplerParameters(int sampler, bool linear_filter, uint32_t cms, uint32_t cmt) {
    if (!mDevice || !mSamplerHeap) return;

    const uint32_t unit = static_cast<uint32_t>(sampler);
    if (unit >= SHADER_MAX_TEXTURES) return;

    const bool useLinear = linear_filter && mCurrentFilterMode == FILTER_LINEAR;
    const SamplerKeyD3D12 key{ useLinear, cms, cmt };
    auto found = mSamplerCache.find(key);
    if (found != mSamplerCache.end()) {
        mCurrentSamplerIndices[unit] = found->second;
        const uint32_t textureId = mCurrentTextureIds[unit];
        if (textureId < mTextures.size()) mTextures[textureId].samplerIndex = found->second;
        return;
    }
    if (mNextSamplerIndex >= 64) return;

    D3D12_SAMPLER_DESC desc = {};
    desc.Filter = useLinear ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
    const auto toAddressMode = [](uint32_t value) {
        if (value & G_TX_CLAMP) return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        return (value & G_TX_MIRROR) ? D3D12_TEXTURE_ADDRESS_MODE_MIRROR : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    };
    desc.AddressU = toAddressMode(cms);
    desc.AddressV = toAddressMode(cmt);
    desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    desc.MipLODBias = 0.0f;
    desc.MaxAnisotropy = 1;
    desc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    desc.MinLOD = 0.0f;
    // Keep Fast3D's artist-authored point/linear sampling unchanged. The DXR
    // material sampler independently chooses these mips from its ray footprint.
    desc.MaxLOD = 0.0f;

    const uint32_t index = mNextSamplerIndex++;
    D3D12_CPU_DESCRIPTOR_HANDLE handle = mSamplerHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * mSamplerDescriptorSize;
    mDevice->CreateSampler(&desc, handle);
    mSamplerCache.emplace(key, index);
    mCurrentSamplerIndices[unit] = index;
    const uint32_t textureId = mCurrentTextureIds[unit];
    if (textureId < mTextures.size()) mTextures[textureId].samplerIndex = index;
}

void GfxRenderingAPIDX12::DeleteTexture(uint32_t texId) {
    if (texId < mTextures.size()) {
        mTextures[texId].resource.Reset();
        mTextures[texId].uploaded = false;
    }
}

void GfxRenderingAPIDX12::SetTextureFilter(FilteringMode mode) {
    mCurrentFilterMode = mode;
}

FilteringMode GfxRenderingAPIDX12::GetTextureFilter() {
    return mCurrentFilterMode;
}

void GfxRenderingAPIDX12::SetSrgbMode() {
    mSrgbMode = true;
}

ImTextureID GfxRenderingAPIDX12::GetTextureById(int id) {
    if (id < 0 || id >= static_cast<int>(mTextures.size()) || !mTextures[id].uploaded) return nullptr;
    auto handle = mSrvHeap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += uint64_t(mTextures[id].srvIndex) * mSrvDescriptorSize;
    return reinterpret_cast<ImTextureID>(handle.ptr);
}

void GfxRenderingAPIDX12::UnloadShader(struct ShaderProgram* oldPrg) {
    mCurrentShaderProgram = nullptr;
}

void GfxRenderingAPIDX12::LoadShader(struct ShaderProgram* newPrg) {
    mCurrentShaderProgram = reinterpret_cast<ShaderProgramD3D12*>(newPrg);
}

struct ShaderProgram* GfxRenderingAPIDX12::LookupShader(uint64_t shaderId0, uint32_t shaderId1) {
    auto key = std::make_pair(shaderId0, shaderId1);
    auto it = mShaderProgramPool.find(key);
    return (it != mShaderProgramPool.end()) ? reinterpret_cast<struct ShaderProgram*>(&it->second) : nullptr;
}

void GfxRenderingAPIDX12::ShaderGetInfo(struct ShaderProgram* prg, uint8_t* numInputs, bool usedTextures[2]) {
    auto prog = reinterpret_cast<ShaderProgramD3D12*>(prg);
    if (prog) {
        *numInputs = prog->numInputs;
        usedTextures[0] = prog->usedTextures[0];
        usedTextures[1] = prog->usedTextures[1];
    }
}

void GfxRenderingAPIDX12::DumpDebugMessages() {
    ComPtr<ID3D12InfoQueue> infoQueue;
    if (mDevice && SUCCEEDED(mDevice->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
        UINT64 numMessages = infoQueue->GetNumStoredMessages();
        for (UINT64 i = 0; i < numMessages; ++i) {
            SIZE_T messageLength = 0;
            infoQueue->GetMessage(i, nullptr, &messageLength);
            if (messageLength > 0) {
                std::vector<char> messageData(messageLength);
                auto pMessage = reinterpret_cast<D3D12_MESSAGE*>(messageData.data());
                if (SUCCEEDED(infoQueue->GetMessage(i, pMessage, &messageLength))) {
                    LogCouchD3D12(std::string("[COUCH-D3D12][DEBUG-LAYER] ") + pMessage->pDescription);
                }
            }
        }
        infoQueue->ClearStoredMessages();
    }
}

struct ShaderProgram* GfxRenderingAPIDX12::CreateAndLoadNewShader(uint64_t shaderId0, uint32_t shaderId1) {
    auto key = std::make_pair(shaderId0, shaderId1);
    auto& prog = mShaderProgramPool[key];
    prog.shaderId0 = shaderId0;
    prog.shaderId1 = shaderId1;

    CCFeatures cc_features;
    gfx_cc_get_features(shaderId0, shaderId1, &cc_features);
    prog.numInputs = cc_features.numInputs;
    prog.usedTextures[0] = cc_features.usedTextures[0];
    prog.usedTextures[1] = cc_features.usedTextures[1];
    // FORENSIC: track mask permutations (they sample t2/t3 but the main root signature
    // only exposes t0/t1 -> debug-layer warning "SRV range BaseShaderRegister=2 not bound")
    {
        static int sMaskShaderCount = 0, sPlainShaderCount = 0;
        const bool hasMask = cc_features.used_masks[0] || cc_features.used_masks[1];
        if (hasMask) {
            ++sMaskShaderCount;
            if (sMaskShaderCount <= 20) {
                LogCouchD3D12("[COUCH-FORENSIC][SHADER] MASK_PERM id0=" + std::to_string(shaderId0) +
                              " id1=" + std::to_string(shaderId1) +
                              " masks0=" + std::to_string(cc_features.used_masks[0]) +
                              " masks1=" + std::to_string(cc_features.used_masks[1]) +
                              " total=" + std::to_string(sMaskShaderCount));
            }
            // Phase 4: Root signature t2 audit — log each mask shader that may trigger t2 warning
            LogCouchD3D12(std::string("[COUCH-T2-AUDIT] SHADER_USING_MASK t2_warn_candidate") +
                          " shaderId0=" + std::to_string(shaderId0) +
                          " shaderId1=" + std::to_string(shaderId1) +
                          " masks0=" + std::to_string(cc_features.used_masks[0]) +
                          " masks1=" + std::to_string(cc_features.used_masks[1]) +
                          " path=Fast3D NOT_DXR_SHADOW");
        } else {
            ++sPlainShaderCount;
        }
    }

    // Construir inputElements de forma general y exacta conforme a Fast3D
    prog.inputElements.clear();
    prog.inputElements.push_back({
        "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
    });
    for (UINT i = 0; i < 2; i++) {
        if (cc_features.usedTextures[i]) {
            prog.inputElements.push_back({
                "TEXCOORD", i, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
            });
            if (cc_features.clamp[i][0]) {
                prog.inputElements.push_back({
                    "TEXCLAMPS", i, DXGI_FORMAT_R32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
                });
            }
            if (cc_features.clamp[i][1]) {
                prog.inputElements.push_back({
                    "TEXCLAMPT", i, DXGI_FORMAT_R32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
                });
            }
        }
    }
    if (cc_features.opt_fog) {
        prog.inputElements.push_back({
            "FOG", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
        });
    }
    if (cc_features.opt_grayscale) {
        prog.inputElements.push_back({
            "GRAYSCALE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
        });
    }
    for (unsigned int i = 0; i < cc_features.numInputs; i++) {
        DXGI_FORMAT format = cc_features.opt_alpha ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R32G32B32_FLOAT;
        prog.inputElements.push_back({
            "INPUT", i, format, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
        });
    }

    size_t numFloats = 4;
    std::string hlsl = gfx_direct3d_common_build_shader(numFloats, cc_features, false,
                                                       mCurrentFilterMode == FILTER_THREE_POINT, mSrgbMode);
    prog.numFloats = static_cast<uint8_t>(numFloats);
    const auto makeLinearHdrVariant = [](std::string source, bool decodeLegacyOutput) {
        const std::string signature = "float4 fromLinear(float4 linearRGB)";
        const size_t functionStart = source.find(signature);
        if (functionStart != std::string::npos) {
            const size_t openBrace = source.find('{', functionStart + signature.size());
            if (openBrace == std::string::npos) return source;
            size_t cursor = openBrace;
            unsigned depth = 0;
            do {
                if (source[cursor] == '{') ++depth;
                else if (source[cursor] == '}') --depth;
                ++cursor;
            } while (cursor < source.size() && depth != 0);
            if (depth != 0) return source;
            source.replace(openBrace, cursor - openBrace, "{ return linearRGB; }");
        } else if (decodeLegacyOutput) {
            // Non-sRGB Fast3D variants carry display-encoded texture colors.
            // Decode their final RGB before writing to the linear FP16 scene.
            size_t at = 0;
            const std::string alphaReturn = "return texel;";
            const std::string opaqueReturn = "return float4(texel, 1.0);";
            const std::string linearAlphaReturn =
                "return float4(pow(max(texel.rgb, 0.0.xxx), 2.2), texel.a);";
            const std::string linearOpaqueReturn =
                "return float4(pow(max(texel, 0.0.xxx), 2.2), 1.0);";
            while ((at = source.find(alphaReturn, at)) != std::string::npos) {
                source.replace(at, alphaReturn.size(), linearAlphaReturn);
                at += linearAlphaReturn.size();
            }
            at = 0;
            while ((at = source.find(opaqueReturn, at)) != std::string::npos) {
                source.replace(at, opaqueReturn.size(), linearOpaqueReturn);
                at += linearOpaqueReturn.size();
            }
        }
        return source;
    };
    const std::string hdrHlsl = makeLinearHdrVariant(hlsl, true);

    if (mD3dCompile) {
        ComPtr<ID3DBlob> errorBlob;
        HRESULT hrVS = mD3dCompile(hlsl.c_str(), hlsl.length(), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &prog.vsBlob, &errorBlob);
        if (FAILED(hrVS)) {
            LogCouchD3D12("[COUCH-D3D12][ERROR] VS compilation failed for shaderId0=" + std::to_string(shaderId0));
            if (errorBlob) {
                LogCouchD3D12("[COUCH-D3D12] VS Error: " + std::string((char*)errorBlob->GetBufferPointer()));
            }
        }
        ComPtr<ID3DBlob> errorBlob2;
        HRESULT hrPS = mD3dCompile(hlsl.c_str(), hlsl.length(), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &prog.psBlob, &errorBlob2);
        if (FAILED(hrPS)) {
            LogCouchD3D12("[COUCH-D3D12][ERROR] PS compilation failed for shaderId0=" + std::to_string(shaderId0));
            if (errorBlob2) {
                LogCouchD3D12("[COUCH-D3D12] PS Error: " + std::string((char*)errorBlob2->GetBufferPointer()));
            }
        }
        ComPtr<ID3DBlob> hdrPsError;
        const HRESULT hdrPsHr = mD3dCompile(hdrHlsl.c_str(), hdrHlsl.length(), nullptr, nullptr, nullptr,
                                            "PSMain", "ps_5_0", 0, 0, &prog.hdrPsBlob, &hdrPsError);
        if (FAILED(hdrPsHr)) {
            LogCouchD3D12("[COUCH-D3D12][ERROR] HDR PS compilation failed for shaderId0=" +
                          std::to_string(shaderId0) +
                          (hdrPsError ? (" " + std::string(static_cast<const char*>(hdrPsError->GetBufferPointer())))
                                      : std::string()));
        }

        prog.physicalInputElements = prog.inputElements;
        prog.physicalInputElements.push_back({"PHYS_POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 1, 0,
                                               D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
        prog.physicalInputElements.push_back({"PHYS_FLAGS", 0, DXGI_FORMAT_R32_UINT, 1, 12,
                                               D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
        prog.physicalInputElements.push_back({"PHYS_NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 1, 16,
                                               D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
        prog.physicalInputElements.push_back({"PHYS_FLAGS", 1, DXGI_FORMAT_R32_UINT, 1, 28,
                                               D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});

        std::string physicalHlsl = "#define PHYSICAL_NORMAL_VALID 2\n" + hlsl;
        // Compile the material variant independently of the selected profile:
        // shader programs are cached by combiner ID, not by visual profile.
        // DrawTrianglesPhysical selects this variant only for enhanced draws.
        const char* materialResponseEnv = std::getenv("COUCH_MATERIAL_RESPONSE");
        const bool enhancedPhysicalResponse =
            (!materialResponseEnv || strcmp(materialResponseEnv, "0") != 0);
        LogCouchD3D12("[COUCH-R7C5][MATERIAL_RESPONSE] variantRequested=" +
                      std::string(enhancedPhysicalResponse ? "YES" : "NO") +
                      " profile=" + Fast::MaterialRegistry::Instance().GetProfileName() +
                      " env=" + std::string(materialResponseEnv ? materialResponseEnv : "(unset)"));
        if (enhancedPhysicalResponse) {
            // Fast3D's ordinary UNORM combiner output is display encoded.
            // Decode before lighting, keep HDR radiance linear, and encode
            // only the SDR variant. The sRGB combiner already uses linear RGB
            // and has its own fromLinear() output conversion.
            const std::string physicalTransferHlsl =
                "float3 CouchSrgbToLinear(float3 color) {\n"
                "    color = max(color, 0.0.xxx);\n"
                "    return lerp(pow((color + 0.055) / 1.055, 2.4), color / 12.92,\n"
                "                1.0 - step(0.04045, color));\n"
                "}\n"
                "float3 CouchLinearToSrgb(float3 color) {\n"
                "    color = max(color, 0.0.xxx);\n"
                "    return lerp(1.055 * pow(color, 1.0 / 2.4) - 0.055, color * 12.92,\n"
                "                1.0 - step(0.0031308, color));\n"
                "}\n"
                "float3 CouchPhysicalInput(float3 color) {\n" +
                (mSrgbMode ? std::string("    return color;\n")
                           : std::string("    return CouchSrgbToLinear(color);\n")) +
                "}\n"
                "float3 CouchPhysicalOutput(float3 color) {\n"
                "#if defined(COUCH_HDR_OUTPUT)\n"
                "    return color;\n"
                "#else\n" +
                (mSrgbMode ? std::string("    return color;\n")
                           : std::string("    return CouchLinearToSrgb(color);\n")) +
                "#endif\n"
                "}\n";
            physicalHlsl = physicalTransferHlsl +
                "cbuffer CouchPhysicalCB : register(b2) {\n"
                "    uint gCouchMaterialIndex;\n"
                "    uint gCouchDrawFlags;\n"
                "    uint2 gCouchPadding;\n"
                "    float4 gCouchViewLightDirection;\n"
                "    float4 gCouchSceneLightColor;\n"
                "    float4 gCouchAmbientLightColor;\n"
                "    float4 gCouchViewPointLightPosition;\n"
                "    float4 gCouchPointLightOptics;\n"
                "    float4 gCouchViewPointLightPosition2;\n"
                "    float4 gCouchPointLightOptics2;\n"
                "    float4 gCouchMaterialSurface;\n"
                "    float4 gCouchMaterialOptics;\n"
                "};\n"
                "float3 CouchFresnelSchlick(float cosTheta, float3 f0) {\n"
                "    float factor = pow(1.0 - saturate(cosTheta), 5.0);\n"
                "    return f0 + (1.0 - f0) * factor;\n"
                "}\n"
                "float CouchNdfGGX(float nDotH, float roughness) {\n"
                "    float alpha = roughness * roughness;\n"
                "    float alpha2 = alpha * alpha;\n"
                "    float denominator = nDotH * nDotH * (alpha2 - 1.0) + 1.0;\n"
                "    return alpha2 / max(3.14159265 * denominator * denominator, 1.0e-5);\n"
                "}\n"
                "float CouchGeometrySchlickGGX(float nDotX, float roughness) {\n"
                "    float r = roughness + 1.0;\n"
                "    float k = r * r * 0.125;\n"
                "    return nDotX / max(nDotX * (1.0 - k) + k, 1.0e-5);\n"
                "}\n"
                "float3 CouchEvaluateSpecular(float3 n, float3 viewDir, float3 lightDir, float3 lightColor,\n"
                "                             float attenuation, float roughness, float3 f0) {\n"
                "    float nDotL = saturate(dot(n, lightDir));\n"
                "    float nDotV = saturate(dot(n, viewDir));\n"
                "    if (nDotL <= 0.0 || nDotV <= 0.0 || attenuation <= 0.0) return 0.0;\n"
                "    float3 halfVector = normalize(viewDir + lightDir);\n"
                "    float nDotH = saturate(dot(n, halfVector));\n"
                "    float vDotH = saturate(dot(viewDir, halfVector));\n"
                "    float distribution = CouchNdfGGX(nDotH, roughness);\n"
                "    float geometry = CouchGeometrySchlickGGX(nDotV, roughness) *\n"
                "                     CouchGeometrySchlickGGX(nDotL, roughness);\n"
                "    float3 fresnel = CouchFresnelSchlick(vDotH, f0);\n"
                "    float3 brdf = distribution * geometry * fresnel / max(4.0 * nDotV * nDotL, 1.0e-4);\n"
                "    return brdf * nDotL * lightColor * attenuation;\n"
                "}\n"
                "float3 CouchApplyPhysicalResponse(float3 baseColor, float3 viewNormal, float3 viewPosition, uint materialIndex, uint drawFlags) {\n"
                // MaterialType values are the shader ABI: 1..18 cover the
                // authored physical families, 19 is protected UI, and 20 is
                // unknown. Preserve their authored color with the required
                // output transfer while known 3D families receive lighting.
                "    baseColor = CouchPhysicalInput(baseColor);\n"
                "    if ((drawFlags & 16u) != 0u || materialIndex == 0u || materialIndex >= 19u) return CouchPhysicalOutput(baseColor);\n"
                "    float normalLengthSquared = dot(viewNormal, viewNormal);\n"
                "    if (any(isnan(viewNormal)) || any(isinf(viewNormal)) ||\n"
                "        any(isnan(viewPosition)) || any(isinf(viewPosition)) ||\n"
                "        normalLengthSquared <= 1.0e-8 || dot(viewPosition, viewPosition) <= 1.0e-8)\n"
                "        return CouchPhysicalOutput(baseColor);\n"
                "    float3 n = viewNormal * rsqrt(normalLengthSquared);\n"
                // Only authored relief opt-ins derive a micro-normal from albedo.
                // Screen-space derivatives form a view-space surface gradient;
                // this avoids fabricating bumps on skin, cloth, glass, and UI.
                "    if ((drawFlags & 32u) != 0u && gCouchMaterialOptics.y > 0.0) {\n"
                "        float height = dot(max(baseColor, 0.0.xxx), float3(0.2126, 0.7152, 0.0722));\n"
                "        float3 dpdx = ddx(viewPosition); float3 dpdy = ddy(viewPosition);\n"
                "        float3 r1 = cross(dpdy, n); float3 r2 = cross(n, dpdx);\n"
                "        float det = dot(dpdx, r1);\n"
                "        if (abs(det) > 1.0e-7) {\n"
                "            float3 gradient = sign(det) * (ddx(height) * r1 + ddy(height) * r2);\n"
                "            n = normalize(abs(det) * n - gradient * gCouchMaterialOptics.y);\n"
                "        }\n"
                "    }\n"
                "    float roughness = clamp(gCouchMaterialSurface.x, 0.04, 1.0);\n"
                "    float metallic = saturate(gCouchMaterialSurface.y);\n"
                "    float specular = saturate(max(gCouchMaterialSurface.z, gCouchMaterialOptics.x * 0.5));\n"
                "    float emissive = max(gCouchMaterialSurface.w, 0.0);\n"
                "    roughness = lerp(roughness, min(roughness, 0.18), saturate(gCouchMaterialOptics.z));\n"
                "    float3 lightDir = normalize(gCouchViewLightDirection.xyz);\n"
                "    float directionalNdl = saturate(dot(n, lightDir));\n"
                "    float pointNdl = 0.0;\n"
                "    float pointAttenuation = 0.0;\n"
                "    float3 pointDir = lightDir;\n"
                "    float pointAttenuation2 = 0.0;\n"
                "    float pointNdl2 = 0.0;\n"
                "    float3 pointDir2 = lightDir;\n"
                "    if (any(gCouchPointLightOptics.yzw > 0.0)) {\n"
                "        float3 toPointLight = gCouchViewPointLightPosition.xyz - viewPosition;\n"
                "        float lightDistance = length(toPointLight);\n"
                "        pointDir = toPointLight / max(lightDistance, 1.0e-4);\n"
                "        float distanceAttenuation = 1.0 + gCouchViewPointLightPosition.w * lightDistance +\n"
                "                                    gCouchPointLightOptics.x * lightDistance * lightDistance;\n"
                "        pointAttenuation = rcp(max(distanceAttenuation, 1.0));\n"
                "        pointNdl = saturate(dot(n, pointDir)) * pointAttenuation;\n"
                "    }\n"
                "    if (any(gCouchPointLightOptics2.yzw > 0.0)) {\n"
                "        float3 toPointLight2 = gCouchViewPointLightPosition2.xyz - viewPosition;\n"
                "        float lightDistance2 = length(toPointLight2);\n"
                "        pointDir2 = toPointLight2 / max(lightDistance2, 1.0e-4);\n"
                "        float distanceAttenuation2 = 1.0 + gCouchViewPointLightPosition2.w * lightDistance2 +\n"
                "                                     gCouchPointLightOptics2.x * lightDistance2 * lightDistance2;\n"
                "        pointAttenuation2 = rcp(max(distanceAttenuation2, 1.0));\n"
                "        pointNdl2 = saturate(dot(n, pointDir2)) * pointAttenuation2;\n"
                "    }\n"
                "    float3 viewDir = normalize(-viewPosition);\n"
                "    float3 f0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic) *\n"
                "                lerp(0.25, 1.0, saturate(max(specular, gCouchMaterialOptics.x)));\n"
                "    float response = 0.94 + 0.12 * saturate(directionalNdl + pointNdl + pointNdl2);\n"
                "    float3 result = baseColor * response;\n"
                "    result += CouchEvaluateSpecular(n, viewDir, lightDir, gCouchSceneLightColor.rgb,\n"
                "                                   1.0, roughness, f0);\n"
                "    result += CouchEvaluateSpecular(n, viewDir, pointDir, gCouchPointLightOptics.yzw,\n"
                "                                   pointAttenuation, roughness, f0);\n"
                "    result += CouchEvaluateSpecular(n, viewDir, pointDir2, gCouchPointLightOptics2.yzw,\n"
                "                                   pointAttenuation2, roughness, f0);\n"
                "    result += baseColor * gCouchAmbientLightColor.rgb * 0.025;\n"
                "    float subsurface = saturate(gCouchMaterialOptics.w);\n"
                "    float backScatter = saturate(dot(-n, lightDir)) +\n"
                "                        saturate(dot(-n, pointDir)) * pointAttenuation +\n"
                "                        saturate(dot(-n, pointDir2)) * pointAttenuation2;\n"
                "    result += baseColor * (gCouchSceneLightColor.rgb + gCouchPointLightOptics.yzw +\n"
                "                           gCouchPointLightOptics2.yzw) * subsurface * backScatter * 0.025;\n"
                "    result += baseColor * emissive;\n"
                // Preserve radiance above 1.0 for the FP16 enhanced scene
                // target; the fullscreen ACES pass maps it to the SDR swapchain.
                "    if (any(isnan(result)) || any(isinf(result))) return CouchPhysicalOutput(baseColor);\n"
                "    return CouchPhysicalOutput(max(result, 0.0.xxx));\n"
                "}\n" + physicalHlsl;
        }
        const std::string physicalFields =
            "    float3 viewPosition : PHYS_VIEW_POSITION;\n"
            "    float3 viewNormal : PHYS_VIEW_NORMAL;\n"
            "    nointerpolation uint positionFlags : PHYS_POSITION_FLAGS;\n"
            "    nointerpolation uint normalFlags : PHYS_NORMAL_FLAGS;\n";
        bool physicalSidebandFieldsInserted = false;
        const size_t psStructStart = physicalHlsl.find("struct PSInput");
        const size_t psClose = psStructStart == std::string::npos ? std::string::npos
                                                                    : physicalHlsl.find("};", psStructStart);
        if (psClose != std::string::npos) {
            physicalHlsl.insert(psClose, physicalFields);
            physicalSidebandFieldsInserted = true;
        }
        const std::string physParams =
            ", float3 physPosition : PHYS_POSITION0, uint physPositionFlags : PHYS_FLAGS0, "
            "float3 physNormal : PHYS_NORMAL0, uint physNormalFlags : PHYS_FLAGS1";
        bool physicalSidebandParamsInserted = false;
        const size_t vsStart = physicalHlsl.find("PSInput VSMain");
        const size_t vsClose = vsStart == std::string::npos ? std::string::npos
                                                             : physicalHlsl.find(")", vsStart);
        if (vsClose != std::string::npos) {
            physicalHlsl.insert(vsClose, physParams);
            physicalSidebandParamsInserted = true;
        }
        const std::string physAssign =
            "    result.viewPosition = physPosition;\n"
            "    result.viewNormal = physNormal;\n"
            "    result.positionFlags = physPositionFlags;\n"
            "    result.normalFlags = physNormalFlags;\n";
        bool physicalSidebandAssignmentsInserted = false;
        const size_t assignAt = physicalHlsl.find("result.position = position;");
        if (assignAt != std::string::npos) {
            const size_t assignEnd = physicalHlsl.find(';', assignAt);
            if (assignEnd != std::string::npos) {
                physicalHlsl.insert(assignEnd + 1, "\n" + physAssign);
                physicalSidebandAssignmentsInserted = true;
            }
        }
        const char* normalDebug = std::getenv("COUCH_NORMAL_DEBUG");
        const char* positionDebug = std::getenv("COUCH_POSITION_DEBUG");
        std::string debugCode;
        if (normalDebug && std::string(normalDebug) == "1") {
            debugCode = "    if ((input.normalFlags & PHYSICAL_NORMAL_VALID) != 0) { float3 n = normalize(input.viewNormal); return float4(n * 0.5 + 0.5, 1.0); } return float4(1, 0, 1, 1);\n";
        } else if (positionDebug && std::string(positionDebug) == "1") {
            debugCode = "    return float4(frac(abs(input.viewPosition) * 0.05), 1.0);\n";
        }
        if (!debugCode.empty()) {
            size_t psBody = physicalHlsl.find("float4 PSMain");
            psBody = physicalHlsl.find("{", psBody);
            if (psBody != std::string::npos) physicalHlsl.insert(psBody + 1, "\n" + debugCode);
        }
        bool physicalResponseBeforeFog = false;
        if (enhancedPhysicalResponse && physicalSidebandFieldsInserted && physicalSidebandAssignmentsInserted) {
            const std::string applyResponse =
                "    texel.rgb = CouchApplyPhysicalResponse(texel.rgb, input.viewNormal, input.viewPosition, "
                "gCouchMaterialIndex, gCouchDrawFlags);\n";
            // Light the surface before atmospheric blending. Applying the
            // material response at PSMain's return also lights the fog color.
            const std::string fogBlend = cc_features.opt_alpha
                ? "texel = float4(lerp(texel.rgb, input.fog.rgb, input.fog.a), texel.a);"
                : "texel = lerp(texel, input.fog.rgb, input.fog.a);";
            const size_t fogAt = cc_features.opt_fog ? physicalHlsl.find(fogBlend) : std::string::npos;
            if (fogAt != std::string::npos) {
                physicalHlsl.insert(fogAt, applyResponse);
                physicalResponseBeforeFog = true;
            }
            const std::string returnLinearAlpha = "return fromLinear(texel);";
            const std::string returnLinearRgb = "return fromLinear(float4(texel, 1.0));";
            const std::string returnAlpha = "return texel;";
            const std::string returnRgb = "return float4(texel, 1.0);";
            size_t at = 0;
            while (!physicalResponseBeforeFog && (at = physicalHlsl.find(returnLinearAlpha, at)) != std::string::npos) {
                physicalHlsl.insert(at, applyResponse);
                at += applyResponse.size() + returnLinearAlpha.size();
            }
            at = 0;
            while (!physicalResponseBeforeFog && (at = physicalHlsl.find(returnLinearRgb, at)) != std::string::npos) {
                physicalHlsl.insert(at, applyResponse);
                at += applyResponse.size() + returnLinearRgb.size();
            }
            at = 0;
            while (!physicalResponseBeforeFog && (at = physicalHlsl.find(returnAlpha, at)) != std::string::npos) {
                physicalHlsl.insert(at, applyResponse);
                at += applyResponse.size() + returnAlpha.size();
            }
            at = 0;
            while (!physicalResponseBeforeFog && (at = physicalHlsl.find(returnRgb, at)) != std::string::npos) {
                physicalHlsl.insert(at, applyResponse);
                at += applyResponse.size() + returnRgb.size();
            }
        } else if (enhancedPhysicalResponse && (!physicalSidebandFieldsInserted || !physicalSidebandAssignmentsInserted)) {
            LogCouchD3D12("[COUCH-R7C5][PHYSICAL_SHADER] Sideband transport not found; material response skipped");
        }
        {
            static int transportLogCount = 0;
            if (transportLogCount < 32) {
                ++transportLogCount;
                LogCouchD3D12("[COUCH-R7C5][PHYSICAL_TRANSPORT] fields=" +
                              std::string(physicalSidebandFieldsInserted ? "YES" : "NO") +
                              " assignments=" + std::string(physicalSidebandAssignmentsInserted ? "YES" : "NO") +
                              " params=" +
                              std::string(physicalHlsl.find("float3 physNormal : PHYS_NORMAL0") != std::string::npos ? "YES" : "NO") +
                              " normalUse=" +
                              std::string(physicalHlsl.find("result.viewNormal = physNormal;") != std::string::npos ? "YES" : "NO"));
            }
        }
        ComPtr<ID3DBlob> physicalVsError;
        ComPtr<ID3DBlob> physicalPsError;
        HRESULT physicalVsHr = mD3dCompile(physicalHlsl.c_str(), physicalHlsl.length(), nullptr, nullptr, nullptr,
                                           "VSMain", "vs_5_0", 0, 0, &prog.physicalVsBlob, &physicalVsError);
        HRESULT physicalPsHr = mD3dCompile(physicalHlsl.c_str(), physicalHlsl.length(), nullptr, nullptr, nullptr,
                                           "PSMain", "ps_5_0", 0, 0, &prog.physicalPsBlob, &physicalPsError);
        if (FAILED(physicalVsHr)) {
            LogCouchD3D12("[COUCH-R7C5][PHYSICAL_SHADER][VS_ERROR] HRESULT=" +
                          std::to_string(static_cast<long long>(physicalVsHr)) +
                          (physicalVsError ? (" " + std::string(static_cast<const char*>(physicalVsError->GetBufferPointer())))
                                           : std::string()));
        }
        if (FAILED(physicalPsHr)) {
            LogCouchD3D12("[COUCH-R7C5][PHYSICAL_SHADER][PS_ERROR] HRESULT=" +
                          std::to_string(static_cast<long long>(physicalPsHr)) +
                          (physicalPsError ? (" " + std::string(static_cast<const char*>(physicalPsError->GetBufferPointer())))
                                           : std::string()));
        }
        std::string physicalHdrHlsl = "#define COUCH_HDR_OUTPUT 1\n" +
            makeLinearHdrVariant(physicalHlsl, !enhancedPhysicalResponse);
        if (physicalResponseBeforeFog) {
            // The HDR material response now precedes fog/grayscale blending,
            // so those display-authored colors must share its linear space.
            for (const std::string inputColor : { "input.fog.rgb", "input.grayscale.rgb" }) {
                const std::string linearColor = "CouchPhysicalInput(" + inputColor + ")";
                size_t at = 0;
                while ((at = physicalHdrHlsl.find(inputColor, at)) != std::string::npos) {
                    physicalHdrHlsl.replace(at, inputColor.size(), linearColor);
                    at += linearColor.size();
                }
            }
        }
        ComPtr<ID3DBlob> physicalHdrPsError;
        const HRESULT physicalHdrPsHr = mD3dCompile(physicalHdrHlsl.c_str(), physicalHdrHlsl.length(), nullptr,
            nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &prog.physicalHdrPsBlob, &physicalHdrPsError);
        if (FAILED(physicalHdrPsHr)) {
            LogCouchD3D12("[COUCH-R7C5][PHYSICAL_SHADER][HDR_PS_ERROR] HRESULT=" +
                std::to_string(static_cast<long long>(physicalHdrPsHr)) +
                (physicalHdrPsError ? (" " + std::string(static_cast<const char*>(physicalHdrPsError->GetBufferPointer())))
                                    : std::string()));
        }
        LogCouchD3D12("[COUCH-R7C5][PHYSICAL_SHADER] VS=" + std::string(SUCCEEDED(physicalVsHr) ? "PASS" : "FAIL") +
                      " PS=" + std::string(SUCCEEDED(physicalPsHr) ? "PASS" : "FAIL") +
                      " HDR_PS=" + std::string(SUCCEEDED(physicalHdrPsHr) ? "PASS" : "FAIL"));
        prog.physicalShaderValid = SUCCEEDED(physicalVsHr) && SUCCEEDED(physicalPsHr) &&
                                   SUCCEEDED(physicalHdrPsHr) &&
                                   physicalSidebandFieldsInserted && physicalSidebandParamsInserted &&
                                   physicalSidebandAssignmentsInserted;
        LogCouchD3D12("[COUCH-R7C5][PHYSICAL_CONTRACT] valid=" +
                      std::string(prog.physicalShaderValid ? "YES" : "NO") +
                      " fields=" + std::string(physicalSidebandFieldsInserted ? "YES" : "NO") +
                      " params=" + std::string(physicalSidebandParamsInserted ? "YES" : "NO") +
                      " assignments=" + std::string(physicalSidebandAssignmentsInserted ? "YES" : "NO"));
    }

    mCurrentShaderProgram = &prog;
    return reinterpret_cast<struct ShaderProgram*>(&prog);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> GfxRenderingAPIDX12::GetOrCreatePipelineState(const PsoKey& key, ShaderProgramD3D12* prog) {
    auto it = mPipelineStates.find(key);
    if (it != mPipelineStates.end()) return it->second;

    if (!prog || !prog->vsBlob || !prog->psBlob) return nullptr;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = mRootSignature.Get();
    const bool physical = key.physicalInputs;
    if (physical && !prog->physicalShaderValid) return nullptr;
    auto& vsBlob = physical ? prog->physicalVsBlob : prog->vsBlob;
    auto& psBlob = physical
        ? (key.hdrOutput ? prog->physicalHdrPsBlob : prog->physicalPsBlob)
        : (key.hdrOutput ? prog->hdrPsBlob : prog->psBlob);
    auto& inputElements = physical ? prog->physicalInputElements : prog->inputElements;
    if (!vsBlob || !psBlob) return nullptr;
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.InputLayout = { inputElements.data(), static_cast<UINT>(inputElements.size()) };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = key.hdrOutput ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = UINT_MAX;

    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.RasterizerState.SlopeScaledDepthBias = key.decal ? -2.0f : 0.0f;

    psoDesc.DepthStencilState.DepthEnable = key.depthTest ? TRUE : FALSE;
    psoDesc.DepthStencilState.DepthWriteMask = key.depthMask ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    auto& rtBlend = psoDesc.BlendState.RenderTarget[0];
    if (key.useAlpha) {
        rtBlend.BlendEnable = TRUE;
        rtBlend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        rtBlend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        rtBlend.BlendOp = D3D12_BLEND_OP_ADD;
        rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
        rtBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
        rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    } else {
        rtBlend.BlendEnable = FALSE;
    }
    rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    ComPtr<ID3D12PipelineState> pso;
    HRESULT hr = mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso));
    if (SUCCEEDED(hr)) {
        mPipelineStates[key] = pso;
        LogCouchD3D12("[COUCH-D3D12][PSO_CREATED] id=" + std::to_string(key.shaderId0) + "_" + std::to_string(key.shaderId1));

        if (!mDiagnosticPSOsCreated && !mSavedVSBlob) {
            mSavedVSBlob = prog->vsBlob;
            mSavedInputElements = prog->inputElements;
            LogCouchD3D12("[COUCH-R4] Saved VS blob from first PSO: " + std::to_string(key.shaderId0) + "_" + std::to_string(key.shaderId1));
            CreateDiagnosticPSOs();
            if (mPixelIsoMode >= 20) {
                CreateR4bDiagnosticPSOs();
            }
        }

        return pso;
    }

    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << (uint32_t)hr;
    LogCouchD3D12("[COUCH-D3D12][PSO-FAIL] id=" + std::to_string(key.shaderId0) +
                  " hr=" + ss.str() +
                  " shader=" + std::to_string(key.shaderId0) + "_" + std::to_string(key.shaderId1) +
                  " rootSig=" + std::string(mRootSignature != nullptr ? "VALID" : "NULL") +
                  " rtv=0x" + std::to_string(psoDesc.RTVFormats[0]) +
                  " dsv=0x" + std::to_string(psoDesc.DSVFormat));
    DumpDebugMessages();
    return nullptr;
}

bool GfxRenderingAPIDX12::ValidateDraw(const char* stage) {
    if (!mCurrentShaderProgram) {
        LogCouchD3D12("[COUCH-D3D12][INVALID_DRAW] " + std::string(stage) + " MISSING_SHADER_PROGRAM");
        mInvalidDrawCount++;
        return false;
    }
    if (!mCurrentShaderProgram->vsBlob || !mCurrentShaderProgram->psBlob) {
        LogCouchD3D12("[COUCH-D3D12][INVALID_DRAW] " + std::string(stage) + " INCOMPLETE_SHADER_BLOBS");
        mInvalidDrawCount++;
        return false;
    }
    if (!mRootSignature) {
        LogCouchD3D12("[COUCH-D3D12][INVALID_DRAW] " + std::string(stage) + " MISSING_ROOT_SIGNATURE");
        mInvalidDrawCount++;
        return false;
    }
    if (!mCommandListOpen) {
        LogCouchD3D12("[COUCH-D3D12][INVALID_DRAW] " + std::string(stage) + " COMMAND_LIST_CLOSED");
        mInvalidDrawCount++;
        return false;
    }
    return true;
}

void GfxRenderingAPIDX12::DrawTriangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    // Consume the pending packet into locals before any validation can return.
    // This prevents a failed draw from leaking the previous draw's physical
    // stream into a later legacy draw.
    const PhysicalVertexData* physicalVertices = mPendingPhysicalVertices;
    const size_t physicalVertexCount = mPendingPhysicalVertexCount;
    const PhysicalDrawData physicalDraw = mPendingPhysicalDraw;
    mPendingPhysicalVertices = nullptr;
    mPendingPhysicalVertexCount = 0;
    mPendingPhysicalDraw = {};

    if (!mInitialized || !mCommandListOpen || buf_vbo_num_tris == 0) return;

    if (mR7c5A5Enabled) {
        ++mR7c5A5DrawTrianglesEntered;
        if (mR7c5A5DrawTrianglesEntered <= 3) {
            LogCouchD3D12("[COUCH-R7C5-A5][D3D12_DRAWTRIANGLES_ENTERED] count=" +
                          std::to_string(mR7c5A5DrawTrianglesEntered));
        }
    }

    if (!ValidateDraw("DrawTriangles")) return;

    bool physicalInputs = physicalVertices != nullptr &&
                          physicalVertexCount == buf_vbo_num_tris * 3 &&
                          mCurrentShaderProgram->physicalShaderValid;
    size_t physicalBytes = physicalInputs ? physicalVertexCount * sizeof(PhysicalVertexData) : 0;
    if (physicalInputs && physicalBytes > kRingBufferSliceSize) {
        LogCouchD3D12("[COUCH-R7C5][PHYSICAL_TRANSPORT] single draw exceeds ring slice; using LEGACY");
        physicalInputs = false;
        physicalBytes = 0;
    }

    PsoKey psoKey = {
        mCurrentShaderProgram->shaderId0,
        mCurrentShaderProgram->shaderId1,
        mCurrentDepthTest,
        mCurrentDepthMask,
        mCurrentZmodeDecal,
        mCurrentUseAlpha
        , physicalInputs,
        FramebufferColor(mCurrentFramebuffer) &&
            FramebufferColor(mCurrentFramebuffer)->GetDesc().Format == DXGI_FORMAT_R16G16B16A16_FLOAT
    };

    auto pso = GetOrCreatePipelineState(psoKey, mCurrentShaderProgram);
    if (!pso) {
        LogCouchD3D12("[COUCH-D3D12][INVALID_DRAW] DrawTriangles PSO_CREATION_FAILED");
        mInvalidDrawCount++;
        return;
    }

    // ============================================================
    // R4-B — canonical draw capture (once) + frozen diagnostic override
    // ============================================================
    bool isCanonicalDraw = false;
    if (mPixelIsoMode >= 20 || mR7c5A5Enabled) {
        const int floatsPerVtx = mCurrentShaderProgram->numFloats;
        const UINT candidateVerts = static_cast<UINT>(buf_vbo_num_tris * 3);

        // --- capture phase: pick the FIRST valid draw of a settled frame ---
        if (!mR4b.captured && forensic::gForensicFrame >= R4bMinFrame()) {
            mR4b.settled = true;

            bool areaValid = false;
            double area = 0.0;
            int inside = 0, dumped = 0;
            if (floatsPerVtx >= 4 && buf_vbo_len >= (size_t)(3 * floatsPerVtx)) {
                const float* v[3] = { buf_vbo, buf_vbo + floatsPerVtx, buf_vbo + 2 * floatsPerVtx };
                bool finite = true;
                for (int i = 0; i < 3; ++i) {
                    float x = v[i][0], y = v[i][1], z = v[i][2], w = v[i][3];
                    if (!(std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(w))) finite = false;
                }
                if (finite) {
                    double ax = v[0][0] * v[0][3], ay = v[0][1] * v[0][3];
                    double bx = v[1][0] * v[1][3], by = v[1][1] * v[1][3];
                    double cx = v[2][0] * v[2][3], cy = v[2][1] * v[2][3];
                    area = 0.5 * ((bx - ax) * (cy - ay) - (cx - ax) * (by - ay));
                    area = area < 0 ? -area : area;
                    for (int i = 0; i < 3; ++i) {
                        float x = v[i][0], y = v[i][1], z = v[i][2], w = v[i][3];
                        if (w != 0.0f &&
                            x > -w && x < w && y > -w && y < w && z > -w && z < w) ++inside;
                    }
                    areaValid = (area > (double)R4bMinArea());
                    dumped = 3;
                    for (int i = 0; i < 3; ++i) {
                        for (int f = 0; f < 8 && f < floatsPerVtx; ++f) mR4b.dumpV[i][f] = v[i][f];
                    }
                }
            }

            if (areaValid) {
                mR4b.captured = true;
                mR4b.frame = forensic::gForensicFrame;
                mR4b.drawIndex = 0;
                mR4b.shaderId0 = psoKey.shaderId0;
                mR4b.shaderId1 = psoKey.shaderId1;
                mR4b.vertexCount = candidateVerts;
                mR4b.strideBytes = floatsPerVtx * (int)sizeof(float);
                mR4b.triangleArea = area;
                mR4b.insideClip = inside;
                mR4b.totalVerts = dumped;
                mR4b.dumpFloats = (floatsPerVtx < 8) ? floatsPerVtx : 8;
                mR4b.depthTest = psoKey.depthTest;
                mR4b.depthMask = psoKey.depthMask;
                mR4b.decal = psoKey.decal;
                mR4b.useAlpha = psoKey.useAlpha;
                mR4b.tex0W = (mCurrentTextureIds[0] < mTextures.size()) ? mTextures[mCurrentTextureIds[0]].width : 0;
                mR4b.tex0H = (mCurrentTextureIds[0] < mTextures.size()) ? mTextures[mCurrentTextureIds[0]].height : 0;

                LogCouchD3D12("[COUCH-R4B][CANONICAL_CAPTURE] frame=" + std::to_string(mR4b.frame) +
                              " drawIndex=" + std::to_string(mR4b.drawIndex) +
                              " shader=" + std::to_string(mR4b.shaderId0) + "_" + std::to_string(mR4b.shaderId1) +
                              " vertexCount=" + std::to_string(mR4b.vertexCount) +
                              " stride=" + std::to_string(mR4b.strideBytes) +
                              " area=" + std::to_string(mR4b.triangleArea) +
                              " insideClip=" + std::to_string(inside) + "/" + std::to_string(dumped));
                LogCouchD3D12("[COUCH-R4B][CANONICAL_CAPTURE] states: depthTest=" + std::to_string(mR4b.depthTest) +
                              " depthMask=" + std::to_string(mR4b.depthMask) +
                              " decal=" + std::to_string(mR4b.decal) +
                              " alpha=" + std::to_string(mR4b.useAlpha) +
                              " tex0=" + std::to_string(mR4b.tex0W) + "x" + std::to_string(mR4b.tex0H));
                LogCouchD3D12("[COUCH-R4B][CANONICAL_CAPTURE] viewport=(" +
                              std::to_string(mViewport.TopLeftX) + "," + std::to_string(mViewport.TopLeftY) + "," +
                              std::to_string(mViewport.Width) + "," + std::to_string(mViewport.Height) + ")");
                LogCouchD3D12("[COUCH-R4B][CANONICAL_CAPTURE] scissor=(" +
                              std::to_string(mScissorRect.left) + "," + std::to_string(mScissorRect.top) + "," +
                              std::to_string(mScissorRect.right) + "," + std::to_string(mScissorRect.bottom) + ")");

                // ---- INPUT LAYOUT AUDIT (PSO desc vs real vertex buffer) ----
                {
                    const auto& elems = mCurrentShaderProgram->inputElements;
                    UINT psoStride = 0;
                    UINT posOffset = UINT_MAX, tcOffset = UINT_MAX, colOffset = UINT_MAX;
                    bool hasPosition = false;
                    for (const auto& el : elems) {
                        if (el.InputSlotClass != D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA) continue;
                        if (el.AlignedByteOffset != D3D12_APPEND_ALIGNED_ELEMENT && el.AlignedByteOffset > psoStride) {
                            psoStride = el.AlignedByteOffset;
                        }
                        if (strcmp(el.SemanticName, "POSITION") == 0) { hasPosition = true; posOffset = el.AlignedByteOffset; }
                        else if (strncmp(el.SemanticName, "TEXCOORD", 8) == 0 && tcOffset == UINT_MAX) tcOffset = el.AlignedByteOffset;
                        else if (strncmp(el.SemanticName, "INPUT", 5) == 0 && colOffset == UINT_MAX) colOffset = el.AlignedByteOffset;
                    }
                    psoStride += 4;
                    UINT vbStride = floatsPerVtx * 4;
                    mR4b.layoutStrideMatch = (psoStride == vbStride);
                    LogCouchD3D12("[COUCH-R4B][AUDIT][LAYOUT] hasPosition=" + std::string(hasPosition ? "YES" : "NO") +
                                  " psoStride=" + std::to_string(psoStride) +
                                  " vbStride=" + std::to_string(vbStride) +
                                  " match=" + std::string(mR4b.layoutStrideMatch ? "YES" : "MISMATCH"));
                    auto fmtName = [](DXGI_FORMAT f) -> std::string {
                        switch (f) {
                            case DXGI_FORMAT_R32G32B32A32_FLOAT: return "R32G32B32A32_FLOAT";
                            case DXGI_FORMAT_R32G32B32_FLOAT: return "R32G32B32_FLOAT";
                            case DXGI_FORMAT_R32G32_FLOAT: return "R32G32_FLOAT";
                            case DXGI_FORMAT_R32_FLOAT: return "R32_FLOAT";
                            default: return "fmt_" + std::to_string((int)f);
                        }
                    };
                    for (const auto& el : elems) {
                        LogCouchD3D12(std::string("[COUCH-R4B][AUDIT][LAYOUT] elem=") + el.SemanticName +
                                      std::to_string(el.SemanticIndex) + " fmt=" + fmtName(el.Format) +
                                      " slot=" + std::to_string(el.InputSlot) +
                                      " offset=" + (el.AlignedByteOffset == D3D12_APPEND_ALIGNED_ELEMENT
                                                        ? std::string("APPEND") : std::to_string(el.AlignedByteOffset)));
                    }
                    LogCouchD3D12("[COUCH-R4B][AUDIT][LAYOUT] POSITION_OFFSET=" +
                                  std::string(posOffset == UINT_MAX ? "MISSING" : std::to_string(posOffset)) +
                                  " TEXCOORD_OFFSET=" +
                                  std::string(tcOffset == UINT_MAX ? "NONE" : std::to_string(tcOffset)) +
                                  " COLOR_OFFSET=" +
                                  std::string(colOffset == UINT_MAX ? "NONE" : std::to_string(colOffset)));
                    if (!hasPosition || !mR4b.layoutStrideMatch) {
                        LogCouchD3D12("[COUCH-R4B][AUDIT][LAYOUT] INPUT_LAYOUT_MISMATCH=YES");
                    }
                }

                // ---- TOPOLOGY AUDIT ----
                // PSO PrimitiveTopologyType = TRIANGLE y IASetPrimitiveTopology = TRIANGLELIST
                mR4b.topologyMatch = true;
                LogCouchD3D12("[COUCH-R4B][AUDIT][TOPOLOGY] pso=TRIANGLE draw=TRIANGLELIST match=YES");

                // ---- DRAW ARGUMENTS AUDIT ----
                {
                    UINT64 need = (UINT64)candidateVerts * mR4b.strideBytes;
                    mR4b.drawArgsValid = (buf_vbo_len == (size_t)candidateVerts * (size_t)floatsPerVtx) &&
                                         (need <= kRingBufferSliceSize);
                    LogCouchD3D12("[COUCH-R4B][AUDIT][DRAWARGS] DrawInstanced VertexCountPerInstance=" +
                                  std::to_string(candidateVerts) + " InstanceCount=1 StartVertex=0 StartInstance=0" +
                                  " bytesNeeded=" + std::to_string(need) +
                                  " valid=" + std::string(mR4b.drawArgsValid ? "YES" : "NO"));
                }

                // ---- COLOR WRITE AUDIT ----
                mR4b.colorWriteValid = true; // canonical PSO: RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL
                LogCouchD3D12("[COUCH-R4B][AUDIT][COLORWRITE] RenderTargetWriteMask=RGBA COLOR_WRITE_VALID=YES");

                // ---- VERTEX DUMP (CPU input, primeros 3-6 vértices) ----
                {
                    int dumpCount = (candidateVerts < 6) ? (int)candidateVerts : 6;
                    for (int sv = 0; sv < dumpCount; ++sv) {
                        const float* v = buf_vbo + (size_t)sv * floatsPerVtx;
                        std::string vtx = "[V" + std::to_string(sv) + "] ";
                        for (int f = 0; f < floatsPerVtx; f++) {
                            vtx += "f" + std::to_string(f) + "=" + std::to_string(v[f]) + " ";
                        }
                        if (floatsPerVtx >= 4 && v[3] != 0.0f) {
                            float ndcX = v[0] / v[3], ndcY = v[1] / v[3], ndcZ = v[2] / v[3];
                            vtx += "| NDC=(" + std::to_string(ndcX) + "," + std::to_string(ndcY) + "," +
                                   std::to_string(ndcZ) + ")";
                        }
                        LogCouchD3D12("[COUCH-R4B][CANONICAL_DUMP] " + vtx);
                    }
                    mR4b.dumped = true;
                }
            }
        }

        // --- frozen override: apply the diagnostic PSO ONLY on the captured draw ---
        isCanonicalDraw = mR4b.captured &&
                          psoKey.shaderId0 == mR4b.shaderId0 && psoKey.shaderId1 == mR4b.shaderId1 &&
                          candidateVerts == mR4b.vertexCount;
        if (isCanonicalDraw) {
            ComPtr<ID3D12PipelineState> diagPSO;
            switch (mPixelIsoMode) {
                case 20: diagPSO = mR4bPSO_C0; break;
                case 21: diagPSO = mR4bPSO_C1; break;
                case 22: diagPSO = mR4bPSO_C2; break;
                case 23: diagPSO = mR4bPSO_C3; break;
                case 24: diagPSO = mR4bPSO_C4; break;
                default: break;
            }
            if (diagPSO) {
                pso = diagPSO;
                if (mPixelIsoMode == 23) {
                    // C3: forzar full viewport/scissor SOLO para este draw
                    D3D12_VIEWPORT fullVp = { 0.0f, 0.0f, (float)mRenderWidth, (float)mRenderHeight, 0.0f, 1.0f };
                    D3D12_RECT fullSc = { 0, 0, (LONG)mRenderWidth, (LONG)mRenderHeight };
                    mCommandList->RSSetViewports(1, &fullVp);
                    mCommandList->RSSetScissorRects(1, &fullSc);
                    mViewportDirty = true;
                    mScissorDirty = true;
                    mViewport = fullVp;
                    mScissorRect = fullSc;
                }
            }
        }
    }

    mCommandList->SetPipelineState(pso.Get());

    if (mViewportDirty) {
        mCommandList->RSSetViewports(1, &mViewport);
        mViewportDirty = false;
    }
    if (mScissorDirty) {
        mCommandList->RSSetScissorRects(1, &mScissorRect);
        mScissorDirty = false;
    }

    const size_t vtxBytes = buf_vbo_len * sizeof(float);
    auto& ctx = mFrameContexts[mFrameIndex];

    size_t alignedOffset = (ctx.ringBufferUsed + D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1) & ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1);
    size_t physicalAlignedOffset = (alignedOffset + vtxBytes + 15u) & ~size_t(15u);

    if (alignedOffset + vtxBytes > kRingBufferSliceSize ||
        (physicalBytes > 0 && physicalAlignedOffset + physicalBytes > kRingBufferSliceSize)) {
        ctx.ringBufferUsed = 0;
        alignedOffset = 0;
        physicalAlignedOffset = (alignedOffset + vtxBytes + 15u) & ~size_t(15u);

        WaitForFrame(mFrameIndex);
    }

    size_t globalOffset = ctx.ringBufferBase + alignedOffset;

    if (globalOffset + vtxBytes > kUploadBufferSize) {
        LogCouchD3D12("[COUCH-D3D12][INVALID_DRAW] DrawTriangles RING_BUFFER_OVERFLOW");
        mInvalidDrawCount++;
        return;
    }

    memcpy(mUploadBufferMapped + globalOffset, buf_vbo, vtxBytes);
    size_t physicalGlobalOffset = ctx.ringBufferBase + physicalAlignedOffset;
    if (physicalBytes > 0 &&
        (physicalAlignedOffset + physicalBytes > kRingBufferSliceSize ||
         physicalGlobalOffset + physicalBytes > kUploadBufferSize)) {
        LogCouchD3D12("[COUCH-D3D12][INVALID_DRAW] DrawTriangles PHYSICAL_RING_BUFFER_OVERFLOW");
        mInvalidDrawCount++;
        return;
    }
    if (psoKey.physicalInputs) {
        static bool loggedPhysicalSelection = false;
        if (!loggedPhysicalSelection) {
            LogCouchD3D12("[COUCH-R7C5][PHYSICAL_PSO_SELECTED] PHYSICAL_INPUTS_BIT=1");
            loggedPhysicalSelection = true;
        }
    }
    if (physicalBytes > 0) memcpy(mUploadBufferMapped + physicalGlobalOffset, physicalVertices, physicalBytes);
    ctx.ringBufferUsed = physicalBytes > 0 ? physicalAlignedOffset + physicalBytes : alignedOffset + vtxBytes;

    D3D12_VERTEX_BUFFER_VIEW vbView = {};
    vbView.BufferLocation = mUploadBuffer->GetGPUVirtualAddress() + globalOffset;
    vbView.SizeInBytes = static_cast<UINT>(vtxBytes);
    vbView.StrideInBytes = static_cast<UINT>(mCurrentShaderProgram->numFloats * sizeof(float));

    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    if (mPixelIsoMode == 24 && mR4b.captured && pso.Get() == mR4bPSO_C4.Get()) {
        mCommandList->IASetVertexBuffers(0, 0, nullptr);
    } else {
        if (physicalBytes > 0) {
            D3D12_VERTEX_BUFFER_VIEW physicalView = {};
            physicalView.BufferLocation = mUploadBuffer->GetGPUVirtualAddress() + physicalGlobalOffset;
            physicalView.SizeInBytes = static_cast<UINT>(physicalBytes);
            physicalView.StrideInBytes = sizeof(PhysicalVertexData);
            D3D12_VERTEX_BUFFER_VIEW views[2] = { vbView, physicalView };
            mCommandList->IASetVertexBuffers(0, 2, views);
            static bool loggedPhysicalSlot = false;
            if (!loggedPhysicalSlot) {
                LogCouchD3D12("[COUCH-R7C5][IA_BIND] LEGACY_SLOT_BOUND=YES PHYSICAL_SLOT_BOUND=YES SLOT1_STRIDE=32");
                loggedPhysicalSlot = true;
            }
        } else {
            mCommandList->IASetVertexBuffers(0, 1, &vbView);
        }
    }

    uint32_t perFrameConstants[4] = { 0, 0, 0, 0 };
    mCommandList->SetGraphicsRoot32BitConstants(0, 4, perFrameConstants, 0);

    uint32_t physicalConstants[40] = {
        physicalInputs ? physicalDraw.materialIndex : 0u,
        physicalInputs ? physicalDraw.flags : 0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
    };
    if (physicalInputs) {
        static_assert(sizeof(physicalDraw.viewLightDirection) == 4 * sizeof(uint32_t));
        memcpy(&physicalConstants[4], physicalDraw.viewLightDirection, sizeof(physicalDraw.viewLightDirection));
        memcpy(&physicalConstants[8], physicalDraw.sceneLightColor, sizeof(physicalDraw.sceneLightColor));
        memcpy(&physicalConstants[12], physicalDraw.ambientLightColor, sizeof(physicalDraw.ambientLightColor));
        memcpy(&physicalConstants[16], physicalDraw.viewPointLightPosition, sizeof(physicalDraw.viewPointLightPosition));
        memcpy(&physicalConstants[20], physicalDraw.pointLightOptics, sizeof(physicalDraw.pointLightOptics));
        memcpy(&physicalConstants[24], physicalDraw.viewPointLightPosition2, sizeof(physicalDraw.viewPointLightPosition2));
        memcpy(&physicalConstants[28], physicalDraw.pointLightOptics2, sizeof(physicalDraw.pointLightOptics2));
        memcpy(&physicalConstants[32], physicalDraw.materialSurface, sizeof(physicalDraw.materialSurface));
        memcpy(&physicalConstants[36], physicalDraw.materialOptics, sizeof(physicalDraw.materialOptics));
    }
    mCommandList->SetGraphicsRoot32BitConstants(2, 40, physicalConstants, 0);

    uint32_t perDrawConstants[8] = { 32, 32, 1, 0, 32, 32, 1, 0 };
    for (uint32_t i = 0; i < SHADER_MAX_TEXTURES; ++i) {
        uint32_t boundSrvIndex = kNullSrvIndex;
        if (mCurrentTextureIds[i] < mTextures.size()) {
            auto& tex = mTextures[mCurrentTextureIds[i]];
            if (i < 2) {
                const size_t constantOffset = i * 4;
                perDrawConstants[constantOffset + 0] = tex.width > 0 ? tex.width : 32;
                perDrawConstants[constantOffset + 1] = tex.height > 0 ? tex.height : 32;
            }
            if (tex.uploaded && tex.resource) {
                boundSrvIndex = tex.srvIndex;
            }
        }

        D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = mSrvHeap->GetGPUDescriptorHandleForHeapStart();
        srvGpu.ptr += static_cast<SIZE_T>(boundSrvIndex) * mSrvDescriptorSize;
        mCommandList->SetGraphicsRootDescriptorTable(3 + i, srvGpu);

        D3D12_GPU_DESCRIPTOR_HANDLE samplerGpu = mSamplerHeap->GetGPUDescriptorHandleForHeapStart();
        samplerGpu.ptr += static_cast<SIZE_T>(mCurrentSamplerIndices[i]) * mSamplerDescriptorSize;
        mCommandList->SetGraphicsRootDescriptorTable(9 + i, samplerGpu);
    }

    mCommandList->SetGraphicsRoot32BitConstants(1, 8, perDrawConstants, 0);

    const UINT vertexCount = static_cast<UINT>(buf_vbo_num_tris * 3);
    if (mR7c5A5Enabled && isCanonicalDraw && !mR7c5A5QueryArmed && !mR7c5A5QueryDone && mR4bStatsHeap) {
        mCommandList->BeginQuery(mR4bStatsHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
        mR4bStatsQueryStartFrame = forensic::gForensicFrame;
        mR7c5A5QueryArmed = true;
        LogCouchD3D12("[COUCH-R7C5-A5][PIPELINE_QUERY_BEGIN] canonical_draw=1");
    }
    mCommandList->DrawInstanced(vertexCount, 1, 0, 0);
    if (mR7c5A5Enabled && mR7c5A5QueryArmed) {
        mCommandList->EndQuery(mR4bStatsHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
        LogCouchD3D12("[COUCH-R7C5-A5][PIPELINE_QUERY_END] canonical_draw=1");
        mR7c5A5QueryArmed = false;
        mR7c5A5QueryDone = true;
    }

    mDrawCount++;
    mVertexCount += vertexCount;
    mTriangleCount += buf_vbo_num_tris;
}

void GfxRenderingAPIDX12::DrawTrianglesPhysical(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris,
                                                 const PhysicalVertexData* physicalVertices, size_t physicalVertexCount,
                                                 const PhysicalDrawData& physicalDraw) {
    if (mR7c5A5Enabled) {
        ++mR7c5A5DrawTrianglesPhysicalEntered;
        if (mR7c5A5DrawTrianglesPhysicalEntered <= 3) {
            LogCouchD3D12("[COUCH-R7C5-A5][D3D12_DRAWTRIANGLESPHYSICAL_ENTERED] count=" +
                          std::to_string(mR7c5A5DrawTrianglesPhysicalEntered));
        }
    }
    const char* forceLegacy = std::getenv("COUCH_R7C5_FORCE_LEGACY");
    const char* usePhysical = std::getenv("COUCH_R7C5_USE_PHYSICAL");
    const bool enhancedMaterialProfile = Fast::MaterialRegistry::IsEnhancedVisualProfile();
    const bool forcePhysicalOff = usePhysical && std::string(usePhysical) == "0";
    const bool forceLegacyOn = forceLegacy && std::string(forceLegacy) == "1";
    // Enhanced and Hybrid RT use the normal/position sideband by default so
    // classified material response reaches the raster image without a hidden
    // environment-variable opt-in. Keep both explicit legacy overrides for
    // visual diagnosis; invalid or oversized per-draw streams still fall back
    // to the original Fast3D layout below.
    const bool physicalTransportEnabled = enhancedMaterialProfile && !forcePhysicalOff && !forceLegacyOn;
    if (!physicalTransportEnabled || physicalVertices == nullptr ||
        physicalVertexCount != buf_vbo_num_tris * 3 || sizeof(PhysicalVertexData) != 32) {
        static bool loggedPhysicalFallback = false;
        if (!loggedPhysicalFallback) {
            LogCouchD3D12("[COUCH-R7C5][PHYSICAL_TRANSPORT] profile=" +
                          std::string(enhancedMaterialProfile ? "ENHANCED" : "CLASSIC") +
                          " transport=" + std::string(physicalTransportEnabled ? "ON" : "OFF") +
                          " env=" + std::string(usePhysical ? usePhysical : "(unset)") +
                          " legacyOverride=" + std::string(forceLegacyOn ? "ON" : "OFF"));
            loggedPhysicalFallback = true;
        }
        DrawTriangles(buf_vbo, buf_vbo_len, buf_vbo_num_tris);
        return;
    }
    mPendingPhysicalVertices = physicalVertices;
    mPendingPhysicalVertexCount = physicalVertexCount;
    mPendingPhysicalDraw = physicalDraw;
    DrawTriangles(buf_vbo, buf_vbo_len, buf_vbo_num_tris);
}

} // namespace Fast

#endif
