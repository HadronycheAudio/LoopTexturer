#pragma once

#include "hadronyche/dsp/Gain.h"
#include "hadronyche/vst/ParamSpec.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <array>
#include <functional>

namespace LoopTexturer {

// Parameter IDs are saved in Cubase projects and automation: never renumber them, only append.
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

// The entries of kLooperBars, in order.
enum LooperBarChoice
{
    kBarsHalf,
    kBarsOne,
    kBarsTwo,
    kBarsFour,
    kBarsSix,
    kBarsEight,
    kBarsCustom,
    kBarsOpen
};

using Hadronyche::Curve;
using Hadronyche::ParamSpec;
using Hadronyche::kVolumeSilenceDb;
using Hadronyche::normalizedToPlain;
using Hadronyche::plainToNormalized;

const ParamSpec& getParamSpec (Steinberg::Vst::ParamID id);

using ParamValues = std::array<double, kParamCount>;

bool writeParamState (Steinberg::IBStream* stream, const ParamValues& normalizedValues);

// Calls onValue (id, normalized) for every known parameter in the stream. On return, the stream is
// positioned at whatever was saved after the parameters.
bool readParamState (Steinberg::IBStream* stream, const std::function<void (Steinberg::Vst::ParamID, double)>& onValue,
                     int* version = nullptr);

} // namespace LoopTexturer
