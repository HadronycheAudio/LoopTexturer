// These protect projects people have already saved. The expected values are written out here on
// purpose, rather than taken from the plug-in's own definitions, so that changing those definitions
// fails a test. If one of these fails, the change would damage saved projects: fix the change, not
// the test, unless it also migrates old projects.

#include "Harness.h"
#include "Tests.h"

#include "pluginterfaces/base/funknown.h"
#include "public.sdk/source/common/memorystream.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Steinberg;

namespace LoopTexturer {
namespace Tests {

namespace {

// Hosts save these numbers, so they can never change. New parameters get new numbers.
static_assert (kGain == 0 && kPan == 1 && kPanLfoRate == 2 && kPanLfoAmount == 3 && kPanLfoWave == 4, "Parameter IDs are saved in projects");
static_assert (kFilterMode == 5 && kFilterFreq == 6 && kFilterResonance == 7 && kFilterLfoRate == 8 && kFilterLfoAmount == 9 && kFilterLfoWave == 10, "Parameter IDs are saved in projects");
static_assert (kSatOn == 11 && kSatMode == 12 && kSatDrive == 13 && kSatMix == 14, "Parameter IDs are saved in projects");
static_assert (kPostSatOn == 15 && kPostSatMode == 16 && kPostSatDrive == 17 && kPostSatMix == 18, "Parameter IDs are saved in projects");
static_assert (kVolume == 19 && kLooperMode == 20 && kLooperFreeLength == 21 && kLooperBars == 22 && kLooperCustomBars == 23 && kLooperFeedback == 24 && kLooperDryWet == 25, "Parameter IDs are saved in projects");

void appendBytes (std::vector<char>& out, const void* data, size_t size)
{
    const auto* bytes = static_cast<const char*> (data);
    out.insert (out.end (), bytes, bytes + size);
}

template <typename T>
void append (std::vector<char>& out, T value)
{
    appendBytes (out, &value, sizeof (value));
}

std::string getLabel (Plug& plug, ParamId id, int index)
{
    Vst::ParameterInfo info {};
    plug.controller->getParameterInfo (id, info);
    return paramText (plug.controller, id, static_cast<double> (index) / info.stepCount);
}

} // namespace

void testClassIds ()
{
    // The values in src/PluginConfig.h, which hosts use to find the plug-in in saved projects.
    const FUID processor (0x2402746B, 0x3F884603, 0x87927853, 0x1D2D1EF1);
    const FUID controller (0xE906F66D, 0x91354D4F, 0x8BEBAFFE, 0x709D2759);

    bool foundProcessor = false;
    bool foundController = false;
    auto* factory = getFactory ();
    for (int32 i = 0; i < factory->countClasses (); ++i)
    {
        PClassInfo info;
        factory->getClassInfo (i, &info);
        foundProcessor = foundProcessor || (std::strcmp (info.category, kVstAudioEffectClass) == 0 && FUnknownPrivate::iidEqual (info.cid, processor.toTUID ()));
        foundController = foundController || (std::strcmp (info.category, kVstComponentControllerClass) == 0 && FUnknownPrivate::iidEqual (info.cid, controller.toTUID ()));
    }
    check ("the processor's class ID is unchanged", foundProcessor);
    check ("the controller's class ID is unchanged", foundController);
}

void testParameterIds ()
{
    // The numbering itself is checked at compile time, by the static_asserts above.
    Plug plug;
    bool ok = plug.controller->getParameterCount () >= 26;
    for (int32 i = 0; ok && i < plug.controller->getParameterCount (); ++i)
    {
        Vst::ParameterInfo info {};
        ok = plug.controller->getParameterInfo (i, info) == kResultOk && info.id == static_cast<Vst::ParamID> (i);
    }
    check ("every parameter's ID matches its position", ok);
}

void testParameterRanges ()
{
    // Hosts save values normalized to 0..1, so a new range or curve changes what saved projects
    // sound like. The middle value is what tells the curves apart.
    struct Expected
    {
        ParamId id;
        const char* name;
        double min;
        double middle;
        double max;
    };

    const Expected expected[] = {
        { kGain, "Gain", -24.0, 0.0, 24.0 },
        { kPan, "Pan", -100.0, 0.0, 100.0 },
        { kPanLfoRate, "Pan LFO rate", 0.1, 1.0, 10.0 },
        { kPanLfoAmount, "Pan LFO amount", 0.0, 50.0, 100.0 },
        { kPanLfoWave, "Pan LFO wave", 0.0, 2.0, 3.0 },
        { kFilterMode, "Filter mode", 0.0, 2.0, 3.0 },
        { kFilterFreq, "Filter frequency", 20.0, 632.4555, 20000.0 },
        { kFilterResonance, "Filter resonance", 0.0, 50.0, 100.0 },
        { kFilterLfoRate, "Filter LFO rate", 0.1, 1.0, 10.0 },
        { kFilterLfoAmount, "Filter LFO amount", 0.0, 2.0, 4.0 },
        { kFilterLfoWave, "Filter LFO wave", 0.0, 2.0, 3.0 },
        { kSatOn, "Saturator on", 0.0, 1.0, 1.0 },
        { kSatMode, "Saturator mode", 0.0, 3.0, 5.0 },
        { kSatDrive, "Saturator drive", 0.0, 18.0, 36.0 },
        { kSatMix, "Saturator mix", 0.0, 50.0, 100.0 },
        { kPostSatOn, "Post saturator on", 0.0, 1.0, 1.0 },
        { kPostSatMode, "Post saturator mode", 0.0, 3.0, 5.0 },
        { kPostSatDrive, "Post saturator drive", 0.0, 18.0, 36.0 },
        { kPostSatMix, "Post saturator mix", 0.0, 50.0, 100.0 },
        { kVolume, "Volume", -60.0, -27.0, 6.0 },
        { kLooperMode, "Looper mode", 0.0, 1.0, 1.0 },
        { kLooperFreeLength, "Looper length", 0.5, 3.872983, 30.0 },
        { kLooperBars, "Looper bars", 0.0, 4.0, 7.0 },
        { kLooperCustomBars, "Looper custom bars", 1.0, 33.0, 64.0 },
        { kLooperFeedback, "Looper feedback", 0.0, 100.0, 200.0 },
        { kLooperDryWet, "Looper dry/wet", 0.0, 50.0, 100.0 },
    };

    Plug plug;
    for (const auto& parameter : expected)
    {
        const double min = plug.controller->normalizedParamToPlain (parameter.id, 0.0);
        const double middle = plug.controller->normalizedParamToPlain (parameter.id, 0.5);
        const double max = plug.controller->normalizedParamToPlain (parameter.id, 1.0);
        const double tolerance = 1e-3;
        const bool ok = std::abs (min - parameter.min) < tolerance && std::abs (middle - parameter.middle) < tolerance && std::abs (max - parameter.max) < tolerance;
        if (!ok)
            std::printf ("  %s is now %g, %g, %g\n", parameter.name, min, middle, max);
        check (parameter.name, ok);
    }
}

void testParameterLabels ()
{
    // List parameters are saved by position, so the order of each list can't change.
    struct Expected
    {
        ParamId id;
        std::vector<std::string> labels;
    };

    const std::vector<std::string> lfoWaves = { "Sine", "Triangle", "Square", "Sample & hold" };
    const std::vector<std::string> saturatorModes = { "Hard clip", "Soft saturator", "Sine clip", "Tube", "Wavefolder", "Rectifier" };

    const Expected expected[] = {
        { kPanLfoWave, lfoWaves },
        { kFilterLfoWave, lfoWaves },
        { kFilterMode, { "Low-pass", "High-pass", "Band-pass", "Notch" } },
        { kSatMode, saturatorModes },
        { kPostSatMode, saturatorModes },
        { kLooperMode, { "Free running", "Tempo sync" } },
        { kLooperBars, { "1/2 bar", "1 bar", "2 bars", "4 bars", "6 bars", "8 bars", "Custom", "Open" } },
    };

    Plug plug;
    for (const auto& parameter : expected)
    {
        bool ok = true;
        for (size_t i = 0; i < parameter.labels.size (); ++i)
        {
            const auto label = getLabel (plug, parameter.id, static_cast<int> (i));
            if (label != parameter.labels[i])
            {
                std::printf ("  parameter %d, entry %zu: expected \"%s\", got \"%s\"\n", static_cast<int> (parameter.id), i, parameter.labels[i].c_str (), label.c_str ());
                ok = false;
            }
        }
        check ("list entries are in their saved order", ok);
    }
}

void testSavedFormat ()
{
    // A state written out byte by byte, the way version 2 of the plug-in saved it.
    constexpr int64_t frames = 96000;
    const auto audio = sine (1000, 0.25, 2.0);

    std::vector<char> saved;
    append<int32_t> (saved, 2); // Version
    append<int32_t> (saved, 1); // Parameter count
    append<int32_t> (saved, kLooperDryWet);
    append<double> (saved, 1.0);
    append<int32_t> (saved, 1); // Has a loop
    append<int32_t> (saved, 0); // Free running
    append<int64_t> (saved, frames);
    append<double> (saved, 0.0); // Quarters
    append<double> (saved, 0.0); // Anchor
    append<double> (saved, 0.0); // Frames per quarter
    append<double> (saved, 1.0); // Write speed
    appendBytes (saved, audio.data (), audio.size () * sizeof (float));
    appendBytes (saved, audio.data (), audio.size () * sizeof (float));

    MemoryStream stream (saved.data (), static_cast<TSize> (saved.size ()));
    Plug plug;
    check ("a version 2 state loads", plug.component->setState (&stream) == kResultOk);

    // At the default length of 2 seconds, 96000 frames play at normal speed.
    plug.neutral ();
    Samples out;
    plug.runMono (silence (2.5), out);
    const auto tail = slice (out, 24000, 96000);
    const double frequency = measureFrequency (tail);
    const double level = rms (tail) * std::sqrt (2.0);
    std::printf ("  restored loop: %.2f Hz, level %.3f\n", frequency, level);
    check ("the loop plays at the right pitch and level", std::abs (frequency - 1000.0) < 1.0 && std::abs (level - 0.25) < 0.01);

    // Reading a parameter back also shows the loop didn't overwrite it.
    const auto written = plug.getStateBytes ();
    check ("it saves as the current version", readAt<int32_t> (written, 0) == 3);
    check ("every parameter is saved", readAt<int32_t> (written, 4) == kParamCount);

    bool idsInOrder = true;
    for (int i = 0; i < kParamCount; ++i)
        idsInOrder = idsInOrder && readAt<int32_t> (written, 8 + static_cast<size_t> (i) * 12) == i;
    check ("parameters are saved as ID and value pairs", idsInOrder);

    check ("the loop follows the parameters", readAt<int32_t> (written, kParamStateBytes) == 1);
    check ("then its mode", readAt<int32_t> (written, kParamStateBytes + 4) == 0);
    check ("then its frame count", readAt<int64_t> (written, kParamStateBytes + 8) == frames);

    const size_t audioStart = kParamStateBytes + kLoopHeaderBytes;
    const size_t audioBytes = audio.size () * sizeof (float);
    check ("then its audio, unchanged",
           written.size () == audioStart + 2 * audioBytes && std::memcmp (written.data () + audioStart, audio.data (), audioBytes) == 0);
}

void testOlderProject ()
{
    // A project saved before the looper existed: version 1, with 20 parameters and nothing after them.
    std::vector<char> saved;
    append<int32_t> (saved, 1);
    append<int32_t> (saved, 20);
    for (int32_t id = 0; id < 20; ++id)
    {
        append<int32_t> (saved, id);
        append<double> (saved, id == kGain ? 0.75 : 0.5);
    }

    MemoryStream processorStream (saved.data (), static_cast<TSize> (saved.size ()));
    MemoryStream controllerStream (saved.data (), static_cast<TSize> (saved.size ()));
    Plug plug;
    check ("it loads", plug.component->setState (&processorStream) == kResultOk);
    check ("the controller loads it too", plug.controller->setComponentState (&controllerStream) == kResultOk);

    plug.settle (0.01);
    const auto written = plug.getStateBytes ();
    check ("its values are kept", std::abs (readAt<double> (written, 8 + 4) - 0.75) < 1e-9);
    check ("it has no loop", written.size () == kNoLoopStateBytes);
    check ("a looper parameter it never had stays at its default", std::abs (plug.controller->getParamNormalized (kLooperFeedback) - 0.5) < 1e-9);
}

namespace {

// A free running loop as version 1.0.0 saved it, whose length went up to 10 s: a sine on a whole 10 s tape.
std::vector<char> makeTenSecondTapeState (double savedLength)
{
    constexpr int64_t frames = 480000;
    const auto audio = sine (1000, 0.25, 10.0);

    std::vector<char> saved;
    append<int32_t> (saved, 2); // Version
    append<int32_t> (saved, 1); // Parameter count
    append<int32_t> (saved, kLooperFreeLength);
    append<double> (saved, savedLength);
    append<int32_t> (saved, 1); // Has a loop
    append<int32_t> (saved, 0); // Free running
    append<int64_t> (saved, frames);
    append<double> (saved, 0.0); // Quarters
    append<double> (saved, 0.0); // Anchor
    append<double> (saved, 0.0); // Frames per quarter
    append<double> (saved, 1.0); // Write speed
    appendBytes (saved, audio.data (), audio.size () * sizeof (float));
    appendBytes (saved, audio.data (), audio.size () * sizeof (float));
    return saved;
}

} // namespace

void testOlderLength ()
{
    // Each saved value, what the knob meant when it was saved, and how fast that plays the tape.
    const double cases[][2] = { { 1.0, 10.0 }, { 0.5, 2.236068 }, { 0.0, 0.5 } };
    for (const auto& [savedLength, seconds] : cases)
    {
        auto saved = makeTenSecondTapeState (savedLength);
        MemoryStream processorStream (saved.data (), static_cast<TSize> (saved.size ()));
        MemoryStream controllerStream (saved.data (), static_cast<TSize> (saved.size ()));
        Plug plug;
        check ("it loads", plug.component->setState (&processorStream) == kResultOk);
        check ("the controller loads it too", plug.controller->setComponentState (&controllerStream) == kResultOk);

        const double shown = plug.controller->normalizedParamToPlain (kLooperFreeLength, plug.controller->getParamNormalized (kLooperFreeLength));
        const auto written = plug.getStateBytes ();
        const double used = plug.controller->normalizedParamToPlain (kLooperFreeLength, readAt<double> (written, 8 + kLooperFreeLength * 12 + 4));
        std::printf ("  saved at %.2f: the panel shows %.3f s, the audio side has %.3f s\n", savedLength, shown, used);
        check ("the panel shows the same length as before", std::abs (shown - seconds) < 1e-3);
        check ("the audio side has the same length as before", std::abs (used - seconds) < 1e-3);

        plug.looperOnly ();
        Samples out;
        plug.runMono (silence (1.5), out);
        const auto tail = slice (out, 24000, 48000);
        const double frequency = measureFrequency (tail);
        const double expected = 1000.0 * 10.0 / seconds;
        std::printf ("  plays at %.1f Hz, expected %.1f Hz\n", frequency, expected);
        check ("the loop keeps the pitch it had", std::abs (frequency - expected) < expected * 0.01);
    }

    // From version 3, the value is already in the new range.
    std::vector<char> current;
    append<int32_t> (current, 3);
    append<int32_t> (current, 1);
    append<int32_t> (current, kLooperFreeLength);
    append<double> (current, 0.5);
    append<int32_t> (current, 0); // No loop
    MemoryStream stream (current.data (), static_cast<TSize> (current.size ()));
    Plug plug;
    check ("a current state loads", plug.controller->setComponentState (&stream) == kResultOk);
    const double shown = plug.controller->normalizedParamToPlain (kLooperFreeLength, plug.controller->getParamNormalized (kLooperFreeLength));
    check ("a current state isn't converted", std::abs (shown - 3.872983) < 1e-3);
}

void testOlderTapeReplaced ()
{
    auto saved = makeTenSecondTapeState (1.0);
    MemoryStream stream (saved.data (), static_cast<TSize> (saved.size ()));
    Plug plug;
    check ("it loads", plug.component->setState (&stream) == kResultOk);
    plug.looperOnly ();
    plug.settle (0.1);
    plug.pressButton (Looper::kCommandClear);
    plug.set (kLooperFreeLength, 30.0);

    // The buttons do nothing until the worker thread has provided the new tape, however long that
    // takes on this machine, so this keeps pressing Rec until recording starts.
    constexpr double kSeconds = 30.0;
    constexpr double kHz = 5000.0;
    const auto in = sine (kHz, 0.25, kSeconds);
    bool isRecording = false;
    Samples out;
    for (int attempt = 0; attempt < 300 && !isRecording; ++attempt)
    {
        sleepMilliseconds (20);
        plug.settle (0.8);
        plug.pressButton (Looper::kCommandRec);
        plug.runMono (slice (in, 0, 4800), out);
        isRecording = plug.getStateBytes ().size () != kNoLoopStateBytes;
    }
    check ("recording starts after clearing", isRecording);

    plug.runMono (slice (in, 4800, in.size () - 4800), out);
    plug.pressButton (Looper::kCommandRec);
    plug.settle (0.05);

    const auto written = plug.getStateBytes ();
    const auto frames = readAt<int64_t> (written, kParamStateBytes + 8);
    std::printf ("  the new loop's tape has %lld frames\n", static_cast<long long> (frames));
    check ("the new recording gets a whole 30 s tape", frames == 1440000);

    plug.runMono (silence (1.0), out);
    const double frequency = measureFrequency (out);
    const double level = rms (out) * std::sqrt (2.0);
    std::printf ("  plays at %.1f Hz, level %.3f\n", frequency, level);
    check ("a high tone is recorded cleanly", std::abs (frequency - kHz) < 5.0 && std::abs (level - 0.25) < 0.01);
}

} // namespace Tests
} // namespace LoopTexturer
