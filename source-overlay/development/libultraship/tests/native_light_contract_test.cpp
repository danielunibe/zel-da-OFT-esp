#include "fast/NativeLightContract.h"
#include "fast/MaterialRegistry.h"
#include <cassert>
#include <limits>
#include <cmath>
#include <iostream>

int main() {
    using namespace Fast;
    assert(NativePointEnabled(COUCH_LIGHT_NAVI_GLOW,100));
    assert(!NativePointEnabled(COUCH_LIGHT_NAVI_GLOW,0));
    assert(!NativePointEnabled(COUCH_LIGHT_NAVI_AUX,-1));
    assert(NativePointEnabled(COUCH_LIGHT_NAVI_AUX,200));
    assert(!NativePointEnabled(COUCH_LIGHT_FAIRY_AUX,-1));
    assert(!NativePointEnabled(COUCH_LIGHT_NAVI_GLOW,std::numeric_limits<float>::infinity()));
    assert(!NativePointEnabled(COUCH_LIGHT_NAVI_GLOW,std::numeric_limits<float>::quiet_NaN()));
    assert(NativePointAttenuation(0,100)==1);
    assert(NativePointAttenuation(50,100)==0.75f);
    assert(NativePointAttenuation(100,100)==0);
    assert(NativePointAttenuation(125,100)==0);
    assert(NativePointAttenuation(0,0)==0);
    assert(NativePointAttenuation(-1,100)==0);
    assert(NativePointAttenuation(std::numeric_limits<float>::quiet_NaN(),100)==0);
    float previous=1;
    for(int d=0;d<=100;++d) {
        const float value=NativePointAttenuation(float(d),100);
        assert(value>=0 && value<=1 && value<=previous);
        previous=value;
    }
    // The packed owner/role cannot identify another fairy or the auxiliary lamp.
    for(uint32_t owner: {1u,17u,65535u}) {
        for(uint32_t role=1;role<=4;++role) {
            const float gpu=float(owner*8u+role);
            assert(uint32_t(gpu)/8u==owner && uint32_t(gpu)%8u==role);
        }
        const uint32_t sampler=(owner<<16u)|1023u;
        assert((sampler>>16u)==owner && (sampler&1023u)==1023u);
    }
    // Test DxrNativeLightReceiverEligible logic
    assert(DxrNativeLightReceiverEligible(MaterialType::STONE, 0, 0, 0, 0, 0.0f));
    assert(DxrNativeLightReceiverEligible(MaterialType::WOOD, 0, 0, 42, 0, 0.0f)); // Actor prop receiver
    assert(DxrNativeLightReceiverEligible(MaterialType::CHARACTER, 0, 0, 10, 0, 0.0f)); // Actor character receiver
    assert(!DxrNativeLightReceiverEligible(MaterialType::STONE, 2, 0, 0, 0, 0.0f)); // Translucent excluded
    assert(!DxrNativeLightReceiverEligible(MaterialType::STONE, 0, 1u << 11, 0, 0, 0.0f)); // Beam excluded
    assert(!DxrNativeLightReceiverEligible(MaterialType::STONE, 0, 0, 1, COUCH_LIGHT_NAVI_GLOW, 0.0f)); // Emitter excluded
    assert(!DxrNativeLightReceiverEligible(MaterialType::STONE, 0, 0, 1, 0, 1.5f)); // Emissive material excluded
    assert(!DxrNativeLightReceiverEligible(MaterialType::WATER, 0, 0, 0, 0, 0.0f)); // Water excluded
    assert(!DxrNativeLightReceiverEligible(MaterialType::GLASS, 0, 0, 0, 0, 0.0f)); // Glass excluded

    std::cout << "PASS native activation, finite guards, attenuation, owner/role, sampler packing and receiver eligibility\n";
}
