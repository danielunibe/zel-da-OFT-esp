#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "functions.h"

extern "C" {
#include "z64.h"
#include "overlays/gamestates/ovl_file_choose/file_choose.h"
#include "overlays/actors/ovl_En_Elf/z_en_elf.h"
}

#include <nlohmann/json.hpp>

#define NOMINMAX
#include "fast/MaterialRegistry.h"
#if defined(_WIN32)
#include "fast/backends/DxrSurfaceRenderer.h"
#endif
#include "ship/Context.h"
#include "ship/config/ConsoleVariable.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

extern "C" SaveContext gSaveContext;
extern "C" PlayState* gPlayState;

namespace {
using Json = nlohmann::json;
using namespace std::string_view_literals;

enum class HarnessState { Disabled, Booting, SelectingSlot, WaitingForScene, Stabilizing, Capturing,
                          CaptureComplete, Passed, Failed };

bool IsHarnessTerminal(HarnessState state) {
    return state == HarnessState::CaptureComplete || state == HarnessState::Passed ||
           state == HarnessState::Failed;
}

struct HarnessConfig {
    std::filesystem::path outputRoot;
    Json manifest;
    Json scenario;
    Json view;
    std::string scenarioId;
    std::string viewId;
    std::string truthMode;
    int backendTruthMode = 0;
    int entrance = -1;
    int expectedScene = -1;
    int expectedRoom = -1;
    int linkAge = -1;
    int dayTime = -1;
    int nightFlag = -1;
    int stableFramesRequired = 30;
    int cameraWarmupFrames = 120;
    int timeoutFrames = 1200;
    bool freezeTime = true;
    bool freezePlayer = true;
};

HarnessState sState = HarnessState::Disabled;
HarnessConfig sConfig;
std::uint64_t sGameFrame = 0;
std::uint64_t sStateStartFrame = 0;
std::uint64_t sLastViewHash = 0;
std::uint64_t sLastProjectionHash = 0;
std::uint64_t sLastPlayerHash = 0;
int sStableFrameCount = 0;
std::array<s8, 2> sRawCameraStick{};
bool sSaveLoaded = false;
bool sDxrReady = false;
bool sDxrRuntimeValidated = false;
bool sGameLightProductive = false;
std::string sGameLightSyncStatus = "UNKNOWN";
bool sPlayerFreezeRequested = false;
bool sTimeCaptured = false;
bool sMainSlotInputInjected = false;
bool sConfirmInputInjected = false;
bool sFrozenPlayerTransformCaptured = false;
std::array<float, 6> sFrozenPlayerTransform{};
float sFrozenPlayerFloorHeight = 0.0f;
bool sFrozenPlayerFocusCaptured = false;
bool sCameraFrozen = false;
Vec3f sFrozenCameraEye{};
Vec3f sFrozenCameraAt{};
Vec3f sFrozenCameraUp{};
float sFrozenCameraFovy = 60.0f;
EnElf* sNaviOtherFairy = nullptr;
Vec3f sNaviOtherFairyPosition{};
std::array<float, 3> sFrozenPlayerFocusPosition{};
std::array<s16, 3> sFrozenPlayerFocusRotation{};
std::uint64_t sLastHeartbeatFrame = 0;
u16 sFrozenDayTime = 0;
u16 sFrozenSkyboxTime = 0;

std::string ReadEnv(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}

std::uint64_t HashBytes(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string HexValue(int value) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << value;
    return out.str();
}

Json BuildLightingContractEvidence() {
    Json contract = sConfig.scenario.value("lightingContract", Json::object());
    if (!contract.is_object() || contract.empty()) return Json::object();

    contract["runtimeStatus"] = "CAPTURED_NO_FEATURE_ASSERTIONS";
    contract["comparisonStatus"] = "NOT_RUN";
    contract["visualAcceptance"] = "NOT_RECORDED";
    if (contract.contains("targetMaterials") && contract["targetMaterials"].is_array()) {
        for (auto& target : contract["targetMaterials"]) {
            if (target.is_object()) target["runtimeStatus"] = "NOT_RECORDED";
        }
    }
    if (contract.contains("runtimeGates") && contract["runtimeGates"].is_array()) {
        for (auto& gate : contract["runtimeGates"]) {
            if (gate.is_object()) gate["observedStatus"] = "NOT_RUN";
        }
    }
    return contract;
}

void WriteJson(const std::filesystem::path& path, const Json& data) {
    std::filesystem::create_directories(path.parent_path());
    const auto tmpPath = path.string() + ".tmp";
    {
        std::ofstream output(tmpPath, std::ios::binary | std::ios::trunc);
        output << std::setw(2) << data << '\n';
        output.flush();
    }
    std::error_code ec;
    std::filesystem::rename(tmpPath, path, ec);
}

void Log(const std::string& event, const Json& fields = Json::object()) {
    Json record = fields;
    record["event"] = event;
    record["marker"] = "[HARNESS_" + event + "]";
    record["scenario_id"] = sConfig.scenarioId;
    record["view_id"] = sConfig.viewId;
    record["truth_mode"] = sConfig.truthMode;
    record["game_frame"] = sGameFrame;
    std::filesystem::create_directories(sConfig.outputRoot);
    std::ofstream output(sConfig.outputRoot / "harness.log", std::ios::binary | std::ios::app);
    if (output) output << std::setw(0) << record << '\n';
}

void Fail(const std::string& code, const std::string& message) {
    if (IsHarnessTerminal(sState)) return;
    sState = HarnessState::Failed;
    Json evidence = {{"schema_version", 1}, {"result", "FAIL"}, {"blocker", code}, {"message", message},
                     {"scenario_id", sConfig.scenarioId}, {"view_id", sConfig.viewId},
                     {"truth_mode", sConfig.truthMode}, {"game_frame", sGameFrame}};
    if (gPlayState) {
        evidence["scene"] = HexValue(gPlayState->sceneNum);
        evidence["room"] = gPlayState->roomCtx.curRoom.num;
        evidence["view_hash"] = std::to_string(HashBytes(&gPlayState->view.viewing, sizeof(gPlayState->view.viewing)));
        evidence["projection_hash"] = std::to_string(HashBytes(&gPlayState->view.projection, sizeof(gPlayState->view.projection)));
        evidence["dxr_ready"] = sDxrReady;
    }
    if (!sConfig.outputRoot.empty()) {
        WriteJson(sConfig.outputRoot / "evidence.json", evidence);
        const auto failTmp = sConfig.outputRoot / "RUN_FAIL.tmp";
        const auto failFinal = sConfig.outputRoot / "RUN_FAIL";
        {
            std::ofstream marker(failTmp, std::ios::binary | std::ios::trunc);
            marker << code << '\n';
            marker.flush();
        }
        std::error_code ec;
        std::filesystem::rename(failTmp, failFinal, ec);
    }
    Log("FAIL", {{"blocker", code}, {"message", message}});
    if (Ship::Context::GetInstance() && Ship::Context::GetInstance()->GetWindow()) {
        Ship::Context::GetInstance()->GetWindow()->Close();
    }
}

bool ParseTruthMode(const std::string& mode, int& backendValue) {
    static const std::array<std::pair<std::string_view, int>, 7> modes = {{{"NORMAL", 0},
                                                                            {"HIT_MISS", 1},
                                                                            {"WORLD_NORMAL", 2},
                                                                            {"HIT_DISTANCE", 3},
                                                                            {"INSTANCE_IDS", 4},
                                                                            {"SYNTHETIC_LAMBERT", 5},
                                                                            {"GAME_LIGHT_LAMBERT", 6}}};
    for (const auto& [name, value] : modes) {
        if (mode == name) {
            backendValue = value;
            return true;
        }
    }
    return false;
}

bool ResolveScenario() {
    try {
        const std::string manifestPath = ReadEnv("COUCH_DXR_MANIFEST");
        sConfig.outputRoot = ReadEnv("COUCH_DXR_OUTPUT");
        sConfig.scenarioId = ReadEnv("COUCH_DXR_SCENARIO");
        sConfig.truthMode = ReadEnv("COUCH_DXR_MODE");
        if (sConfig.truthMode.empty()) sConfig.truthMode = "NORMAL";
        if (manifestPath.empty() || sConfig.outputRoot.empty() || sConfig.scenarioId.empty() ||
            sConfig.truthMode.empty()) {
            Fail("HARNESS_CONFIGURATION_MISSING", "COUCH_DXR_MANIFEST, COUCH_DXR_OUTPUT and COUCH_DXR_SCENARIO are required.");
            return false;
        }
        if (!ParseTruthMode(sConfig.truthMode, sConfig.backendTruthMode)) {
            Fail("HARNESS_UNKNOWN_TRUTH_MODE", "COUCH_DXR_MODE is not one of the version 1 diagnostic modes.");
            return false;
        }
        const std::string configuredBackendMode = ReadEnv("COUCH_DXR_TRUTH_MODE");
        if (configuredBackendMode != std::to_string(sConfig.backendTruthMode)) {
            Fail("HARNESS_TRUTH_MODE_MAPPING_MISMATCH", "The backend mode does not match the requested named mode.");
            return false;
        }
        std::ifstream input(manifestPath, std::ios::binary);
        if (!input) {
            Fail("HARNESS_MANIFEST_NOT_FOUND", "The scenario manifest could not be opened.");
            return false;
        }
        input >> sConfig.manifest;
        if (sConfig.manifest.value("schemaVersion", 0) != 1) {
            Fail("HARNESS_MANIFEST_VERSION_UNSUPPORTED", "Only manifest schema version 1 is supported.");
            return false;
        }
        const auto& scenarios = sConfig.manifest.at("scenarios");
        const auto found = std::find_if(scenarios.begin(), scenarios.end(), [](const Json& item) {
            return item.value("id", "") == sConfig.scenarioId;
        });
        if (found == scenarios.end()) {
            Fail("HARNESS_SCENARIO_NOT_FOUND", "The requested scenario id is not present in the manifest.");
            return false;
        }
        sConfig.scenario = *found;
        if (sConfig.scenario.value("status", "UNMAPPED") != "MAPPED") {
            Fail("HARNESS_SCENARIO_UNMAPPED", "The requested scenario has no verified entrance/scene/room mapping.");
            return false;
        }
        sConfig.viewId = sConfig.scenario.value("viewId", "");
        const auto& views = sConfig.manifest.at("views");
        const auto foundView = std::find_if(views.begin(), views.end(), [](const Json& item) {
            return item.value("id", "") == sConfig.viewId;
        });
        if (sConfig.viewId.empty() || foundView == views.end()) {
            Fail("HARNESS_VIEW_NOT_FOUND", "The scenario references a view that is not in the manifest.");
            return false;
        }
        sConfig.view = *foundView;
        const auto& diagnosticModes = sConfig.manifest.at("diagnosticModes");
        const bool modePresent = std::any_of(diagnosticModes.begin(), diagnosticModes.end(), [](const Json& item) {
            return item.value("id", "") == sConfig.truthMode;
        });
        if (!modePresent) {
            Fail("HARNESS_UNKNOWN_TRUTH_MODE", "The selected diagnostic mode is not declared in the manifest.");
            return false;
        }
        sConfig.entrance = sConfig.scenario.at("entrance").get<int>();
        sConfig.expectedScene = sConfig.scenario.at("expectedScene").get<int>();
        sConfig.expectedRoom = sConfig.scenario.at("expectedRoom").get<int>();
        sConfig.linkAge = sConfig.scenario.at("linkAge").get<int>();
        sConfig.dayTime = sConfig.scenario.at("dayTime").get<int>();
        sConfig.nightFlag = sConfig.scenario.at("nightFlag").get<int>();
        sConfig.freezeTime = sConfig.scenario.value("freezeTime", true);
        sConfig.freezePlayer = sConfig.scenario.value("freezePlayer", true);
        sConfig.stableFramesRequired = sConfig.view.value("stableFrames", 30);
        sConfig.cameraWarmupFrames = sConfig.view.value("cameraWarmupFrames", 240);
        sConfig.timeoutFrames = sConfig.view.value("timeoutFrames", 1200);
        if (sConfig.view.value("camera", "") == "MANIFEST_FIXED_POSE") {
            const auto eye = sConfig.view.at("eye").get<std::array<float, 3>>();
            const auto at = sConfig.view.at("at").get<std::array<float, 3>>();
            const auto up = sConfig.view.at("up").get<std::array<float, 3>>();
            const float fovy = sConfig.view.at("fovy").get<float>();
            const std::array<float, 3> direction = {at[0]-eye[0], at[1]-eye[1], at[2]-eye[2]};
            const std::array<float, 3> cross = {direction[1]*up[2]-direction[2]*up[1],
                direction[2]*up[0]-direction[0]*up[2], direction[0]*up[1]-direction[1]*up[0]};
            const auto finite = [](const auto& vector) { return std::all_of(vector.begin(), vector.end(),
                [](float value) { return std::isfinite(value); }); };
            if (!finite(eye) || !finite(at) || !finite(up) || !finite(cross) || !std::isfinite(fovy) ||
                fovy < 10.0f || fovy > 120.0f || cross[0]*cross[0]+cross[1]*cross[1]+cross[2]*cross[2] < 0.0001f) {
                Fail("HARNESS_INVALID_CAMERA_POSE", "Fixed camera must have a finite, nondegenerate look direction and field of view.");
                return false;
            }
            sFrozenCameraEye = {eye[0], eye[1], eye[2]};
            sFrozenCameraAt = {at[0], at[1], at[2]};
            sFrozenCameraUp = {up[0], up[1], up[2]};
            sFrozenCameraFovy = fovy;
        }
        if (sConfig.entrance < 0 || sConfig.entrance > 0xFFFF || sConfig.expectedScene < 0 ||
            sConfig.expectedScene > 0xFF || sConfig.expectedRoom < 0 || sConfig.expectedRoom > 0x7F ||
            sConfig.linkAge < 0 || sConfig.linkAge > 1 || sConfig.dayTime < 0 || sConfig.dayTime > 0xFFFF ||
            sConfig.nightFlag < 0 || sConfig.nightFlag > 1 ||
            sConfig.stableFramesRequired <= 0 || sConfig.cameraWarmupFrames < 0 ||
            sConfig.timeoutFrames < sConfig.stableFramesRequired + sConfig.cameraWarmupFrames) {
            Fail("HARNESS_INVALID_SCENARIO_MAPPING", "Entrance, scene, room or frame limits are out of range.");
            return false;
        }
        const std::string mappingEvidenceId = sConfig.scenario.value("mappingEvidenceId", "");
        const auto& verifiedMappings = sConfig.manifest.at("verifiedMappings");
        const bool mappingVerified = std::any_of(verifiedMappings.begin(), verifiedMappings.end(), [&](const Json& item) {
            return item.value("id", "") == mappingEvidenceId && item.value("entrance", -1) == sConfig.entrance &&
                   item.value("scene", -1) == sConfig.expectedScene && item.value("room", -1) == sConfig.expectedRoom;
        });
        if (mappingEvidenceId.empty() || !mappingVerified) {
            Fail("HARNESS_INVALID_ENTRANCE", "Entrance/scene/room tuple is not present in verifiedMappings.");
            return false;
        }
        const std::string saveRoot = ReadEnv("COUCH_DXR_SAVE_ROOT");
        if (saveRoot.empty() || !std::filesystem::is_directory(saveRoot)) {
            Fail("HARNESS_SAVE_SANDBOX_MISSING", "A per-run isolated save directory is required before title boot.");
            return false;
        }
        Json resolved = sConfig.manifest;
        resolved["resolvedRun"] = {{"scenarioId", sConfig.scenarioId}, {"viewId", sConfig.viewId},
                                   {"truthMode", sConfig.truthMode},
                                   {"backendTruthMode", sConfig.backendTruthMode},
                                   {"entrance", HexValue(sConfig.entrance)},
                                   {"expectedScene", HexValue(sConfig.expectedScene)},
                                   {"expectedRoom", sConfig.expectedRoom},
                                   {"linkAge", sConfig.linkAge}, {"dayTime", sConfig.dayTime},
                                   {"nightFlag", sConfig.nightFlag},
                                   {"saveMode", "TEMP_DEBUG_SAVE"}, {"bloomOnly", "NOT_PRESENT"},
                                   {"audioOutput", "MUTED_NULL_BACKEND"},
                                   {"d3d12Validation", ReadEnv("COUCH_D3D12_DEBUG") == "1"
                                                          ? "DEBUG_LAYER_REQUESTED"
                                                          : "NOT_RUN_DEBUG_LAYER_OFF"}};
        WriteJson(sConfig.outputRoot / "manifest.resolved.json", resolved);
        return true;
    } catch (const std::exception& exception) {
        Fail("HARNESS_MANIFEST_INVALID", exception.what());
        return false;
    }
}

bool BackendDxrReady() {
    std::ifstream input(sConfig.outputRoot / "harness-dxr.log", std::ios::binary);
    if (!input) return false;
    const std::string log((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const bool initialized = log.find("DXR Acceleration Structures ENABLED") != std::string::npos;
    const auto tlas = log.find("FIRST_TLAS = PASS");
    const auto tlasEnd = tlas == std::string::npos ? std::string::npos : log.find('\n', tlas);
    const std::string tlasLine = tlas == std::string::npos ? std::string() :
                                 log.substr(tlas, tlasEnd == std::string::npos ? std::string::npos : tlasEnd - tlas);
    const auto instances = tlasLine.find("Instances=");
    const bool tlasBuilt = instances != std::string::npos && instances + 10 < tlasLine.size() &&
                           tlasLine[instances + 10] >= '1' && tlasLine[instances + 10] <= '9';
    const bool dispatchSucceeded = log.find("First DispatchRays SUCCESS") != std::string::npos;
    bool lightMatches = true;
    std::string lightSyncStatus = "NOT_REQUIRED";
    if (sConfig.scenario.value("expectDirectionalLight", false)) {
        const auto lightRecord = log.rfind("[COUCH-DXR-LIGHT-SYNC]");
        if (lightRecord == std::string::npos) {
            lightMatches = false;
            lightSyncStatus = "UNAVAILABLE";
        } else {
            const auto lineEnd = log.find('\n', lightRecord);
            const auto latestRecord = log.substr(lightRecord,
                lineEnd == std::string::npos ? std::string::npos : lineEnd - lightRecord);
            lightMatches = latestRecord.find("sameFrame=1") != std::string::npos &&
                           latestRecord.find("match=YES") != std::string::npos;
            if (lightMatches) lightSyncStatus = "PASS";
            else if (latestRecord.find("sameFrame=0") != std::string::npos)
                lightSyncStatus = "TEMPORAL_MISMATCH";
            else lightSyncStatus = "SAME_FRAME_MISMATCH";
        }
    }
    sGameLightSyncStatus = lightSyncStatus;
    if (initialized && tlasBuilt && dispatchSucceeded) {
        sDxrReady = true;
        sGameLightProductive = lightMatches;
        Log("DXR_READY", {{"initialization", "PASS"}, {"tlas", "PASS"},
                           {"dispatch_rays", "PASS"}, {"productive_game_light", lightMatches ? "PASS" : "INDEPENDENT_GATE"},
                           {"game_light_sync_status", lightSyncStatus}});
        return true;
    }
    return false;
}

std::string BackendLightSyncStatusForFrame(int captureFrame) {
    if (captureFrame < 0) return "NOT_RECORDED";
    std::ifstream input(sConfig.outputRoot / "harness-dxr.log", std::ios::binary);
    if (!input) return "NOT_RECORDED";
    const std::string expectedPrefix = "[COUCH-DXR-LIGHT-SYNC] frame=" +
                                       std::to_string(captureFrame) + " ";
    std::string line;
    std::string matchingRecord;
    while (std::getline(input, line)) {
        if (line.rfind(expectedPrefix, 0) == 0) matchingRecord = line;
    }
    if (matchingRecord.empty()) return "NOT_RECORDED";
    if (matchingRecord.find("sameFrame=0") != std::string::npos) return "TEMPORAL_MISMATCH";
    if (matchingRecord.find("sameFrame=1") != std::string::npos &&
        matchingRecord.find("match=YES") != std::string::npos) {
        return "PASS";
    }
    return "SAME_FRAME_MISMATCH";
}

bool IsD3D12BackendActive() {
    auto context = Ship::Context::GetInstance();
    if (context) {
        auto cvars = context->GetConsoleVariables();
        if (cvars && cvars->GetInteger("gEnhancements.Graphics.RendererBackend", -1) == 1) {
            return true;
        }
    }
    if (!sConfig.outputRoot.empty()) {
        std::ifstream d3d12Log(sConfig.outputRoot / "harness-d3d12.log", std::ios::binary);
        if (d3d12Log) {
            const std::string log((std::istreambuf_iterator<char>(d3d12Log)),
                                  std::istreambuf_iterator<char>());
            if (log.find("[COUCH-D3D12] Backend = D3D12") != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

// The DXR hybrid path only activates for the HYBRID_RT and
// PATH_TRACED_EXPERIMENTAL visual profiles, and it requires the
// Direct3D 12 backend. Interpreter::StartFrame applies the
// COUCH_VISUAL_PROFILE override before the first gameplay frame, so
// the effective runtime configuration is validated once, on the
// first gameplay frame.
void ValidateDxrRuntime() {
    if (sDxrRuntimeValidated) return;
    sDxrRuntimeValidated = true;

    const bool d3d12Backend = IsD3D12BackendActive();
    const int profile = Fast::MaterialRegistry::Instance().GetProfile();
    const char* profileName = Fast::MaterialRegistry::Instance().GetProfileName();
    if (!d3d12Backend) {
        Fail("HARNESS_BACKEND_MISMATCH", "The DXR hybrid path requires the Direct3D 12 backend.");
        return;
    }
    if (!Fast::MaterialRegistry::IsHybridRtProfile()) {
        Fail("HARNESS_PROFILE_MISMATCH", std::string("DXR requires the HYBRID_RT or PATH_TRACED_EXPERIMENTAL visual profile; active profile is '") +
             profileName + "' (" + std::to_string(profile) + ").");
        return;
    }
    Log("DXR_PROFILE_VALIDATED", {{"renderer_backend", "Direct3D 12"},
                                      {"renderer_backend_cvar", "gEnhancements.Graphics.RendererBackend=1"},
                                      {"visual_profile", profile},
                                      {"profile_name", profileName}});
}

void OnTitleUpdate(void* gameState) {
    if (sState != HarnessState::Booting) return;
    auto* title = static_cast<TitleContext*>(gameState);
    gSaveContext.seqId = static_cast<u8>(NA_BGM_DISABLED);
    gSaveContext.natureAmbienceId = 0xFF;
    gSaveContext.gameMode = GAMEMODE_FILE_SELECT;
    title->state.running = false;
    SET_NEXT_GAMESTATE(&title->state, FileChoose_Init, FileChooseContext);
    sState = HarnessState::SelectingSlot;
    sStateStartFrame = sGameFrame;
    Log("TEMP_DEBUG_STARTUP_REQUESTED", {{"route", "TITLE_TO_DEBUG_TEMP_SAVE"}});
}

void OnFileChooseMain(void* gameState) {
    if (sState != HarnessState::SelectingSlot) return;
    if (sGameFrame - sStateStartFrame > static_cast<std::uint64_t>(sConfig.timeoutFrames)) {
        const std::string blocker = sMainSlotInputInjected ? "HARNESS_AUTOLOAD_TIMEOUT" : "HARNESS_FILE_SELECT_TIMEOUT";
        Fail(blocker, "The automatic temporary debug-save route did not reach gameplay within the frame limit.");
        return;
    }
    auto* fileChoose = static_cast<FileChooseContext*>(gameState);
    if (fileChoose->menuMode != FS_MENU_MODE_CONFIG || fileChoose->configMode != CM_MAIN_MENU) return;
    // Route through the game's existing debug-save initializer without synthesizing input.
    fileChoose->buttonIndex = 0xFF;
    fileChoose->menuMode = FS_MENU_MODE_SELECT;
    fileChoose->selectMode = SM_LOAD_GAME;
    sMainSlotInputInjected = true;
    Log("TEMP_DEBUG_SAVE_REQUESTED", {{"slot", "TEMPORARY"}, {"flow", "FileChoose->Sram_InitDebugSave->Play_Init"}});
}

void OnLoadGame(int32_t fileNum) {
    if (sState == HarnessState::Disabled || IsHarnessTerminal(sState)) return;
    if (fileNum != 0xFF) {
        Fail("HARNESS_UNEXPECTED_SAVE_MODE", "The debug-save route did not produce the temporary harness save.");
        return;
    }
    sSaveLoaded = true;
    gSaveContext.entranceIndex = static_cast<s16>(sConfig.entrance);
    gSaveContext.linkAge = sConfig.linkAge;
    gSaveContext.dayTime = static_cast<u16>(sConfig.dayTime);
    gSaveContext.skyboxTime = static_cast<u16>(sConfig.dayTime);
    gSaveContext.nightFlag = sConfig.nightFlag;
    sTimeCaptured = false;
    sState = HarnessState::WaitingForScene;
    sStateStartFrame = sGameFrame;
    Log("WARP", {{"entrance", HexValue(sConfig.entrance)}, {"scene", HexValue(sConfig.expectedScene)},
                  {"room", sConfig.expectedRoom}, {"player_spawn", "GAME_DEFAULT_SPAWN"}});
    Log("TEMP_STATE_READY", {{"save_mode", "DEBUG_TEMP"}, {"entrance", HexValue(sConfig.entrance)},
                              {"file_num", fileNum}});
}

void OnGameStateMainStart() {
    if (sState == HarnessState::Disabled || IsHarnessTerminal(sState)) return;
    ++sGameFrame;
    if (!sSaveLoaded || !gPlayState || !sConfig.freezePlayer ||
        gPlayState->sceneNum != sConfig.expectedScene || gPlayState->roomCtx.curRoom.num != sConfig.expectedRoom) {
        return;
    }

    Player* player = GET_PLAYER(gPlayState);
    if (!player) return;
    sRawCameraStick = {gPlayState->state.input[0].cur.right_stick_x,
                       gPlayState->state.input[0].cur.right_stick_y};
    for (auto& input : gPlayState->state.input) {
        input.cur.button = 0;
        input.press.button = 0;
        input.rel.button = 0;
        input.cur.stick_x = input.cur.stick_y = 0;
        input.press.stick_x = input.press.stick_y = 0;
        input.rel.stick_x = input.rel.stick_y = 0;
        input.cur.right_stick_x = input.cur.right_stick_y = 0;
        input.press.right_stick_x = input.press.right_stick_y = 0;
        input.rel.right_stick_x = input.rel.right_stick_y = 0;
    }

    if (!sFrozenPlayerTransformCaptured) {
        sFrozenPlayerTransform = {player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z,
                                  static_cast<float>(player->actor.world.rot.y),
                                  static_cast<float>(player->actor.shape.rot.y),
                                  static_cast<float>(player->actor.shape.rot.x)};
        sFrozenPlayerFloorHeight = player->actor.floorHeight;
        const Json fixture = sConfig.scenario.value("naviPilot", Json::object());
        const Json linkPosition = fixture.value("linkPosition", Json::array());
        if (linkPosition.is_array() && linkPosition.size() == 3 &&
            std::all_of(linkPosition.begin(), linkPosition.end(), [](const Json& value) {
                return value.is_number() && std::isfinite(value.get<float>());
            })) {
            sFrozenPlayerTransform[0] = linkPosition[0].get<float>();
            sFrozenPlayerTransform[1] = linkPosition[1].get<float>();
            sFrozenPlayerTransform[2] = linkPosition[2].get<float>();
            sFrozenPlayerFloorHeight = sFrozenPlayerTransform[1];
        }
        sFrozenPlayerTransformCaptured = true;
        sPlayerFreezeRequested = true;
        const Json configuredPosition = fixture.value("linkPosition", Json::array());
        const bool authoredPose = configuredPosition.is_array() && configuredPosition.size() == 3 &&
            std::all_of(configuredPosition.begin(), configuredPosition.end(), [](const Json& value) {
                return value.is_number() && std::isfinite(value.get<float>());
            });
        Log("PLAYER_INPUT_LOCK_ACTIVE", { {"position", Json::array({sFrozenPlayerTransform[0], sFrozenPlayerTransform[1],
                                                                       sFrozenPlayerTransform[2]})},
                                             {"position_provenance", authoredPose ? "AUTHORED_HARNESS_TEST_POSE"
                                                                                  : "GAME_DEFAULT_SPAWN"} });
    }
    player->actor.world.pos.x = sFrozenPlayerTransform[0];
    player->actor.world.pos.y = sFrozenPlayerTransform[1];
    player->actor.world.pos.z = sFrozenPlayerTransform[2];
    player->actor.world.rot.y = static_cast<s16>(sFrozenPlayerTransform[3]);
    player->actor.shape.rot.y = static_cast<s16>(sFrozenPlayerTransform[4]);
    player->actor.shape.rot.x = static_cast<s16>(sFrozenPlayerTransform[5]);
    player->actor.floorHeight = sFrozenPlayerFloorHeight;
    player->actor.speedXZ = 0.0f;
    player->actor.velocity.x = 0.0f;
    player->actor.velocity.y = 0.0f;
    player->actor.velocity.z = 0.0f;
    player->linearVelocity = 0.0f;
}

void RestoreFrozenPlayerPose() {
    if (!sConfig.freezePlayer || !sFrozenPlayerTransformCaptured || !gPlayState) return;
    Player* player = GET_PLAYER(gPlayState);
    if (!player) return;
    player->actor.world.pos = {sFrozenPlayerTransform[0], sFrozenPlayerTransform[1], sFrozenPlayerTransform[2]};
    player->actor.world.rot.y = static_cast<s16>(sFrozenPlayerTransform[3]);
    player->actor.shape.rot.y = static_cast<s16>(sFrozenPlayerTransform[4]);
    player->actor.shape.rot.x = static_cast<s16>(sFrozenPlayerTransform[5]);
    player->actor.floorHeight = sFrozenPlayerFloorHeight;
    player->actor.speedXZ = player->linearVelocity = 0.0f;
    player->actor.velocity = {0, 0, 0};
}

void OnPlayDrawBegin() {
    if (!sSaveLoaded || sState == HarnessState::Disabled || sState == HarnessState::Failed || !gPlayState ||
        gPlayState->sceneNum != sConfig.expectedScene || gPlayState->roomCtx.curRoom.num != sConfig.expectedRoom) return;
    // Physics and enemy knockback run after MainStart. Honor freezePlayer again
    // before geometry is drawn, so nightly actors cannot invalidate the fixture.
    RestoreFrozenPlayerPose();
    if (CouchNaviPilotActive() && sNaviOtherFairy && sNaviOtherFairy->actor.update) {
        auto* other=sNaviOtherFairy;
        other->actor.world.pos=sNaviOtherFairyPosition;
        other->actor.scale={0.008f,0.008f,0.008f};
        other->unk_2A8=0;other->fairyFlags&=~0x28u;
        Lights_PointGlowSetInfo(&other->lightInfoGlow,sNaviOtherFairyPosition.x,sNaviOtherFairyPosition.y,
            sNaviOtherFairyPosition.z,255,255,255,100);
        Lights_PointNoGlowSetInfo(&other->lightInfoNoGlow,sNaviOtherFairyPosition.x,sNaviOtherFairyPosition.y,
            sNaviOtherFairyPosition.z,255,255,255,-1);
    }
    if (!sCameraFrozen && sState == HarnessState::Stabilizing &&
        sGameFrame - sStateStartFrame >= static_cast<std::uint64_t>(sConfig.cameraWarmupFrames)) {
        if (sConfig.view.value("camera", "") == "NAVI_RECEIVER_CLOSEUP") {
            const Player* player = GET_PLAYER(gPlayState);
            sFrozenCameraEye = {player->actor.world.pos.x+155.0f, player->actor.world.pos.y+105.0f,
                                player->actor.world.pos.z+165.0f};
            sFrozenCameraAt = {player->actor.world.pos.x+35.0f, player->actor.world.pos.y+12.0f,
                               player->actor.world.pos.z+30.0f};
            sFrozenCameraUp = {0,1,0}; sFrozenCameraFovy=60.0f;
        } else if (sConfig.view.value("camera", "") != "MANIFEST_FIXED_POSE") {
            sFrozenCameraEye = gPlayState->view.eye;
            sFrozenCameraAt = gPlayState->view.lookAt;
            sFrozenCameraUp = gPlayState->view.up;
            sFrozenCameraFovy = gPlayState->view.fovy;
        }
        sCameraFrozen = true;
        Log("CAMERA_POSE_FROZEN", {{"camera_eye", Json::array({sFrozenCameraEye.x, sFrozenCameraEye.y, sFrozenCameraEye.z})},
            {"camera_at", Json::array({sFrozenCameraAt.x, sFrozenCameraAt.y, sFrozenCameraAt.z})},
            {"pose_source", sConfig.view.value("camera", "GAME_DEFAULT_AT_SCENARIO_SPAWN")},
            {"warmup_frames", sConfig.cameraWarmupFrames}});
    }
    if (!sCameraFrozen) return;
    // Transition cameras can reactivate themselves after CAM_STAT_CUT. Pin the
    // actual View before Play_Draw constructs matrices, on every harness frame.
    if (Camera* camera = GET_ACTIVE_CAM(gPlayState)) {
        camera->eye = camera->eyeNext = sFrozenCameraEye;
        camera->at = sFrozenCameraAt;
        camera->up = sFrozenCameraUp;
        camera->fov = sFrozenCameraFovy;
    }
    func_800AA358(&gPlayState->view, &sFrozenCameraEye, &sFrozenCameraAt, &sFrozenCameraUp);
    gPlayState->view.fovy = sFrozenCameraFovy;
    View_SetDistortion(&gPlayState->view, Vec3f{0, 0, 0}, Vec3f{1, 1, 1}, 0.0f);
}

void OnPlayDrawEnd() {
    if (!sSaveLoaded || sState == HarnessState::Disabled || IsHarnessTerminal(sState) ||
        !gPlayState || !sConfig.freezePlayer ||
        gPlayState->sceneNum != sConfig.expectedScene || gPlayState->roomCtx.curRoom.num != sConfig.expectedRoom) {
        return;
    }

    Player* player = GET_PLAYER(gPlayState);
    if (!player) return;
    RestoreFrozenPlayerPose();

    // Player_DrawGameplay derives the camera target from the animated head limb. Capture and
    // pin it after world drawing, so we use the real default-spawn target and the next camera
    // update cannot follow idle breathing.
    if (!sFrozenPlayerFocusCaptured) {
        sFrozenPlayerFocusPosition = {player->actor.focus.pos.x, player->actor.focus.pos.y,
                                      player->actor.focus.pos.z};
        sFrozenPlayerFocusRotation = {player->actor.focus.rot.x, player->actor.focus.rot.y,
                                      player->actor.focus.rot.z};
        sFrozenPlayerFocusCaptured = true;
        Log("PLAYER_CAMERA_TARGET_LOCK_ACTIVE",
            {{"focus_position", Json::array({sFrozenPlayerFocusPosition[0], sFrozenPlayerFocusPosition[1],
                                              sFrozenPlayerFocusPosition[2]})}});
        return;
    }

    player->actor.focus.pos.x = sFrozenPlayerFocusPosition[0];
    player->actor.focus.pos.y = sFrozenPlayerFocusPosition[1];
    player->actor.focus.pos.z = sFrozenPlayerFocusPosition[2];
    player->actor.focus.rot.x = sFrozenPlayerFocusRotation[0];
    player->actor.focus.rot.y = sFrozenPlayerFocusRotation[1];
    player->actor.focus.rot.z = sFrozenPlayerFocusRotation[2];
}

void OnSceneInit(int16_t sceneNum) {
    if (!sSaveLoaded || IsHarnessTerminal(sState)) return;
    if (sceneNum == sConfig.expectedScene) {
        // SceneInit and TransitionEnd can both report the same successful target entry.
        // Keep the first target-scene frame as the start of one stabilization interval.
        if (sState == HarnessState::Stabilizing || sState == HarnessState::Capturing) return;
        const bool forceNightMode = (sConfig.nightFlag == 1) || sConfig.scenario.value("nightMode", false) ||
                                    (ReadEnv("COUCH_DXR_NIGHT_MODE") == "1");
        if (forceNightMode) {
            gSaveContext.dayTime = 0xC000;
            gSaveContext.skyboxTime = 0xC000;
            gSaveContext.nightFlag = 1;
        }
        if (sConfig.freezeTime && !sTimeCaptured) {
            sFrozenDayTime = gSaveContext.dayTime;
            sFrozenSkyboxTime = gSaveContext.skyboxTime;
            sTimeCaptured = true;
            Log("TIME_FREEZE_SNAPSHOT", {{"day_time", sFrozenDayTime}, {"skybox_time", sFrozenSkyboxTime}});
        }
        sState = HarnessState::Stabilizing;
        sStateStartFrame = sGameFrame;
        sStableFrameCount = 0;
        Log("SCENE_READY", {{"scene", HexValue(sceneNum)}, {"room", gPlayState ? gPlayState->roomCtx.curRoom.num : -1}});
        if (gPlayState && gPlayState->colCtx.colHeader) {
            Json waterList = Json::array();
            for (int i = 0; i < gPlayState->colCtx.colHeader->numWaterBoxes; ++i) {
                const auto& wb = gPlayState->colCtx.colHeader->waterBoxes[i];
                waterList.push_back({
                    {"idx", i}, {"xMin", wb.xMin}, {"ySurface", wb.ySurface},
                    {"zMin", wb.zMin}, {"xLength", wb.xLength}, {"zLength", wb.zLength}
                });
            }
            Log("WATER_BOXES", {{"count", gPlayState->colCtx.colHeader->numWaterBoxes}, {"boxes", waterList}});
        }
    }
}

void OnFrameUpdate() {
    if (sState == HarnessState::Disabled || IsHarnessTerminal(sState)) return;
    if (!sSaveLoaded) {
        if (sGameFrame - sStateStartFrame > static_cast<std::uint64_t>(sConfig.timeoutFrames)) {
            const std::string blocker = sMainSlotInputInjected ? "HARNESS_AUTOLOAD_TIMEOUT"
                                                               : "HARNESS_FILE_SELECT_TIMEOUT";
            Fail(blocker, "The automatic temporary debug-save route did not reach gameplay within the frame limit.");
        }
        return;
    }

    if (sConfig.freezeTime && sTimeCaptured) {
        gSaveContext.dayTime = sFrozenDayTime;
        gSaveContext.skyboxTime = sFrozenSkyboxTime;
    }
    if (!sDxrRuntimeValidated) {
        ValidateDxrRuntime();
        if (sState == HarnessState::Failed) return;
    }
    if (sState == HarnessState::Capturing) {
        const auto ackPath = sConfig.outputRoot / "capture.ack.json";
        if (std::filesystem::exists(ackPath)) {
            Json ack;
            try {
                std::ifstream ackFile(ackPath, std::ios::binary);
                ackFile >> ack;
                const std::string captureResult = ack.value("result", "");
                if ((captureResult != "CAPTURE_COMPLETE" && captureResult != "PARTIAL") ||
                    ack.value("sequence", 0) != 1) {
                    Fail("HARNESS_CAPTURE_ACK_INVALID", "The renderer did not confirm capture sequence 1.");
                    return;
                }
                const int captureFrame = ack.value("capture_frame", -1);
                const bool expectsDirectionalLight =
                    sConfig.scenario.value("expectDirectionalLight", false);
                sGameLightSyncStatus = expectsDirectionalLight
                    ? BackendLightSyncStatusForFrame(captureFrame) : "NOT_REQUIRED";
                sGameLightProductive = !expectsDirectionalLight || sGameLightSyncStatus == "PASS";
                if (captureResult == "PARTIAL") {
                    sState = HarnessState::CaptureComplete;
                    Json evidence = { {"schema_version", 2}, {"result", "PARTIAL"},
                                      {"capture_status", "PARTIAL"}, {"capture_frame", ack.value("capture_frame", -1)},
                                      {"dxr_frame", ack.value("dxr_frame", Json::object())},
                                      {"game_light_productive", sGameLightProductive},
                                      {"game_light_sync_status", sGameLightSyncStatus},
                                      {"captures", ack.value("captures", Json::object())},
                                      {"lighting_contract", BuildLightingContractEvidence()},
                                      {"reason", "One or more capture files failed content validation."} };
                    WriteJson(sConfig.outputRoot / "evidence.json", evidence);
                    {
                        const auto partialTmp = sConfig.outputRoot / "CAPTURE_PARTIAL.tmp";
                        const auto partialFinal = sConfig.outputRoot / "CAPTURE_PARTIAL";
                        std::ofstream partial(partialTmp, std::ios::binary | std::ios::trunc);
                        partial << "CAPTURE_PARTIAL\n";
                        partial.flush();
                        partial.close();
                        std::error_code ec;
                        std::filesystem::rename(partialTmp, partialFinal, ec);
                    }
                    Log("CAPTURE_PARTIAL", evidence);
                    if (Ship::Context::GetInstance() && Ship::Context::GetInstance()->GetWindow()) {
                        Ship::Context::GetInstance()->GetWindow()->Close();
                    }
                    return;
                }
                if (ack.value("capture_mask", 0) != 255 || ack.value("capture_frame", -1) < 0) {
                    Fail("HARNESS_CAPTURE_ACK_INVALID", "Capture completion requires eight valid same-frame stages.");
                    return;
                }
            } catch (...) {
                Fail("HARNESS_CAPTURE_ACK_INVALID", "The renderer acknowledgment is malformed.");
                return;
            }
            const Json dxrFrame = ack.value("dxr_frame", Json::object());
            const bool sameDxrFrame = dxrFrame.value("request_matches_capture", false);
            const std::string executionStatus = sameDxrFrame
                ? dxrFrame.value("execution_status", "NOT_RUN") : "NOT_RUN";
            const std::string compositionStatus = sameDxrFrame
                ? dxrFrame.value("composition_status", "NOT_RUN") : "NOT_RUN";
            const std::string visualStatus = sameDxrFrame
                ? dxrFrame.value("visual_status", "NOT_RUN") : "NOT_RUN";
            sState = HarnessState::CaptureComplete;
            Json evidence = {{"schema_version", 2}, {"result", "CAPTURE_COMPLETE"},
                            {"capture_status", "CAPTURE_COMPLETE"}, {"capture_frame", ack.value("capture_frame", -1)},
                            {"dxr_execution_status", executionStatus},
                            {"dxr_composition_status", compositionStatus},
                            {"dxr_visual_verified", visualStatus},
                            {"dxr_visual_reason", "Causal same-frame ablation was not run."},
                            {"presentation_status", "PRE_PRESENT_CAPTURE"},
                            {"physical_presentation_status", "NOT_RUN"},
                            {"scenario_id", sConfig.scenarioId},
                            {"view_id", sConfig.viewId}, {"truth_mode", sConfig.truthMode},
                            {"entrance", HexValue(sConfig.entrance)}, {"scene", HexValue(sConfig.expectedScene)},
                            {"room", sConfig.expectedRoom}, {"stable_frames", sStableFrameCount},
                            {"link_age", sConfig.linkAge}, {"day_time", sConfig.dayTime},
                            {"night_flag", sConfig.nightFlag},
                            {"view_hash", std::to_string(sLastViewHash)},
                            {"projection_hash", std::to_string(sLastProjectionHash)},
                            {"player_state_hash", std::to_string(sLastPlayerHash)},
                            {"player_position", Json::array({sFrozenPlayerTransform[0], sFrozenPlayerTransform[1],
                                                               sFrozenPlayerTransform[2]})},
                            {"player_yaw", sFrozenPlayerTransform[3]},
                            {"dxr_ready", sDxrReady}, {"dxr_frame", dxrFrame},
                            {"game_light_productive", sGameLightProductive},
                            {"game_light_sync_status", sGameLightSyncStatus},
                            {"player_input_lock_enabled", sPlayerFreezeRequested},
                            {"time_frozen", sConfig.freezeTime},
                            {"audio_output", "MUTED_NULL_BACKEND"},
                            {"save_sandbox_used", true}, {"temp_save", true}, {"capture_sequence", 1},
                            {"bloom_only", "NOT_PRESENT"},
                            {"d3d12_validation", ReadEnv("COUCH_D3D12_DEBUG") == "1"
                                                      ? "DEBUG_LAYER_REQUESTED"
                                                      : "NOT_RUN_DEBUG_LAYER_OFF"},
                            {"captures", ack.value("captures", Json::object())},
                            {"lighting_contract", BuildLightingContractEvidence()}};
            WriteJson(sConfig.outputRoot / "evidence.json", evidence);
            // OCZ-LAVA-01: dump this scene's draw inventory so the lava
            // classifier's exact-rule table can be filled from authentic
            // volcanic-scene evidence (HIDAN and spot17 lava textures carry
            // no semantic names, so a path token alone cannot claim them).
            Fast::MaterialRegistry::Instance().WriteDrawCallInventory(
                (sConfig.outputRoot / "draw_call_inventory.csv").string());
            {
                const auto completeTmp = sConfig.outputRoot / "CAPTURE_COMPLETE.tmp";
                const auto completeFinal = sConfig.outputRoot / "CAPTURE_COMPLETE";
                std::ofstream complete(completeTmp, std::ios::binary | std::ios::trunc);
                complete << "CAPTURE_COMPLETE\n";
                complete.flush();
                complete.close();
                std::error_code ec;
                std::filesystem::rename(completeTmp, completeFinal, ec);
            }
            Log("CAPTURE_COMPLETE", evidence);
            if (Ship::Context::GetInstance() && Ship::Context::GetInstance()->GetWindow()) {
                Ship::Context::GetInstance()->GetWindow()->Close();
            }
            return;
        }
        if (sGameFrame - sStateStartFrame > static_cast<std::uint64_t>(sConfig.timeoutFrames)) {
            Fail("HARNESS_CAPTURE_TIMEOUT", "The renderer did not acknowledge all requested capture resources in time.");
        }
        return;
    }

    if (!gPlayState) {
        if (sGameFrame - sStateStartFrame > static_cast<std::uint64_t>(sConfig.timeoutFrames)) {
            Fail(sState == HarnessState::SelectingSlot ? "HARNESS_GAMEPLAY_NOT_REACHED" : "HARNESS_AUTOLOAD_TIMEOUT",
                 "The normal title/File Select flow did not reach gameplay in the frame limit.");
        }
        return;
    }

    if (gPlayState->sceneNum != sConfig.expectedScene || gPlayState->roomCtx.curRoom.num != sConfig.expectedRoom) {
        sStableFrameCount = 0;
        if (sState == HarnessState::Stabilizing) sState = HarnessState::WaitingForScene;
        if (sGameFrame - sStateStartFrame > static_cast<std::uint64_t>(sConfig.timeoutFrames)) {
            const std::string code = gPlayState->sceneNum == sConfig.expectedScene ? "HARNESS_TARGET_ROOM_MISMATCH"
                                                                                  : "HARNESS_TARGET_SCENE_TIMEOUT";
            Fail(code, "Gameplay did not settle at the manifest scene/room before the frame limit.");
        }
        return;
    }

    if (sState != HarnessState::Stabilizing) {
        sState = HarnessState::Stabilizing;
        sStateStartFrame = sGameFrame;
        sStableFrameCount = 0;
        Log("TARGET_SCENE_ROOM_PASS", {{"scene", HexValue(gPlayState->sceneNum)},
                                       {"room", gPlayState->roomCtx.curRoom.num}});
    }

    Player* player = GET_PLAYER(gPlayState);
    if (!player) {
        sStableFrameCount = 0;
        return;
    }
    const std::uint64_t viewHash = HashBytes(&gPlayState->view.viewing, sizeof(gPlayState->view.viewing));
    const std::uint64_t projectionHash = HashBytes(&gPlayState->view.projection, sizeof(gPlayState->view.projection));
    const std::array<float, 4> playerState = {player->actor.world.pos.x, player->actor.world.pos.y,
                                               player->actor.world.pos.z,
                                               static_cast<float>(player->actor.shape.rot.y)};
    const std::uint64_t playerHash = HashBytes(playerState.data(), sizeof(playerState));
    if (sGameFrame - sLastHeartbeatFrame >= 60) {
        sLastHeartbeatFrame = sGameFrame;
        Log("STABILITY_PROGRESS", {{"scene", HexValue(gPlayState->sceneNum)},
                                    {"room", gPlayState->roomCtx.curRoom.num},
                                    {"stable_frames", sStableFrameCount},
                                    {"view_hash", std::to_string(viewHash)},
                                    {"projection_hash", std::to_string(projectionHash)},
                                    {"player_state_hash", std::to_string(playerHash)},
                                    {"camera_eye", Json::array({gPlayState->view.eye.x, gPlayState->view.eye.y,
                                                                  gPlayState->view.eye.z})},
                                    {"camera_look_at", Json::array({gPlayState->view.lookAt.x,
                                                                     gPlayState->view.lookAt.y,
                                                                     gPlayState->view.lookAt.z})},
                                    {"camera_up", Json::array({gPlayState->view.up.x, gPlayState->view.up.y,
                                                                gPlayState->view.up.z})},
                                    {"camera_fovy", gPlayState->view.fovy},
                                    {"raw_right_stick", Json::array({sRawCameraStick[0], sRawCameraStick[1]})}});
    }
    if (viewHash != 0 && projectionHash != 0 && playerHash != 0 && viewHash == sLastViewHash &&
        projectionHash == sLastProjectionHash && playerHash == sLastPlayerHash) {
        ++sStableFrameCount;
    } else {
        sStableFrameCount = 1;
        sLastViewHash = viewHash;
        sLastProjectionHash = projectionHash;
        sLastPlayerHash = playerHash;
    }
    if (sCameraFrozen && sStableFrameCount >= sConfig.stableFramesRequired) {
        if (!BackendDxrReady()) {
            if (sGameFrame - sStateStartFrame > static_cast<std::uint64_t>(sConfig.timeoutFrames)) {
                Fail("HARNESS_DXR_NOT_READY", "DXR initialization, a non-empty TLAS, first DispatchRays, or expected game-light match did not become ready before the frame limit.");
            }
            return;
        }
        sState = HarnessState::Capturing;
        sStateStartFrame = sGameFrame;
        Json request = {{"schema_version", 1}, {"sequence", 1}, {"scenario_id", sConfig.scenarioId},
                        {"view_id", sConfig.viewId}, {"truth_mode", sConfig.truthMode},
                        {"scene", HexValue(gPlayState->sceneNum)}, {"room", gPlayState->roomCtx.curRoom.num},
                        {"game_frame", sGameFrame}, {"stable_frames", sStableFrameCount},
                        {"view_hash", std::to_string(viewHash)},
                        {"projection_hash", std::to_string(projectionHash)},
                        {"player_state_hash", std::to_string(playerHash)},
                        {"lighting_contract", BuildLightingContractEvidence()},
                        {"bloom_only", "NOT_PRESENT"},
                        {"bloom_inputs", Json::array({"DXR_REFLECTION_RGBA16F", "EMISSIVE_MASK"})}};
        WriteJson(sConfig.outputRoot / "capture.request.json", request);
        Log("CAPTURE", request);
        Log("CAMERA_STABLE", request);
        Log("CAPTURE_REQUESTED", {{"sequence", 1}});
    } else if (sGameFrame - sStateStartFrame > static_cast<std::uint64_t>(sConfig.timeoutFrames)) {
        Fail("HARNESS_CAMERA_UNSTABLE", "View and projection matrices did not remain stable for the configured frames.");
    }
}

void RegisterSceneTestHarness() {
    if (ReadEnv("COUCH_DXR_HARNESS") != "1") return;
    if (ReadEnv("COUCH_DXR_CAPTURE_SEQUENCE") != "1") return;
    sState = HarnessState::Booting;
    if (!ResolveScenario()) return;
    Log("BOOT");
    Log("SCENARIO", {{"id", sConfig.scenarioId}, {"entrance", HexValue(sConfig.entrance)},
                      {"scene", HexValue(sConfig.expectedScene)}, {"room", sConfig.expectedRoom},
                      {"link_age", sConfig.linkAge}, {"day_time", sConfig.dayTime},
                      {"night_flag", sConfig.nightFlag}});
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnZTitleUpdate>(OnTitleUpdate);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnFileChooseMain>(OnFileChooseMain);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnLoadGame>(OnLoadGame);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameStateMainStart>(OnGameStateMainStart);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayDrawBegin>(OnPlayDrawBegin);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayDrawEnd>(OnPlayDrawEnd);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>(OnSceneInit);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnTransitionEnd>(OnSceneInit);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(OnFrameUpdate);
    Log("HARNESS_ENABLED", {{"entrance", HexValue(sConfig.entrance)},
                            {"expected_scene", HexValue(sConfig.expectedScene)},
                            {"expected_room", sConfig.expectedRoom},
                            {"audio_output", "MUTED_NULL_BACKEND"},
                            {"renderer_backend_env", ReadEnv("COUCH_D3D12")},
                            {"visual_profile_env", ReadEnv("COUCH_VISUAL_PROFILE")}});
}

static RegisterShipInitFunc sSceneTestHarnessInit(RegisterSceneTestHarness);
} // namespace

extern "C" void CouchSetNightMode(int enabled) {
    if (enabled) {
        gSaveContext.dayTime = 0xC000;
        gSaveContext.skyboxTime = 0xC000;
        gSaveContext.nightFlag = 1;
    }
}

// Harness-only actor fixture. It runs before the actor publishes both native
// LightInfo records; production cannot activate it through a graphics CVar.
extern "C" void CouchNaviPilotPlace(Actor* actor, PlayState* play) {
    if (!CouchNaviPilotActive() || !actor || !play || !sSaveLoaded ||
        (sState != HarnessState::Stabilizing && sState != HarnessState::Capturing) ||
        play->sceneNum != sConfig.expectedScene || !sCameraFrozen) return;
    auto* fairy=reinterpret_cast<EnElf*>(actor);
    static bool captured=false;
    static Vec3f frozen{}, scale{};
    static Color_RGBAf inner{},outer{};
    static std::array<Vec3s,15> joints{};
    static uint16_t timer=0, flags=0;
    static int16_t state=0;
    const Json fixture=sConfig.scenario.value("naviPilot",Json::object());
    const Json configuredPositionOffset=fixture.value("positionOffset",Json::array());
    const bool authoredPositionOffset=configuredPositionOffset.is_array() && configuredPositionOffset.size()==3 &&
        std::all_of(configuredPositionOffset.begin(),configuredPositionOffset.end(),[](const Json& value){
            return value.is_number() && std::isfinite(value.get<float>());
        });
    if (!captured) {
        const Vec3f natural=actor->world.pos;
        frozen=natural; scale=actor->scale; inner=fairy->innerColor; outer=fairy->outerColor;
        timer=fairy->timer; flags=fairy->fairyFlags; state=fairy->unk_2A8;
        std::copy(std::begin(fairy->jointTable),std::end(fairy->jointTable),joints.begin());
        const Player* player=GET_PLAYER(play);
        const std::string poseKind=fixture.value("pose",std::string("NATURAL"));
        const bool controlled=poseKind=="CONTROLLED";
        const bool naturalActive=poseKind=="NATURAL_ACTIVE";
        if (controlled && player) {
            const float fixtureScale=fixture.value("emitterScale",1.0f)*0.008f;
            scale={fixtureScale,fixtureScale,fixtureScale}; // explicit visible test pose
            if (authoredPositionOffset) {
                frozen={player->actor.world.pos.x+configuredPositionOffset[0].get<float>(),
                    player->actor.floorHeight+configuredPositionOffset[1].get<float>(),
                    player->actor.world.pos.z+configuredPositionOffset[2].get<float>()};
            } else {
                const float height=fixture.value("height",25.0f);
                frozen={player->actor.world.pos.x+35.0f,player->actor.floorHeight+height,
                         player->actor.world.pos.z+30.0f};
            }
        }
        if (controlled || naturalActive) {
            state=fixture.value("glowEnabled",true)?0:8;
            if (controlled)
                flags=uint16_t((flags & ~0x28u) | (fixture.value("auxiliaryEnabled",false)?0x20u:0u));
        }
        if (controlled) {
            if (fixture.value("color","")=="RED") { inner={255.0f,40.0f,40.0f,255.0f};outer={255.0f,40.0f,40.0f,255.0f}; }
            if (fixture.value("color","")=="BLUE" || fixture.value("color","")=="ELECTRIC_BLUE") { inner={160.0f,230.0f,255.0f,255.0f};outer={0.0f,150.0f,255.0f,255.0f}; }
            if (fixture.value("secondFairy",false)) {
                sNaviOtherFairyPosition={frozen.x-50.0f,frozen.y,frozen.z+20.0f};
                sNaviOtherFairy=reinterpret_cast<EnElf*>(Actor_Spawn(&play->actorCtx,play,ACTOR_EN_ELF,
                    sNaviOtherFairyPosition.x,sNaviOtherFairyPosition.y,sNaviOtherFairyPosition.z,
                    0,0,0,FAIRY_KOKIRI,true));
                if (sNaviOtherFairy) sNaviOtherFairy->actor.parent=&GET_PLAYER(play)->actor;
            }
        }
        const char* poseLabel=controlled?"CONTROLLED_TEST_POSE":
                              naturalActive?"NATURAL_POSITION_ACTIVE_LIGHT_TEST":"NATURAL_FROZEN";
        const float pointLightGain=fixture.value("pointLightGain",1.0f);
        const Json pointLightTint=fixture.value("pointLightTint",Json::array({1.0f,1.0f,1.0f}));
        const bool authoredPointColor=pointLightGain!=1.0f ||
            pointLightTint!=Json::array({1.0f,1.0f,1.0f});
        const float visibleGeometryScale=fixture.value("emitterScale",1.0f);
        const Json configuredLinkPosition=fixture.value("linkPosition",Json::array());
        const bool authoredLinkPosition=configuredLinkPosition.is_array() && configuredLinkPosition.size()==3 &&
            std::all_of(configuredLinkPosition.begin(),configuredLinkPosition.end(),[](const Json& value){
                return value.is_number() && std::isfinite(value.get<float>());
            });
        Json pose={{"pose",poseLabel},
            {"naturalPosition",{natural.x,natural.y,natural.z}},
            {"testPosition",{frozen.x,frozen.y,frozen.z}},
            {"linkPosition",player?Json::array({player->actor.world.pos.x,player->actor.world.pos.y,
                                                  player->actor.world.pos.z}):Json(nullptr)},
            {"linkPositionProvenance",authoredLinkPosition?"AUTHORED_HARNESS_TEST_POSE":"GAME_DEFAULT_SPAWN"},
            {"receiverAnchor",fixture.value("receiverAnchor",Json::object())},
            {"state",state},{"flags",flags},{"ownerId",CouchGetLightOwnerId(&fairy->lightInfoGlow)},
            {"emitterScale",visibleGeometryScale},
            {"visibleGeometryScale",visibleGeometryScale},
            {"positionOffset",authoredPositionOffset?configuredPositionOffset:Json::array({35.0f,fixture.value("height",25.0f),30.0f})},
            {"positionOffsetProvenance",controlled?"AUTHORED_HARNESS_TEST_POSE":"NATIVE_ACTOR_POSITION"},
            {"pointLightGain",pointLightGain},
            {"pointLightTint",pointLightTint},
            {"pointLightGainProvenance",authoredPointColor?"AUTHORED_HARNESS_ONLY":"NATIVE"},
            {"pointLightTintProvenance",authoredPointColor?"AUTHORED_HARNESS_ONLY":"NATIVE"},
            {"secondFairySpawned",sNaviOtherFairy!=nullptr},
            {"playerFloorHeight",player?Json(player->actor.floorHeight):Json(nullptr)},
            {"receiver","must be identified from GPU intersection; no synthetic floor"}};
        std::ofstream out(sConfig.outputRoot/"navi.pose.json", std::ios::binary | std::ios::trunc);
        if (out) {
            const std::string text = pose.dump(2);
            out.write(text.data(), text.size());
            out.put('\n');
            out.flush();
            out.close();
        }
        Log("NAVI_POSE_FROZEN",pose);
        captured=true;
    }
    actor->world.pos=frozen; actor->scale=scale; actor->velocity={0,0,0}; actor->speedXZ=0;
    fairy->innerColor=inner;fairy->outerColor=outer;
    fairy->disappearTimer=0;
    fairy->timer=++timer;
    fairy->fairyFlags=flags;fairy->unk_2A8=state;
    std::copy(joints.begin(),joints.end(),std::begin(fairy->jointTable));
}
