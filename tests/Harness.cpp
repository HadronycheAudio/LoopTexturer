#include "Harness.h"

#include "Messages.h"

#include <algorithm>
#include <cmath>

using namespace Steinberg;

namespace LoopTexturer {
namespace Tests {

void Plug::pressButton (int command)
{
    sendToProcessor (kMessageLooperCommand, kAttributeCommand, command);
}

void Plug::run (const Samples& left, const Samples& right, Samples& outLeft, Samples& outRight)
{
    const int total = static_cast<int> (left.size ());
    outLeft.resize (left.size ());
    outRight.resize (left.size ());

    for (int position = 0; position < total; position += kBlockSize)
    {
        const int count = std::min (kBlockSize, total - position);
        float* inputs[2] = { const_cast<float*> (&left[static_cast<size_t> (position)]), const_cast<float*> (&right[static_cast<size_t> (position)]) };
        float* outputs[2] = { &outLeft[static_cast<size_t> (position)], &outRight[static_cast<size_t> (position)] };

        runBlock (inputs, outputs, count);

        if (context && (context->state & Vst::ProcessContext::kPlaying))
        {
            context->projectTimeMusic += count * context->tempo / (60.0 * _sampleRate);
            context->projectTimeSamples += count;
            const double bar = context->timeSigNumerator * 4.0 / context->timeSigDenominator;
            context->barPositionMusic = std::floor (context->projectTimeMusic / bar) * bar;
        }
    }
}

void Plug::runMono (const Samples& in, Samples& out)
{
    Samples right;
    run (in, in, out, right);
}

void Plug::settle (double seconds)
{
    const Samples zero (static_cast<size_t> (seconds * _sampleRate), 0.0f);
    Samples left;
    Samples right;
    run (zero, zero, left, right);
}

void Plug::neutral ()
{
    set (kFilterMode, 1);
    set (kFilterFreq, 20);
    set (kSatOn, 0);
    set (kPostSatOn, 0);
}

void Plug::looperOnly ()
{
    neutral ();
    set (kLooperDryWet, 100);
}

Song::Song (Plug& plug, double tempo, int numerator, int denominator)
{
    context.state = Vst::ProcessContext::kProjectTimeMusicValid | Vst::ProcessContext::kTempoValid
                    | Vst::ProcessContext::kTimeSigValid | Vst::ProcessContext::kBarPositionValid;
    context.sampleRate = kSampleRate;
    context.tempo = tempo;
    context.timeSigNumerator = numerator;
    context.timeSigDenominator = denominator;
    plug.context = &context;
}

void Song::play (bool on)
{
    if (on)
        context.state |= Vst::ProcessContext::kPlaying;
    else
        context.state &= ~Vst::ProcessContext::kPlaying;
}

void Song::locate (double quarters)
{
    context.projectTimeMusic = quarters;
    context.projectTimeSamples = static_cast<Vst::TSamples> (quarters * 60.0 / context.tempo * kSampleRate);
    const double bar = context.timeSigNumerator * 4.0 / context.timeSigDenominator;
    context.barPositionMusic = std::floor (quarters / bar) * bar;
}

size_t Song::samplesFor (double quarters) const
{
    return static_cast<size_t> (std::llround (quarters * 60.0 / context.tempo * kSampleRate));
}

void useSyncMode (Plug& plug)
{
    plug.looperOnly ();
    plug.set (kLooperMode, 1);

    // The tape is sized from the tempo, so this needs a valid one. The slowest tempo in the tests
    // gives a tape that's large enough for all of them. This also gives the level changes above
    // time to finish.
    Song song (plug, 60.0);
    plug.settle (0.3);

    // How long the tape takes depends on the build and the machine, and a host can't ask whether
    // it's there, so this finds out from a short test recording: only a recorded click comes back.
    song.context.tempo = 480.0;
    plug.set (kLooperBars, kBarsHalf);

    constexpr int kMaxAttempts = 300;
    bool isReady = false;
    Samples in (static_cast<size_t> (kSampleRate * 0.6), 0.0f);
    in[1000] = 0.9f;
    Samples out;
    for (int attempt = 0; attempt < kMaxAttempts && !isReady; ++attempt)
    {
        song.locate (0.0);
        song.play (true);
        plug.pressButton (Looper::kCommandRec);
        plug.runMono (in, out);
        isReady = findClicks (out).size () >= 1;

        song.play (false);
        plug.pressButton (Looper::kCommandStop);
        plug.pressButton (Looper::kCommandClear);
        plug.settle (0.01);

        if (!isReady)
            sleepMilliseconds (100);
    }
    check ("the tempo sync tape was allocated", isReady);

    plug.set (kLooperBars, kBarsOne);
    plug.settle (0.05);
    plug.context = nullptr;
}

void recordFree (Plug& plug, double hz, double amplitude, double lengthSeconds)
{
    plug.set (kLooperFreeLength, lengthSeconds);
    plug.settle (0.8);
    plug.pressButton (Looper::kCommandRec);

    Samples out;
    plug.runMono (sine (hz, amplitude, lengthSeconds), out);
    plug.runMono (silence (0.1), out);
    plug.pressButton (Looper::kCommandOverdub);
    plug.settle (0.05);
}

Samples sine (double hz, double amplitude, double seconds, double sampleRate)
{
    Samples samples (static_cast<size_t> (seconds * sampleRate));
    for (size_t i = 0; i < samples.size (); ++i)
        samples[i] = static_cast<float> (amplitude * std::sin (2.0 * Detail::kPi * hz * static_cast<double> (i) / sampleRate));
    return samples;
}

Samples silence (double seconds)
{
    return Samples (static_cast<size_t> (seconds * kSampleRate), 0.0f);
}

double measureFrequency (const Samples& samples, double sampleRate)
{
    int crossings = 0;
    int first = -1;
    int last = -1;
    for (size_t i = 1; i < samples.size (); ++i)
    {
        if (samples[i - 1] < 0.0f && samples[i] >= 0.0f)
        {
            if (first < 0)
                first = static_cast<int> (i);
            last = static_cast<int> (i);
            ++crossings;
        }
    }
    return crossings < 2 ? 0.0 : (crossings - 1) * sampleRate / (last - first);
}

std::vector<size_t> findClicks (const Samples& samples, float threshold)
{
    std::vector<size_t> found;
    for (size_t i = 0; i < samples.size (); ++i)
    {
        if (std::abs (samples[i]) > threshold && (found.empty () || i > found.back () + 100))
            found.push_back (i);
    }
    return found;
}

size_t findPeakIndex (const Samples& samples)
{
    const auto highest = std::max_element (samples.begin (), samples.end (), [] (float a, float b) { return std::abs (a) < std::abs (b); });
    return static_cast<size_t> (highest - samples.begin ());
}

} // namespace Tests
} // namespace LoopTexturer
