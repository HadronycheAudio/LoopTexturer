// The effects chain, measured at the plug-in's output.

#include "Harness.h"
#include "Tests.h"

#include "public.sdk/source/common/memorystream.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <tuple>

using namespace Steinberg;

namespace LoopTexturer {
namespace Tests {

namespace {

// Finds an LFO's rate from how often the level rises through its average.
double measureLfoRate (const Samples& samples, size_t from)
{
    const int hop = static_cast<int> (kSampleRate * 0.002);
    std::vector<double> levels;
    for (size_t i = from; i + static_cast<size_t> (hop) < samples.size (); i += static_cast<size_t> (hop))
    {
        double energy = 0.0;
        for (int j = 0; j < hop; ++j)
            energy += static_cast<double> (samples[i + static_cast<size_t> (j)]) * samples[i + static_cast<size_t> (j)];
        levels.push_back (std::sqrt (energy / hop));
    }

    double average = 0.0;
    for (double level : levels)
        average += level;
    average /= static_cast<double> (levels.size ());

    int crossings = 0;
    int first = -1;
    int last = -1;
    for (size_t i = 1; i < levels.size (); ++i)
    {
        if (levels[i - 1] < average && levels[i] >= average)
        {
            if (first < 0)
                first = static_cast<int> (i);
            last = static_cast<int> (i);
            ++crossings;
        }
    }
    return crossings < 2 ? 0.0 : (crossings - 1) / ((last - first) * hop / kSampleRate);
}

} // namespace

void testLatency ()
{
    for (double sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        for (int on = 0; on < 2; ++on)
        {
            Plug plug (sampleRate);
            plug.neutral ();
            plug.set (kSatOn, on);
            plug.set (kPostSatOn, on);
            plug.settle ();

            Samples in (4096, 0.0f);
            in[1000] = 0.25f;
            Samples out;
            plug.runMono (in, out);

            const auto measured = static_cast<uint32> (findPeakIndex (out) - 1000);
            const auto reported = plug.processor->getLatencySamples ();
            std::printf ("  %.0f Hz, saturators %s: reported %u, measured %u\n", sampleRate, on ? "on" : "off", reported, measured);
            check ("the measured latency is what's reported", measured == reported);
            check ("the latency is 96 samples", reported == 96);
        }
    }
}

void testGainAndVolume ()
{
    const auto in = sine (1000, 0.1, 1.0);
    Samples out;

    Plug neutral;
    neutral.neutral ();
    neutral.settle ();
    neutral.runMono (in, out);
    const double reference = rms (out, 12000);
    std::printf ("  neutral chain: %.3f dB\n", db (reference / rms (in)));
    check ("the neutral chain is at unity", std::abs (db (reference / rms (in))) < 0.05);

    const std::tuple<ParamId, double, const char*> cases[] = {
        { kGain, 12.0, "Gain +12 dB" }, { kGain, -24.0, "Gain -24 dB" }, { kVolume, -12.0, "Volume -12 dB" }, { kVolume, 6.0, "Volume +6 dB" }
    };
    for (const auto& [id, value, name] : cases)
    {
        Plug plug;
        plug.neutral ();
        plug.set (id, value);
        plug.settle ();
        plug.runMono (in, out);
        const double measured = db (rms (out, 12000) / reference);
        std::printf ("  %s: %.3f dB\n", name, measured);
        check (name, std::abs (measured - value) < 0.05);
    }

    Plug silent;
    silent.neutral ();
    silent.set (kVolume, kVolumeSilenceDb);
    silent.settle ();
    silent.runMono (in, out);
    check ("Volume at its minimum is effectively silent", rms (out, 12000) < 1e-6);
}

void testPan ()
{
    const auto in = sine (1000, 0.1, 1.0);
    Samples left;
    Samples right;
    for (double pan : { -100.0, -50.0, 0.0, 50.0, 100.0 })
    {
        Plug plug;
        plug.neutral ();
        plug.set (kPan, pan);
        plug.settle ();
        plug.run (in, in, left, right);

        const double gainLeft = rms (left, 12000) / rms (in);
        const double gainRight = rms (right, 12000) / rms (in);
        std::printf ("  pan %+4.0f: L %7.2f dB, R %7.2f dB\n", pan, db (gainLeft), db (gainRight));
        check ("constant power", std::abs (gainLeft * gainLeft + gainRight * gainRight - 2.0) < 0.001);
        if (pan == 0.0)
            check ("both channels are unchanged in the centre", std::abs (db (gainLeft)) < 0.01 && std::abs (db (gainRight)) < 0.01);
    }
}

void testLfos ()
{
    Samples out;
    for (double rate : { 0.1, 1.0, 10.0 })
    {
        const auto in = sine (2000, 0.1, rate < 0.5 ? 45.0 : 6.0);

        Plug pan;
        pan.neutral ();
        pan.set (kPanLfoRate, rate);
        pan.set (kPanLfoAmount, 100);
        pan.settle ();
        pan.runMono (in, out);
        const double panRate = measureLfoRate (out, 4800);
        std::printf ("  pan LFO set to %.1f Hz: %.3f Hz\n", rate, panRate);
        check ("the pan LFO runs at its rate", std::abs (panRate - rate) < rate * 0.03);

        Plug filter;
        filter.set (kSatOn, 0);
        filter.set (kFilterMode, 0);
        filter.set (kFilterFreq, 2000);
        filter.set (kFilterLfoRate, rate);
        filter.set (kFilterLfoAmount, 2);
        filter.settle ();
        filter.runMono (in, out);
        const double filterRate = measureLfoRate (out, 4800);
        std::printf ("  filter LFO set to %.1f Hz: %.3f Hz\n", rate, filterRate);
        check ("the filter LFO runs at its rate", std::abs (filterRate - rate) < rate * 0.03);
    }

    for (int wave = 0; wave < 4; ++wave)
    {
        const auto in = sine (2000, 0.1, 4.0);
        Plug plug;
        plug.neutral ();
        plug.set (kPanLfoWave, wave);
        plug.set (kPanLfoRate, 2);
        plug.set (kPanLfoAmount, 100);
        plug.settle ();
        plug.runMono (in, out);

        double lowest = 1e9;
        double highest = 0.0;
        for (size_t i = 4800; i + 480 < out.size (); i += 480)
        {
            const double level = rms (slice (out, i, 480));
            lowest = std::min (lowest, level);
            highest = std::max (highest, level);
        }
        std::printf ("  wave %d: level ranges from %.4f to %.4f\n", wave, lowest, highest);
        check ("the wave moves the signal", highest > lowest * 2.0);
    }
}

void testFilter ()
{
    const char* names[] = { "low-pass", "high-pass", "band-pass", "notch" };
    const double frequencies[] = { 100.0, 1000.0, 10000.0 };
    Samples out;

    for (int mode = 0; mode < 4; ++mode)
    {
        double levels[3];
        for (int f = 0; f < 3; ++f)
        {
            const auto in = sine (frequencies[f], 0.1, 0.5);
            Plug plug;
            plug.set (kSatOn, 0);
            plug.set (kFilterMode, mode);
            plug.set (kFilterFreq, 1000);
            plug.settle ();
            plug.runMono (in, out);
            levels[f] = db (rms (out, 12000) / rms (in, 12000));
        }

        std::printf ("  %-9s at 1 kHz: 100 Hz %7.2f dB, 1 kHz %7.2f dB, 10 kHz %7.2f dB\n", names[mode], levels[0], levels[1], levels[2]);
        bool ok = false;
        switch (mode)
        {
            case 0: ok = std::abs (levels[0]) < 0.1 && std::abs (levels[1] + 3.01) < 0.3 && levels[2] < -35.0; break;
            case 1: ok = levels[0] < -35.0 && std::abs (levels[1] + 3.01) < 0.3 && std::abs (levels[2]) < 0.1; break;
            case 2: ok = levels[0] < -15.0 && std::abs (levels[1]) < 0.1 && levels[2] < -15.0; break;
            case 3: ok = std::abs (levels[0]) < 0.3 && levels[1] < -40.0 && std::abs (levels[2]) < 0.3; break;
        }
        check (names[mode], ok);
    }

    const auto in = sine (1000, 0.01, 0.5);
    Plug resonant;
    resonant.set (kSatOn, 0);
    resonant.set (kFilterFreq, 1000);
    resonant.set (kFilterResonance, 100);
    resonant.settle ();
    resonant.runMono (in, out);
    const double level = db (rms (out, 12000) / rms (in, 12000));
    std::printf ("  full resonance, at the cutoff: %.2f dB\n", level);
    check ("full resonance is a Q of 20", std::abs (level - db (20.0)) < 0.2);
}

void testSaturators ()
{
    const char* names[] = { "hard clip", "soft", "sine clip", "tube", "wavefolder", "rectifier" };
    const auto in = sine (1000, 0.5, 1.5);
    Samples out;
    double thirdHarmonics[6] {};

    for (int mode = 0; mode < 6; ++mode)
    {
        Plug plug;
        plug.neutral ();
        plug.set (kSatOn, 1);
        plug.set (kSatMode, mode);
        plug.set (kSatDrive, 12);
        plug.settle ();
        plug.runMono (in, out);

        const double first = toneLevel (out, 1000, 12000);
        const double second = toneLevel (out, 2000, 12000);
        const double third = toneLevel (out, 3000, 12000);
        double offset = 0.0;
        for (size_t i = 12000; i < out.size (); ++i)
            offset += out[i];
        offset /= static_cast<double> (out.size () - 12000);
        const double highest = peak (slice (out, 12000, out.size ()));

        thirdHarmonics[mode] = third / first;
        std::printf ("  %-10s H1 %6.1f, H2 %6.1f, H3 %6.1f dBFS, peak %.3f, DC %+.5f\n", names[mode], db (first), db (second), db (third), highest, offset);
        check ("adds harmonics, stays centred and stays bounded", std::max (second, third) > first * 0.01 && std::abs (offset) < 0.002 && highest < 1.2);
    }
    check ("the modes sound different", thirdHarmonics[0] != thirdHarmonics[1] && thirdHarmonics[1] != thirdHarmonics[2] && thirdHarmonics[3] != thirdHarmonics[4]);

    Plug dry;
    dry.neutral ();
    dry.set (kSatOn, 1);
    dry.set (kSatMode, 0);
    dry.set (kSatDrive, 24);
    dry.set (kSatMix, 0);
    dry.settle ();
    dry.runMono (in, out);
    check ("at 0% mix, the signal is clean", std::abs (db (toneLevel (out, 1000, 12000)) - db (0.5)) < 0.05 && db (toneLevel (out, 3000, 12000)) < -80.0);
}

void testAliasing ()
{
    // 14 kHz keeps the harmonic that folds at 48 kHz apart from the ones that fold at the oversampled
    // rate of 192 kHz. Curves with sharp corners have strong harmonics up there, so 4x oversampling
    // can only do so much for them, and the limits below reflect that.
    const auto measure = [] (int mode, double drive, double hz, double aliasHz) {
        const auto in = sine (hz, 0.5, 1.5);
        Samples out;
        Plug plug;
        plug.neutral ();
        plug.set (kSatOn, 1);
        plug.set (kSatMode, mode);
        plug.set (kSatDrive, drive);
        plug.settle ();
        plug.runMono (in, out);
        return db (toneLevel (out, aliasHz, 12000) / toneLevel (out, hz, 12000));
    };

    const double soft = measure (1, 12, 14000, 6000);
    std::printf ("  soft, 14 kHz, 12 dB of drive, alias at 6 kHz: %.1f dB\n", soft);
    check ("the filters remove what would fold at 48 kHz", soft < -80.0);

    const double hard = measure (0, 24, 14000, 6000);
    std::printf ("  hard clip, 14 kHz, 24 dB of drive, alias at 6 kHz: %.1f dB\n", hard);
    check ("hard clip at an extreme setting", hard < -40.0);

    const double moderate = measure (0, 12, 5000, 7000);
    std::printf ("  hard clip, 5 kHz, 12 dB of drive, alias at 7 kHz: %.1f dB\n", moderate);
    check ("hard clip at a moderate setting", moderate < -50.0);
}

void testBrokenInput ()
{
    const float nan = std::numeric_limits<float>::quiet_NaN ();
    const float infinity = std::numeric_limits<float>::infinity ();

    const auto breakSamples = [&] (Samples samples) {
        samples[1000] = nan;
        samples[2000] = infinity;
        samples[3000] = -infinity;
        return samples;
    };

    // Through the whole chain. A filter's state would otherwise hold on to a broken sample for ever.
    {
        Plug plug;
        plug.set (kFilterFreq, 1000);
        plug.settle ();

        const auto tone = sine (300.0, 0.5, 0.5);
        Samples left;
        Samples right;
        plug.run (breakSamples (tone), tone, left, right);
        check ("a broken sample on the left doesn't reach the output", isFinite (left) && isFinite (right));
        plug.run (tone, breakSamples (tone), left, right);
        check ("nor on the right", isFinite (left) && isFinite (right));

        plug.run (tone, tone, left, right);
        const double level = rms (left, left.size () / 2);
        std::printf ("  afterwards, the level is %.3f\n", level);
        check ("and it carries on working", isFinite (left) && isFinite (right) && level > 0.1);
    }

    // Into the loop, which would otherwise keep it for as long as the loop exists.
    {
        Plug plug;
        plug.looperOnly ();
        plug.set (kLooperFreeLength, 1.0);
        plug.settle (0.8);

        plug.pressButton (Looper::kCommandRec);
        Samples out;
        plug.runMono (breakSamples (sine (300.0, 0.5, 1.0)), out);
        plug.runMono (silence (0.1), out);
        plug.pressButton (Looper::kCommandOverdub);
        plug.settle (0.05);

        plug.runMono (silence (2.0), out);
        const double level = rms (out);
        std::printf ("  the loop plays back at %.3f\n", level);
        check ("a broken sample doesn't get into the loop", isFinite (out) && level > 0.1);

        // A project saved now would otherwise hold it too.
        Plug restored;
        restored.setStateBytes (plug.getStateBytes ());
        restored.looperOnly ();
        restored.settle (0.05);
        restored.runMono (silence (1.0), out);
        check ("nor into a saved project", isFinite (out));
    }
}

void testExtremeSettings ()
{
    std::mt19937 random (1);
    std::normal_distribution<float> noise (0.0f, 2.0f);
    Samples inLeft (48000 * 3);
    Samples inRight (48000 * 3);
    for (auto& sample : inLeft)
        sample = noise (random);
    for (auto& sample : inRight)
        sample = noise (random);

    Samples left;
    Samples right;
    bool allFinite = true;
    double highest = 0.0;
    for (int mode = 0; mode < 6; ++mode)
    {
        for (int filter = 0; filter < 4; ++filter)
        {
            Plug plug;
            plug.set (kGain, 24);
            plug.set (kPanLfoAmount, 100);
            plug.set (kPanLfoRate, 10);
            plug.set (kPanLfoWave, 2);
            plug.set (kFilterMode, filter);
            plug.set (kFilterFreq, 18000);
            plug.set (kFilterResonance, 100);
            plug.set (kFilterLfoAmount, 4);
            plug.set (kFilterLfoRate, 10);
            plug.set (kFilterLfoWave, 3);
            plug.set (kSatMode, mode);
            plug.set (kSatDrive, 36);
            plug.set (kPostSatOn, 1);
            plug.set (kPostSatMode, mode);
            plug.set (kPostSatDrive, 36);
            plug.set (kVolume, 6);
            plug.run (inLeft, inRight, left, right);

            for (size_t i = 0; i < left.size (); ++i)
                allFinite = allFinite && std::isfinite (left[i]) && std::isfinite (right[i]);
            highest = std::max ({ highest, peak (left), peak (right) });
        }
    }
    std::printf ("  largest output: %.3f, with Volume at +6 dB\n", highest);
    check ("no NaN or infinity", allFinite);
}

void testParameterState ()
{
    Plug saved;
    saved.set (kGain, 7);
    saved.set (kPan, -33);
    saved.set (kFilterMode, 2);
    saved.set (kFilterFreq, 750);
    saved.set (kSatMode, 4);
    saved.set (kSatDrive, 9);
    saved.settle (0.01);

    MemoryStream stream;
    check ("getState", saved.component->getState (&stream) == kResultOk);

    Plug restored;
    stream.seek (0, IBStream::kIBSeekSet, nullptr);
    check ("setState", restored.component->setState (&stream) == kResultOk);
    stream.seek (0, IBStream::kIBSeekSet, nullptr);
    check ("setComponentState", restored.controller->setComponentState (&stream) == kResultOk);

    bool same = true;
    for (Vst::ParamID id = 0; id < kParamCount; ++id)
        same = same && std::abs (saved.controller->getParamNormalized (id) - restored.controller->getParamNormalized (id)) < 1e-9;
    check ("the controller restored every value", same);

    const auto in = sine (1000, 0.1, 1.0);
    Samples outSaved;
    Samples outRestored;
    saved.settle ();
    restored.settle ();
    saved.runMono (in, outSaved);
    restored.runMono (in, outRestored);
    check ("the restored instance sounds the same", std::abs (db (rms (outSaved, 24000)) - db (rms (outRestored, 24000))) < 0.01);

    MemoryStream empty;
    Plug invalid;
    check ("an empty state is rejected without crashing", invalid.component->setState (&empty) != kResultOk);
}

void testParameterText ()
{
    Plug plug;
    const std::tuple<ParamId, double> cases[] = {
        { kGain, 7.5 }, { kFilterFreq, 1000.0 }, { kFilterMode, 3.0 }, { kSatMode, 4.0 }, { kSatOn, 0.0 },
        { kVolume, kVolumeSilenceDb }, { kPanLfoRate, 0.1 }, { kLooperBars, 6.0 }, { kLooperCustomBars, 32.0 }
    };
    for (const auto& [id, plain] : cases)
    {
        const double normalized = plug.controller->plainParamToNormalized (id, plain);
        Vst::String128 text {};
        plug.controller->getParamStringByValue (id, normalized, text);

        double parsed = -1.0;
        const auto result = plug.controller->getParamValueByString (id, text, parsed);
        check ("the value's text reads back as the same value", result == kResultOk && std::abs (parsed - normalized) < 0.002);
    }
}

void measureCpu ()
{
    const double seconds = 20.0;
    const auto in = sine (440, 0.3, seconds);
    Samples out;
    for (int post = 0; post < 2; ++post)
    {
        Plug plug;
        plug.set (kPostSatOn, post);
        plug.set (kPanLfoAmount, 50);
        plug.set (kFilterLfoAmount, 2);

        const auto start = std::chrono::steady_clock::now ();
        plug.runMono (in, out);
        const double took = std::chrono::duration<double> (std::chrono::steady_clock::now () - start).count ();
        std::printf ("Post saturator %s: %.2f%% of one core\n", post ? "on" : "off", 100.0 * took / seconds);
    }

    // The looper's worst case: the head passes the most cells per sample at the shortest length, and
    // overdubbing with feedback writes all of them.
    Plug plug;
    plug.looperOnly ();
    recordFree (plug, 440, 0.3, 0.5);
    plug.set (kLooperFeedback, 90);
    plug.pressButton (Looper::kCommandOverdub);
    plug.settle (0.05);

    const auto start = std::chrono::steady_clock::now ();
    plug.runMono (in, out);
    const double took = std::chrono::duration<double> (std::chrono::steady_clock::now () - start).count ();
    std::printf ("Overdubbing the shortest loop: %.2f%% of one core\n", 100.0 * took / seconds);
}

} // namespace Tests
} // namespace LoopTexturer
