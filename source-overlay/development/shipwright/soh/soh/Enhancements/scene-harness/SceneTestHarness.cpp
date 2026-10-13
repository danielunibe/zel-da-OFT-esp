#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "functions.h"

extern "C" {
#include "z64.h"
#include "overlays/gamestates/ovl_file_choose/file_choose.h"
}

#include <nlohmann/json.hpp>

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

enum class HarnessState { Disabled, Booting, SelectingSlot, WaitingForScene, Stabilizing, Capturing, Passed, Failed };

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
bool sGameLightProductive = false;
std::string sGameLightSyncStatus = "UNKNOWN";
bool sPlayerFreezeRequested = false;
bool sTimeCaptured = false;
bool sMainSlotInputInjected = false;
bool sConfirmInputInjected = false;
bool sFrozenPlayerTransformCaptured = false;
std::array<float, 6> sFrozenPlayerTransform{};
bool sFrozenPlayerFocusCaptured = false;
bool sCameraFrozen = false;
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

void WriteJson(const std::filesystem::path& path, const Json& data) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << std::setw(2) << data << '\n';
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
    if (sState == HarnessState::Failed || sState == HarnessState::Passed) return;
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
        std::ofstream marker(sConfig.outputRoot / "RUN_FAIL", std::ios::binary | std::ios::trunc);
        marker << code << '\n';
    }
    Log("FAIL", {{"blocker", code}, {"message", message}});
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
    if (sState == HarnessState::Disabled || sState == HarnessState::Failed || sState == HarnessState::Passed) return;
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
    if (sState == HarnessState::Disabled || sState == HarnessState::Failed || sState == HarnessState::Passed) return;
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
        sFrozenPlayerTransformCaptured = true;
        sPlayerFreezeRequested = true;
        Log("PLAYER_INPUT_LOCK_ACTIVE", { {"position", Json::array({sFrozenPlayerTransform[0], sFrozenPlayerTransform[1],
                                                                       sFrozenPlayerTransform[2]})} });
    } else {
        player->actor.world.pos.x = sFrozenPlayerTransform[0];
        player->actor.world.pos.y = sFrozenPlayerTransform[1];
        player->actor.world.pos.z = sFrozenPlayerTransform[2];
        player->actor.world.rot.y = static_cast<s16>(sFrozenPlayerTransform[3]);
        player->actor.shape.rot.y = static_cast<s16>(sFrozenPlayerTransform[4]);
        player->actor.shape.rot.x = static_cast<s16>(sFrozenPlayerTransform[5]);
        player->actor.speedXZ = 0.0f;
        player->actor.velocity.x = 0.0f;
        player->actor.velocity.y = 0.0f;
        player->actor.velocity.z = 0.0f;
        player->linearVelocity = 0.0f;
    }
}

void OnPlayDrawEnd() {
    if (!sSaveLoaded || sState == HarnessState::Disabled || sState == HarnessState::Failed ||
        sState == HarnessState::Passed || !gPlayState || !sConfig.freezePlayer ||
        gPlayState->sceneNum != sConfig.expectedScene || gPlayState->roomCtx.curRoom.num != sConfig.expectedRoom) {
        return;
    }

    Player* player = GET_PLAYER(gPlayState);
    if (!player) return;

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
    if (!sSaveLoaded || sState == HarnessState::Failed || sState == HarnessState::Passed) return;
    if (sceneNum == sConfig.expectedScene) {
        // SceneInit and TransitionEnd can both report the same successful target entry.
        // Keep the first target-scene frame as the start of one stabilization interval.
        if (sState == HarnessState::Stabilizing || sState == HarnessState::Capturing) return;
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
    }
}

void OnFrameUpdate() {
    if (sState == HarnessState::Disabled || sState == HarnessState::Failed || sState == HarnessState::Passed) return;
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
    if (sState == HarnessState::Capturing) {
        const auto ackPath = sConfig.outputRoot / "capture.ack.json";
        if (std::filesystem::exists(ackPath)) {
            Json ack;
            try {
                std::ifstream ackFile(ackPath, std::ios::binary);
                ackFile >> ack;
                if (ack.value("result", "") != "PASS" || ack.value("sequence", 0) != 1) {
                    Fail("HARNESS_CAPTURE_ACK_INVALID", "The renderer did not confirm capture sequence 1.");
                    return;
                }
            } catch (...) {
                Fail("HARNESS_CAPTURE_ACK_INVALID", "The renderer acknowledgment is malformed.");
                return;
            }
            sState = HarnessState::Passed;
            Json evidence = {{"schema_version", 1}, {"result", "PASS"}, {"scenario_id", sConfig.scenarioId},
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
                            {"dxr_ready", sDxrReady},
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
                            {"captures", ack.value("captures", Json::object())}};
            WriteJson(sConfig.outputRoot / "evidence.json", evidence);
            std::ofstream pass(sConfig.outputRoot / "RUN_PASS", std::ios::binary | std::ios::trunc);
            pass << "DXR_SCENARIO_HARNESS=PASS\n";
            Log("DONE", evidence);
            Log("RUN_PASS", evidence);
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

    // Let the manifest's real spawn camera run through its initial easing, then cut camera
    // updates in harness runs only. This preserves the actual spawn view and prevents the
    // engine's long-tail subpixel camera easing from making matrix hashes perpetually unstable.
    if (!sCameraFrozen && sGameFrame - sStateStartFrame >= static_cast<std::uint64_t>(sConfig.cameraWarmupFrames)) {
        Camera* camera = GET_ACTIVE_CAM(gPlayState);
        if (camera && camera->status != CAM_STAT_CUT) {
            camera->status = CAM_STAT_CUT;
            sCameraFrozen = true;
            Log("CAMERA_POSE_FROZEN", {{"camera_eye", Json::array({camera->eye.x, camera->eye.y, camera->eye.z})},
                                       {"camera_at", Json::array({camera->at.x, camera->at.y, camera->at.z})},
                                       {"camera_status", camera->status},
                                       {"warmup_frames", sConfig.cameraWarmupFrames}});
        }
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
    if (sStableFrameCount >= sConfig.stableFramesRequired) {
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
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayDrawEnd>(OnPlayDrawEnd);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>(OnSceneInit);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnTransitionEnd>(OnSceneInit);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(OnFrameUpdate);
    Log("HARNESS_ENABLED", {{"entrance", HexValue(sConfig.entrance)},
                            {"expected_scene", HexValue(sConfig.expectedScene)},
                            {"expected_room", sConfig.expectedRoom},
                            {"audio_output", "MUTED_NULL_BACKEND"}});
}

static RegisterShipInitFunc sSceneTestHarnessInit(RegisterSceneTestHarness);
} // namespace
