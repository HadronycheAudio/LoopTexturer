#include "Parameters.h"

#include "base/source/fstreamer.h"

#include <algorithm>
#include <cmath>

using namespace Steinberg;

namespace LoopTexturer {

namespace {

constexpr int32 kStateVersion = 1;

// The order of these lists must match the enums in src/dsp.
const char* const kLfoWaves[] = { "Sine", "Triangle", "Square", "Sample & hold" };
const char* const kFilterModes[] = { "Low-pass", "High-pass", "Band-pass", "Notch" };
const char* const kSatModes[] = { "Hard clip", "Soft saturator", "Sine clip", "Tube", "Wavefolder", "Rectifier" };

constexpr int kLfoWaveCount = 4;
constexpr int kFilterModeCount = 4;
constexpr int kSatModeCount = 6;

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

    { "Volume", "dB", kVolumeSilenceDb, 6.0, 0.0, Curve::Linear, 1, nullptr, 0 },
} };

} // namespace

const ParamSpec& getParamSpec (Vst::ParamID id)
{
    return kSpecs[id];
}

double normalizedToPlain (const ParamSpec& spec, double normalized)
{
    const double n = std::clamp (normalized, 0.0, 1.0);
    switch (spec.curve)
    {
        case Curve::Linear: return spec.min + (spec.max - spec.min) * n;
        case Curve::Log: return spec.min * std::pow (spec.max / spec.min, n);
        case Curve::List: return std::round (n * (spec.labelCount - 1));
        case Curve::Toggle: return n >= 0.5 ? 1.0 : 0.0;
    }
    return spec.min;
}

double plainToNormalized (const ParamSpec& spec, double plain)
{
    const double p = std::clamp (plain, spec.min, spec.max);
    switch (spec.curve)
    {
        case Curve::Log: return std::log (p / spec.min) / std::log (spec.max / spec.min);
        case Curve::Linear:
        case Curve::List:
        case Curve::Toggle: return (p - spec.min) / (spec.max - spec.min);
    }
    return 0.0;
}

bool writeParamState (IBStream* stream, const ParamValues& normalizedValues)
{
    IBStreamer streamer (stream, kLittleEndian);
    if (!streamer.writeInt32 (kStateVersion) || !streamer.writeInt32 (kParamCount))
        return false;

    for (int32 id = 0; id < kParamCount; ++id)
    {
        if (!streamer.writeInt32 (id) || !streamer.writeDouble (normalizedValues[static_cast<size_t> (id)]))
            return false;
    }
    return true;
}

bool readParamState (IBStream* stream, const std::function<void (Vst::ParamID, double)>& onValue)
{
    IBStreamer streamer (stream, kLittleEndian);

    int32 version = 0;
    int32 count = 0;
    if (!streamer.readInt32 (version) || version < 1 || version > kStateVersion || !streamer.readInt32 (count))
        return false;

    for (int32 i = 0; i < count; ++i)
    {
        int32 id = 0;
        double value = 0.0;
        if (!streamer.readInt32 (id) || !streamer.readDouble (value))
            return false;

        if (id >= 0 && id < kParamCount && std::isfinite (value))
            onValue (static_cast<Vst::ParamID> (id), std::clamp (value, 0.0, 1.0));
    }
    return true;
}

} // namespace LoopTexturer
