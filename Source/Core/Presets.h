#pragma once
// VOCAL TUNE STUDIO - プリセット(パラメーター値の表。単位はUIと同じ: %, 半音, 秒)
#include <vector>

namespace vts {

struct Preset {
    const char* name;
    float center, drift, modulation, transition;   // 0..100 %
    float formantSemis;                            // -12..12
    float vibDepthPct, vibRatePct, vibOnsetSec, vibAmountPct, vibSmoothPct;
};

inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> p = {
        //  name                          ctr  drift mod  trans  fmt   vDep  vRate vOns  vAmt vSmooth
        { "Natural (Default)",            100,   0,    0,   50,   0.f,  100,  100,  0.f,  100,  0 },
        { "Gentle Correction",             70,  50,    0,   70,   0.f,  100,  100,  0.f,  100,  0 },
        { "Hard Correction",              100, 100,  100,    0,   0.f,  100,  100,  0.f,    0,  0 },
        { "Fix Pitch, Keep Vibrato",      100, 100,    0,   50,   0.f,  100,  100,  0.f,  100,  0 },
        { "Subtle Vibrato",               100,  50,    0,   50,   0.f,   50,  100,  0.f,  100, 30 },
        { "Drift Tamer (Centers Kept)",     0, 100,    0,   50,   0.f,  100,  100,  0.f,  100,  0 },
        { "Warm Formant (-1 st)",         100,   0,    0,   50,  -1.f,  100,  100,  0.f,  100,  0 },
        { "Bright Formant (+1 st)",       100,   0,    0,   50,   1.f,  100,  100,  0.f,  100,  0 },
    };
    return p;
}

} // namespace vts
