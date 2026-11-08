#pragma once

// Generated from the parameter table in rust/, by cargo test there. Don't edit it here: change the table,
// and run HADRONYCHE_UPDATE_HEADERS=1 cargo test in rust/ to bring this up to date.

#include "pluginterfaces/vst/vsttypes.h"

namespace LoopTexturer {

// Parameter IDs are saved in projects and automation: never renumber them, only append.
enum ParamId : Steinberg::Vst::ParamID
{
    kGain,
    kPan,
    kPanLfoRate,
    kPanLfoAmount,
    kPanLfoWave,
    kFilterMode,
    kFilterFreq,
    kFilterResonance,
    kFilterLfoRate,
    kFilterLfoAmount,
    kFilterLfoWave,
    kSatOn,
    kSatMode,
    kSatDrive,
    kSatMix,
    kPostSatOn,
    kPostSatMode,
    kPostSatDrive,
    kPostSatMix,
    kVolume,
    kLooperMode,
    kLooperFreeLength,
    kLooperBars,
    kLooperCustomBars,
    kLooperFeedback,
    kLooperDryWet,
    kParamCount
};

} // namespace LoopTexturer
