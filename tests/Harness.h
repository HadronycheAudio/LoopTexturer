#pragma once

// A minimal VST3 host. It loads the built plug-in and drives it through the same interfaces Cubase
// uses, so the tests exercise the real binary rather than individual classes.

#include "Parameters.h"
#include "dsp/Looper.h"

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <cstddef>
#include <vector>

namespace LoopTexturer {
namespace Tests {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 256;

// The saved state: version and count, then an ID and value per parameter.
constexpr size_t kParamStateBytes = 8 + kParamCount * 12;
// What follows when there's a loop: flag, mode, frames and 4 doubles, then the audio.
constexpr size_t kLoopHeaderBytes = 4 + 4 + 8 + 4 * 8;
constexpr size_t kNoLoopStateBytes = kParamStateBytes + 4;

using Samples = std::vector<float>;

// Loading the plug-in.
bool loadPlugin (const wchar_t* path);
void unloadPlugin ();
Steinberg::IPluginFactory* getFactory ();

// Results. Measurements print as they go, so that a failure comes with its numbers.
void check (const char* what, bool ok);
int getFailureCount ();

class ParameterChanges;

// One instance of the plug-in, connected and active, as a host would set it up.
class Plug
{
public:
    explicit Plug (double sampleRate = kSampleRate);
    ~Plug ();

    Plug (const Plug&) = delete;
    Plug& operator= (const Plug&) = delete;

    // Applied to both sides, as a host would forward it, and takes effect in the next block.
    void set (ParamId id, double plain);

    void run (const Samples& left, const Samples& right, Samples& outLeft, Samples& outRight);
    void runMono (const Samples& in, Samples& out);
    void settle (double seconds = 0.3);

    // A filter and saturators set so that a measurement only sees what it's testing.
    void neutral ();
    // As above, with only the looper in the output.
    void looperOnly ();

    // The same message the panel's buttons send.
    void pressButton (int command);

    std::vector<char> getStateBytes ();

    Steinberg::Vst::IComponent* component { nullptr };
    Steinberg::Vst::IAudioProcessor* processor { nullptr };
    Steinberg::Vst::IEditController* controller { nullptr };
    Steinberg::Vst::ProcessContext* context { nullptr }; // Null means the host gives no transport.

private:
    ParameterChanges* _changes;
    Steinberg::Vst::IConnectionPoint* _componentConnection { nullptr };
    Steinberg::Vst::IConnectionPoint* _controllerConnection { nullptr };
    double _sampleRate;
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

// Switches to tempo sync, and waits for the worker thread to allocate the tape. Tests run faster
// than real time, so without this they would press Rec before the tape exists.
void useSyncMode (Plug& plug);

// Records a tone on the free running tape for one whole pass, then turns off the overdubbing that
// follows, and waits for the tape speed to finish gliding to the new length first.
void recordFree (Plug& plug, double hz, double amplitude, double lengthSeconds);

// Signals.
Samples sine (double hz, double amplitude, double seconds, double sampleRate = kSampleRate);
Samples silence (double seconds);
Samples slice (const Samples& samples, size_t from, size_t count);

// Measurements.
double rms (const Samples& samples, size_t from = 0);
double peak (const Samples& samples);
double db (double value);
double toneLevel (const Samples& samples, double hz, size_t from = 0, double sampleRate = kSampleRate); // Hann windowed.
double measureFrequency (const Samples& samples, double sampleRate = kSampleRate);
std::vector<size_t> findClicks (const Samples& samples, float threshold = 0.1f);
size_t findPeakIndex (const Samples& samples);

} // namespace Tests
} // namespace LoopTexturer
