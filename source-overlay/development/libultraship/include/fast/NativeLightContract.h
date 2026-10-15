#pragma once
#include <stdint.h>

/* Renderer metadata only. Never changes the game's LightInfo/Actor ABI. */
enum CouchLightRole { COUCH_LIGHT_NATIVE = 0, COUCH_LIGHT_NAVI_GLOW = 1,
                      COUCH_LIGHT_NAVI_AUX = 2, COUCH_LIGHT_FAIRY_GLOW = 3,
                      COUCH_LIGHT_FAIRY_AUX = 4 };
#define COUCH_G_SET_LIGHT_OWNER 0x46

#ifdef __cplusplus
extern "C" {
#endif
uint32_t CouchRegisterLightOwner(const void* light, const void* owner, int actorId, int actorParams, int role);
uint32_t CouchGetLightOwnerId(const void* light);
void CouchForgetNativeLight(const void* light);
void CouchCaptureIdentifiedNativeLight(const void* light, int type, float x, float y, float z,
    uint32_t gameFrame, float red, float green, float blue, float rawRadius);
int CouchNaviPilotActive(void);
#ifdef __cplusplus
}

#include <cmath>
namespace Fast {
struct NativeLightRecord {
    uint64_t sourceId = 0;
    uint32_t ownerId = 0;
    int actorId = -1, actorParams = -1, role = COUCH_LIGHT_NATIVE, type = 0;
    float position[3] = {}, color[3] = {}, rawRadius = 0;
    bool enabled = false;
};
inline bool NativePointEnabled(int role, float radius) {
    // A point light requires a positive finite radius. Navi uses -1 as its
    // inactive auxiliary sentinel, and non-positive radii cannot illuminate geometry.
    if (!std::isfinite(radius) || radius <= 0.0f) return false;
    return true;
}
inline float NativePointAttenuation(float distance, float radius) {
    if (!std::isfinite(distance) || !std::isfinite(radius) || distance < 0 || radius <= 0 || distance >= radius) return 0;
    const float d = distance / radius;
    return 1 - d * d;
}
}
#endif
