#include "Parameters.h"

#include "hadronyche/vst/ParamState.h"

#include <cmath>

using namespace Steinberg;

namespace LoopTexturer {

namespace {

// Version 2 adds the looper's audio after the parameters. Version 3 extends the looper's length from
// 10 s to 30 s, which changes what its normalized value means.
constexpr int32 kStateVersion = 3;
constexpr double kVersion2MaxLength = 10.0;

// The order of these lists must match the enums in src/dsp.
const char* const kLfoWaves[] = { "Sine", "Triangle", "Square", "Sample & hold" };
const char* const kFilterModes[] = { "Low-pass", "High-pass", "Band-pass", "Notch" };
const char* const kSatModes[] = { "Hard clip", "Soft saturator", "Sine clip", "Tube", "Wavefolder", "Rectifier" };
const char* const kLooperModes[] = { "Free running", "Tempo sync" };
const char* const kLooperBarChoices[] = { "1/2 bar", "1 bar", "2 bars", "4 bars", "6 bars", "8 bars", "Custom", "Open" };

constexpr int kLfoWaveCount = 4;
constexpr int kFilterModeCount = 4;
constexpr int kSatModeCount = 6;
constexpr int kLooperModeCount = 2;
constexpr int kLooperBarChoiceCount = 8;

// Listed in ParamId order.
const std::array<ParamSpec, kParamCount> kSpecs { {
    { "Gain", "dB", -24.0, 24.0, 0.0, Curve::Linear, 1, nullptr, 0 },

    { "Pan", "%", -100.0, 100.0, 0.0, Curve::Linear, 0, nullptr, 0 },
    { "Pan LFO rate", "Hz", 0.1, 10.0, 1.0, Curve::Log, 2, nullptr, 0 },
    { "Pan LFO amount", "%", 0.0, 100.0, 0.0, Curve::Linear, 0, nullptr, 0 },
    { "Pan LFO wave", "", 0.0, kLfoWaveCount - 1, 0.0, Curve::List, 0, kLfoWaves, kLfoWaveCount },

    { "Filter mode", "", 0.0, kFilterModeCount - 1, 0.0, Curve::List, 0, kFilterModes, kFilterModeCount },
    { "Filter freq", "Hz", 20.0, 20000.0, 20000.0, Curve::Log, 0, nullptr, 0 },
    { "Filter resonance", "%", 0.0, 100.0, 0.0, Curve::Linear, 0, nullptr, 0 },
    { "Filter LFO rate", "Hz", 0.1, 10.0, 1.0, Curve::Log, 2, nullptr, 0 },
    { "Filter LFO amount", "oct", 0.0, 4.0, 0.0, Curve::Linear, 2, nullptr, 0 },
    { "Filter LFO wave", "", 0.0, kLfoWaveCount - 1, 0.0, Curve::List, 0, kLfoWaves, kLfoWaveCount },

    { "Saturator on", "", 0.0, 1.0, 1.0, Curve::Toggle, 0, nullptr, 0 },
    { "Saturator mode", "", 0.0, kSatModeCount - 1, 1.0, Curve::List, 0, kSatModes, kSatModeCount },
    { "Saturator drive", "dB", 0.0, 36.0, 0.0, Curve::Linear, 1, nullptr, 0 },
    { "Saturator mix", "%", 0.0, 100.0, 100.0, Curve::Linear, 0, nullptr, 0 },

    { "Post sat on", "", 0.0, 1.0, 0.0, Curve::Toggle, 0, nullptr, 0 },
    { "Post sat mode", "", 0.0, kSatModeCount - 1, 1.0, Curve::List, 0, kSatModes, kSatModeCount },
    { "Post sat drive", "dB", 0.0, 36.0, 0.0, Curve::Linear, 1, nullptr, 0 },
    { "Post sat mix", "%", 0.0, 100.0, 100.0, Curve::Linear, 0, nullptr, 0 },

    { "Volume", "dB", kVolumeSilenceDb, 6.0, 0.0, Curve::Linear, 1, nullptr, 0, true },

    // Changing the mode clears the loop, which automation shouldn't be able to do.
    { "Looper mode", "", 0.0, kLooperModeCount - 1, 0.0, Curve::List, 0, kLooperModes, kLooperModeCount, false, false },
    { "Looper length", "s", 0.5, 30.0, 2.0, Curve::Log, 2, nullptr, 0 },
    { "Looper bars", "", 0.0, kLooperBarChoiceCount - 1, 1.0, Curve::List, 0, kLooperBarChoices, kLooperBarChoiceCount },
    { "Looper custom bars", "bars", 1.0, 64.0, 16.0, Curve::Integer, 0, nullptr, 0 },
    { "Looper feedback", "%", 0.0, 200.0, 100.0, Curve::Linear, 0, nullptr, 0 },
    { "Looper dry/wet", "%", 0.0, 100.0, 50.0, Curve::Linear, 0, nullptr, 0 },
} };

} // namespace

const ParamSpec& getParamSpec (Vst::ParamID id)
{
    return kSpecs[id];
}

bool writeParamState (IBStream* stream, const ParamValues& normalizedValues)
{
    return Hadronyche::writeParamState (stream, kStateVersion, normalizedValues.data (), kParamCount);
}

bool readParamState (IBStream* stream, const std::function<void (Vst::ParamID, double)>& onValue, int* version)
{
    const auto convertOldValues = [&onValue] (Vst::ParamID id, double value, int32 savedVersion) {
        if (savedVersion < 3 && id == kLooperFreeLength)
        {
            const auto& spec = getParamSpec (kLooperFreeLength);
            value = plainToNormalized (spec, spec.min * std::pow (kVersion2MaxLength / spec.min, value));
        }
        onValue (id, value);
    };
    return Hadronyche::readParamState (stream, kStateVersion, kParamCount, convertOldValues, version);
}

} // namespace LoopTexturer
