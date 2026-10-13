#include "ship/audio/Audio.h"

#include "ship/Context.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include <cstdlib>
#include <cstring>
#include <spdlog/spdlog.h>

namespace Ship {
namespace {
bool IsSceneHarnessActive() {
    const char* value = std::getenv("COUCH_DXR_HARNESS");
    return value && std::strcmp(value, "1") == 0;
}
} // namespace

Audio::~Audio() {
    SPDLOG_TRACE("destruct audio");
}

void Audio::InitAudioPlayer() {
    if (IsSceneHarnessActive()) {
        mAudioPlayer = std::make_shared<NullAudioPlayer>(this->mAudioSettings);
        if (mAudioPlayer->Init()) {
            SPDLOG_INFO("[SCENE-HARNESS] Audio output suppressed with NullAudioPlayer");
        }
        return;
    }

    switch (GetCurrentAudioBackend()) {
#ifdef _WIN32
        case AudioBackend::WASAPI:
            mAudioPlayer = std::make_shared<WasapiAudioPlayer>(this->mAudioSettings);
            break;
#endif
        case AudioBackend::SDL:
            mAudioPlayer = std::make_shared<SDLAudioPlayer>(this->mAudioSettings);
            break;
        default:
            mAudioPlayer = std::make_shared<NullAudioPlayer>(this->mAudioSettings);
            break;
    }

    if (mAudioPlayer && !mAudioPlayer->Init()) {
        // Failed to initialize system audio player.
        // Fallback to Null if the native system player does not work.
        SetCurrentAudioBackend(AudioBackend::NUL);
    }
}

void Audio::Init() {
    mAvailableAudioBackends = std::make_shared<std::vector<AudioBackend>>();
#ifdef _WIN32
    mAvailableAudioBackends->push_back(AudioBackend::WASAPI);
#endif
    mAvailableAudioBackends->push_back(AudioBackend::SDL);
    mAvailableAudioBackends->push_back(AudioBackend::NUL);

    if (IsSceneHarnessActive()) {
        // Do not load, migrate, or save the user's audio backend for a harness run.
        mAudioBackend = AudioBackend::NUL;
        InitAudioPlayer();
        return;
    }

    SetCurrentAudioBackend(Context::GetInstance()->GetConfig()->GetCurrentAudioBackend());
}

std::shared_ptr<AudioPlayer> Audio::GetAudioPlayer() {
    return mAudioPlayer;
}

AudioBackend Audio::GetCurrentAudioBackend() {
    return mAudioBackend;
}

void Audio::SetCurrentAudioBackend(AudioBackend backend) {
    if (IsSceneHarnessActive()) {
        mAudioBackend = AudioBackend::NUL;
    } else {
        mAudioBackend = backend;
        Context::GetInstance()->GetConfig()->SetCurrentAudioBackend(GetCurrentAudioBackend());
        Context::GetInstance()->GetConfig()->Save();
    }

    InitAudioPlayer();
}

std::shared_ptr<std::vector<AudioBackend>> Audio::GetAvailableAudioBackends() {
    return mAvailableAudioBackends;
}

} // namespace Ship
