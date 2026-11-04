#pragma once

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

enum class Curve
{
    Linear,
    Log,
    List,
    Toggle,
    Integer
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

struct ParamSpec
{
    const char* title;
    const char* units;
    double min;
    double max;
    double defaultValue; // In plain units, like min and max.
    Curve curve;
    int precision;
    const char* const* labels; // Curve::List only.
    int labelCount;
};

const ParamSpec& getParamSpec (Steinberg::Vst::ParamID id);

// Conversions between the host's 0..1 values and the units in ParamSpec.
double normalizedToPlain (const ParamSpec& spec, double normalized);
double plainToNormalized (const ParamSpec& spec, double plain);

// At or below this, kVolume is silent.
constexpr double kVolumeSilenceDb = -60.0;

using ParamValues = std::array<double, kParamCount>;

// Parameters are saved as ID and value pairs, so that newer versions can add parameters and still
// read older projects.
bool writeParamState (Steinberg::IBStream* stream, const ParamValues& normalizedValues);

// Calls onValue (id, normalized) for every known parameter in the stream. On return, the stream is
// positioned at whatever was saved after the parameters.
bool readParamState (Steinberg::IBStream* stream, const std::function<void (Steinberg::Vst::ParamID, double)>& onValue,
                     int* version = nullptr);

} // namespace LoopTexturer
