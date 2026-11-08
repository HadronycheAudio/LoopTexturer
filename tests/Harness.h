#pragma once

// The test host from HadronycheCore, and what this plug-in's tests add to it.

#include "LooperIds.h"
#include "ParamIds.h"

#include "hadronyche/dsp/Gain.h"

#include "hadronyche/testhost/Host.h"

#include <cstddef>
#include <vector>

namespace LoopTexturer {
namespace Tests {

using namespace Hadronyche::TestHost;

// The saved state: version and count, then an ID and value per parameter.
constexpr size_t kParamStateBytes = 8 + kParamCount * 12;
// What follows when there's a loop: flag, mode, frames and 4 doubles, then the audio.
constexpr size_t kLoopHeaderBytes = 4 + 4 + 8 + 4 * 8;
constexpr size_t kNoLoopStateBytes = kParamStateBytes + 4;

class Plug : public PlugBase
{
public:
    using PlugBase::PlugBase;

    void run (const Samples& left, const Samples& right, Samples& outLeft, Samples& outRight);
    void runMono (const Samples& in, Samples& out);
    void settle (double seconds = 0.3);

    // A filter and saturators set so that a measurement only sees what it's testing.
    void neutral ();
    // As above, with only the looper in the output.
    void looperOnly ();

    // The same message the panel's buttons send.
    void pressButton (int command);
};

// A simulated song, which moves on in Plug::run while it's playing.
class Song
{
public:
    explicit Song (Plug& plug, double tempo = 120.0, int numerator = 4, int denominator = 4);

    void play (bool on);
    void locate (double quarters);
    size_t samplesFor (double quarters) const;

    Steinberg::Vst::ProcessContext context {};
};

// Switches to tempo sync, with the looper alone in the output, and waits for the worker thread to
// allocate the tape. Tests run faster than real time, so without this they would press Rec before
// the tape exists.
void useSyncMode (Plug& plug);

// Records a tone on the free running tape for one whole pass, then turns off the overdubbing that
// follows, and waits for the tape speed to finish gliding to the new length first.
void recordFree (Plug& plug, double hz, double amplitude, double lengthSeconds);

// Signals.
Samples sine (double hz, double amplitude, double seconds, double sampleRate = kSampleRate);
Samples silence (double seconds);

// Measurements.
double measureFrequency (const Samples& samples, double sampleRate = kSampleRate);
std::vector<size_t> findClicks (const Samples& samples, float threshold = 0.1f);
size_t findPeakIndex (const Samples& samples);

} // namespace Tests
} // namespace LoopTexturer
