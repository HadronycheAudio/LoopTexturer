// The looper, in both modes. Timing is checked to the exact sample wherever it's meant to be exact.

#include "Harness.h"
#include "Tests.h"

#include "public.sdk/source/common/memorystream.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <tuple>
#include <utility>

using namespace Steinberg;

namespace LoopTexturer {
namespace Tests {

namespace {

constexpr size_t kLoopAudioStart = kParamStateBytes + kLoopHeaderBytes;

bool sameAudio (const std::vector<char>& a, const std::vector<char>& b)
{
    return a.size () == b.size () && a.size () > kLoopAudioStart
           && std::equal (a.begin () + kLoopAudioStart, a.end (), b.begin () + kLoopAudioStart);
}

// Starts the song from the top with Rec already pressed, so that recording begins right away.
void startRecordingFromTop (Plug& plug, Song& song)
{
    song.locate (0);
    song.play (true);
    plug.pressButton (kCommandRec);
}

} // namespace

void testFreePlayback ()
{
    Plug plug;
    plug.looperOnly ();
    recordFree (plug, 1000, 0.25, 2.0);

    // There's no transport here at all: the tape runs by itself.
    Samples out;
    plug.runMono (silence (4.0), out);
    const double level = rms (out) / 0.25 * std::sqrt (2.0);
    const double frequency = measureFrequency (out);
    std::printf ("  level %.4f of the input, %.2f Hz\n", level, frequency);
    check ("replays the recording at the right level and pitch", std::abs (db (level)) < 0.1 && std::abs (frequency - 1000.0) < 1.0);

    const double distortion = std::max (toneLevel (out, 3000), toneLevel (out, 5000)) / toneLevel (out, 1000);
    std::printf ("  3rd and 5th harmonics: %.1f dB\n", db (distortion));
    check ("the tape adds no audible distortion", db (distortion) < -60.0);
}

void testFreeExactTiming ()
{
    Plug plug;
    plug.looperOnly ();
    plug.set (kLooperFreeLength, 1.0);
    plug.settle (0.8);

    plug.pressButton (kCommandRec);
    auto in = silence (0.9);
    const size_t click = 20000;
    in[click] = 0.8f;
    Samples out;
    plug.runMono (in, out);
    plug.pressButton (kCommandRec);
    plug.runMono (silence (0.8), out);

    // One second later, plus the plug-in's 96 samples of latency.
    const size_t expected = click + 48000 + 96 - in.size ();
    const size_t found = findPeakIndex (out);
    std::printf ("  click expected at %zu, found at %zu\n", expected, found);
    check ("a click replays on the exact sample", found == expected);
}

void testFreeLengthRepitches ()
{
    const std::pair<double, double> cases[] = { { 4.0, 500.0 }, { 1.0, 2000.0 }, { 0.5, 4000.0 }, { 2.0, 1000.0 }, { 10.0, 200.0 }, { 30.0, 66.667 } };
    for (const auto& [length, expected] : cases)
    {
        Plug plug;
        plug.looperOnly ();
        recordFree (plug, 1000, 0.25, 2.0);
        plug.set (kLooperFreeLength, length);
        plug.settle (0.3);

        Samples out;
        plug.runMono (silence (2.0), out);
        const double frequency = measureFrequency (out);
        std::printf ("  length %4.1f s: %.2f Hz, expected %.0f Hz\n", length, frequency, expected);
        check ("the pitch follows the length", std::abs (frequency - expected) < expected * 0.01);
    }
}

void testFreeEachLength ()
{
    for (double length : { 0.5, 1.0, 3.3, 10.0, 30.0 })
    {
        Plug plug;
        plug.looperOnly ();
        recordFree (plug, 1000, 0.25, length);

        Samples out;
        plug.runMono (silence (std::max (1.0, length)), out);
        const double frequency = measureFrequency (out);
        const double level = rms (out) * std::sqrt (2.0) / 0.25;
        std::printf ("  length %4.1f s: %.2f Hz, level %.4f\n", length, frequency, level);
        check ("keeps its pitch and level", std::abs (frequency - 1000.0) < 2.0 && std::abs (db (level)) < 0.2);
    }
}

void testOverdub ()
{
    Plug plug;
    plug.looperOnly ();
    recordFree (plug, 500, 0.2, 2.0);

    plug.pressButton (kCommandOverdub);
    Samples out;
    plug.runMono (sine (3000, 0.1, 2.1), out);
    plug.pressButton (kCommandOverdub);
    plug.settle (0.05);

    plug.runMono (silence (2.0), out);
    const double first = toneLevel (out, 500);
    const double second = toneLevel (out, 3000);
    std::printf ("  first layer %.4f, second layer %.4f\n", first, second);
    check ("both layers play at their own level", std::abs (first - 0.2) < 0.005 && std::abs (second - 0.1) < 0.005);
}

void testFeedbackUnity ()
{
    Plug plug;
    plug.looperOnly ();
    recordFree (plug, 700, 0.3, 1.0);
    const auto before = plug.getStateBytes ();

    Samples out;
    plug.runMono (silence (5.0), out);
    check ("the loop is bit-exact after 5 passes", sameAudio (before, plug.getStateBytes ()));

    plug.pressButton (kCommandOverdub);
    plug.runMono (silence (2.2), out);
    plug.pressButton (kCommandOverdub);
    plug.settle (0.05);
    check ("the loop is bit-exact after overdubbing silence", sameAudio (before, plug.getStateBytes ()));

    // The post saturator colours the output, but must never reach the loop.
    plug.set (kPostSatOn, 1);
    plug.set (kPostSatMode, 0);
    plug.set (kPostSatDrive, 30);
    plug.settle (0.05);
    plug.runMono (silence (2.5), out);
    check ("the post saturator colours the output", toneLevel (out, 2100, 4800) > toneLevel (out, 700, 4800) * 0.1);
    check ("the post saturator doesn't change the stored loop", sameAudio (before, plug.getStateBytes ()));
}

void testFeedbackBelowUnity ()
{
    Plug plug;
    plug.looperOnly ();
    recordFree (plug, 1000, 0.5, 1.0);
    plug.set (kLooperFeedback, 50);
    plug.settle (0.05);

    Samples out;
    plug.runMono (silence (1.0), out);
    const double first = rms (slice (out, 24000, 19200));
    plug.runMono (silence (1.0), out);
    const double second = rms (slice (out, 24000, 19200));
    std::printf ("  change over one pass at 50%%: %.2f dB\n", db (second / first));
    check ("the level drops by 6 dB per pass", std::abs (db (second / first) + 6.02) < 0.3);
}

void testFeedbackAboveUnity ()
{
    Plug plug;
    plug.looperOnly ();
    recordFree (plug, 1000, 0.05, 0.5);
    plug.set (kLooperFeedback, 200);
    plug.settle (0.05);

    Samples out;
    double highest = 0.0;
    double firstLevel = 0.0;
    bool finite = true;
    for (int pass = 0; pass < 60; ++pass)
    {
        plug.runMono (silence (0.5), out);
        for (float sample : out)
            finite = finite && std::isfinite (sample);
        highest = std::max (highest, peak (out));
        if (pass == 0)
            firstLevel = rms (out);
    }

    double offset = 0.0;
    for (float sample : out)
        offset += sample;
    offset /= static_cast<double> (out.size ());
    const double distortion = toneLevel (out, 3000) / std::max (toneLevel (out, 1000), 1e-9);

    std::printf ("  level %.3f rising to %.3f, peak %.3f, DC %+.5f, 3rd harmonic %.1f dB\n", firstLevel, rms (out), highest, offset, db (distortion));
    check ("no invalid values", finite);
    check ("pinned below the limiter's ceiling", highest < 1.55);
    check ("the level grows and the loop distorts", rms (out) > firstLevel * 3.0 && distortion > 0.01);
    check ("no DC build-up", std::abs (offset) < 0.01);
}

void testFeedbackKeepsLowEnd ()
{
    // Above 100%, a filter stops offsets from building up. It runs per cell of tape, so a larger tape
    // would raise its corner and thin out the loop if it weren't adjusted. With the corner at 25 Hz
    // for this length, a quiet 40 Hz tone at 150% grows by about 2 dB per pass. At 75 Hz, it would fade.
    Plug plug;
    plug.looperOnly ();
    recordFree (plug, 40, 0.02, 2.0);
    plug.set (kLooperFeedback, 150);
    plug.settle (0.05);

    Samples out;
    plug.runMono (silence (2.0), out);
    const double first = rms (out);
    for (int pass = 0; pass < 4; ++pass)
        plug.runMono (silence (2.0), out);
    const double perPass = db (rms (out) / first) / 4.0;
    std::printf ("  change per pass at 150%%: %.2f dB\n", perPass);
    check ("low frequencies keep their place above 100% feedback", std::abs (perPass - 2.1) < 0.5);
}

void testDryWet ()
{
    const auto in = sine (440, 0.2, 2.2);
    const std::tuple<double, double, double> cases[] = { { 0.0, 1.0, 0.0 }, { 50.0, 1.0, 1.0 }, { 100.0, 0.0, 1.0 } };
    for (const auto& [mix, expectedDry, expectedWet] : cases)
    {
        Plug plug;
        plug.neutral ();
        recordFree (plug, 3000, 0.1, 1.0);
        plug.set (kLooperDryWet, mix);
        plug.settle (0.1);

        Samples out;
        plug.runMono (in, out);
        const double dry = toneLevel (out, 440, 4800) / 0.2;
        const double wet = toneLevel (out, 3000, 4800) / 0.1;
        std::printf ("  %3.0f%%: dry %.3f, wet %.3f\n", mix, dry, wet);
        check ("the levels match the control", std::abs (dry - expectedDry) < 0.01 && std::abs (wet - expectedWet) < 0.01);
    }
}

void testClear ()
{
    Plug plug;
    plug.looperOnly ();
    recordFree (plug, 1000, 0.5, 1.0);

    plug.pressButton (kCommandClear);
    plug.settle (0.3);
    Samples out;
    plug.runMono (silence (1.5), out);
    check ("silent after Clear", rms (out) < 1e-6);
    check ("no loop is saved after Clear", plug.getStateBytes ().size () == kNoLoopStateBytes);
}

void testRecMovesToOverdub ()
{
    {
        Plug plug;
        plug.looperOnly ();
        plug.set (kLooperFreeLength, 1.0);
        plug.settle (0.8);
        plug.pressButton (kCommandRec);

        Samples out;
        plug.runMono (sine (500, 0.2, 1.02), out);
        // No button pressed: this second tone should layer on top, not replace the first.
        plug.runMono (sine (3000, 0.1, 1.05), out);
        plug.pressButton (kCommandOverdub);
        plug.settle (0.05);

        plug.runMono (silence (2.0), out);
        const double first = toneLevel (out, 500);
        const double second = toneLevel (out, 3000);
        std::printf ("  first pass %.4f, second pass %.4f\n", first, second);
        check ("the second pass layers on top of the first", std::abs (first - 0.2) < 0.01 && std::abs (second - 0.1) < 0.01);
    }
    {
        Plug plug;
        plug.looperOnly ();
        plug.set (kLooperFreeLength, 1.0);
        plug.settle (0.8);
        plug.pressButton (kCommandRec);

        Samples out;
        plug.runMono (sine (500, 0.2, 0.5), out);
        plug.pressButton (kCommandRec);
        plug.runMono (sine (3000, 0.1, 2.0), out);
        plug.runMono (silence (1.0), out);
        check ("pressing Rec before the pass ends doesn't start overdubbing", toneLevel (out, 3000) < 0.001);
    }
}

void testFreeStopAndPlay ()
{
    Plug plug;
    plug.looperOnly ();
    plug.set (kLooperFreeLength, 1.0);
    plug.settle (0.8);

    // On an empty tape, the loop starts where the recording starts.
    plug.pressButton (kCommandRec);
    auto in = silence (0.9);
    const size_t click = 20000;
    in[click] = 0.8f;
    Samples out;
    plug.runMono (in, out);
    plug.pressButton (kCommandRec);
    plug.runMono (silence (0.3), out);
    const auto before = plug.getStateBytes ();

    plug.pressButton (kCommandStop);
    plug.set (kLooperFeedback, 50);
    plug.runMono (silence (1.5), out);
    check ("Stop silences the loop", rms (slice (out, 1000, 40000)) < 1e-6);
    check ("a stopped tape isn't changed, even with feedback below 100%", sameAudio (before, plug.getStateBytes ()));
    plug.set (kLooperFeedback, 100);
    plug.settle (0.1);

    plug.pressButton (kCommandPlay);
    plug.runMono (silence (0.9), out);
    const auto clicks = findClicks (out);
    std::printf ("  after Play, click at %zu, expected %zu\n", clicks.empty () ? 0 : clicks[0], click + 96);
    check ("Play relaunches from the start of the loop", clicks.size () == 1 && clicks[0] == click + 96);

    plug.pressButton (kCommandStop);
    plug.settle (0.05);
    plug.pressButton (kCommandRec);
    plug.runMono (sine (3000, 0.1, 0.5), out);
    plug.pressButton (kCommandRec);
    plug.runMono (silence (0.5), out);
    plug.runMono (silence (0.6), out);
    check ("Rec restarts a stopped tape and records", rms (out) > 0.01);
}

void testSyncGrid ()
{
    Plug plug;
    useSyncMode (plug);
    plug.set (kLooperBars, 1); // 1 bar
    Song song (plug);
    song.locate (0);
    song.play (true);

    const size_t beat = song.samplesFor (1);
    Samples in (song.samplesFor (24), 0.0f);
    in[song.samplesFor (3.25)] = 0.9f; // Before the recording, so it should never come back.
    in[song.samplesFor (5.5)] = 0.9f;  // In the recorded bar.
    in[song.samplesFor (8.5)] = 0.9f;  // After the recording, which is overdubbing by then.

    // Rec partway through the first bar: recording should wait for the bar line at quarter 4.
    const Samples first (in.begin (), in.begin () + static_cast<std::ptrdiff_t> (beat * 2));
    const Samples rest (in.begin () + static_cast<std::ptrdiff_t> (beat * 2), in.end ());
    Samples out;
    plug.runMono (first, out);
    plug.pressButton (kCommandRec);
    plug.runMono (rest, out);

    std::printf ("  clicks at, in quarter notes:");
    std::vector<size_t> positions;
    for (size_t found : findClicks (out))
    {
        positions.push_back (found + beat * 2);
        std::printf (" %.4f", static_cast<double> (positions.back ()) / static_cast<double> (beat));
    }
    std::printf ("\n");

    // The click at 5.5 repeats from 9.5, and the overdubbed one at 8.5 repeats from 12.5.
    std::vector<size_t> expected;
    for (double quarters : { 9.5, 12.5, 13.5, 16.5, 17.5, 20.5, 21.5 })
        expected.push_back (song.samplesFor (quarters) + 96);
    check ("clicks repeat on the exact sample each bar, and overdubbing follows the recording", positions == expected);
}

void testSyncLengths ()
{
    const std::tuple<int, int, int, int, double, const char*> cases[] = {
        { kBarsHalf, 0, 4, 4, 2.0, "1/2 bar in 4/4" },
        { kBarsTwo, 0, 4, 4, 8.0, "2 bars in 4/4" },
        { kBarsFour, 0, 3, 4, 12.0, "4 bars in 3/4" },
        { kBarsSix, 0, 4, 4, 24.0, "6 bars in 4/4" },
        { kBarsEight, 0, 6, 8, 24.0, "8 bars in 6/8" },
        { kBarsCustom, 3, 4, 4, 12.0, "custom 3 bars in 4/4" }
    };

    for (const auto& [choice, customBars, numerator, denominator, quarters, name] : cases)
    {
        Plug plug;
        useSyncMode (plug);
        plug.set (kLooperBars, choice);
        if (customBars > 0)
            plug.set (kLooperCustomBars, customBars);
        Song song (plug, 240.0, numerator, denominator);
        startRecordingFromTop (plug, song);

        const double bar = numerator * 4.0 / denominator;
        Samples in (song.samplesFor (quarters * 3.0 + bar + 1.0), 0.0f);
        in[song.samplesFor (0.5)] = 0.9f;
        Samples out;
        plug.runMono (in, out);

        const auto clicks = findClicks (out);
        const size_t period = song.samplesFor (quarters);
        bool ok = clicks.size () >= 2;
        for (size_t i = 1; ok && i < clicks.size (); ++i)
            ok = clicks[i] - clicks[i - 1] == period;
        std::printf ("  %s: %zu repeats\n", name, clicks.size ());
        check (name, ok);
    }
}

void testSyncOpenEnded ()
{
    Plug plug;
    useSyncMode (plug);
    plug.set (kLooperBars, kBarsOpen);
    Song song (plug, 240.0);
    startRecordingFromTop (plug, song);

    const size_t bar = song.samplesFor (4);
    Samples in (bar * 2 + bar / 2, 0.0f);
    in[1000] = 0.9f;
    Samples out;
    plug.runMono (in, out); // Two and a half bars in.
    plug.pressButton (kCommandRec);
    plug.runMono (silence (10.0), out);

    const auto clicks = findClicks (out);
    const size_t period = clicks.size () >= 2 ? clicks[1] - clicks[0] : 0;
    std::printf ("  loop length: %.3f bars\n", static_cast<double> (period) / static_cast<double> (bar));
    check ("closes at the next bar line, making 3 whole bars", period == bar * 3);
}

void testSync64Bars ()
{
    Plug plug;
    useSyncMode (plug);
    plug.set (kLooperBars, kBarsOpen);
    Song song (plug, 480.0);
    startRecordingFromTop (plug, song);

    const size_t bar = song.samplesFor (4);
    Samples in (bar * 66, 0.0f);
    in[1000] = 0.9f;
    Samples out;
    plug.runMono (in, out);

    const auto clicks = findClicks (out);
    std::printf ("  first repeat at %.3f bars\n", clicks.empty () ? 0.0 : static_cast<double> (clicks[0]) / static_cast<double> (bar));
    check ("the loop closes at 64 bars and replays", clicks.size () == 1 && clicks[0] == bar * 64 + 1000 + 96);
}

void testSyncTransport ()
{
    Plug plug;
    useSyncMode (plug);
    plug.set (kLooperBars, kBarsOne);
    Song song (plug);
    startRecordingFromTop (plug, song);

    const size_t bar = song.samplesFor (4);
    const size_t click = 12000;
    Samples in (bar + 4800, 0.0f);
    in[click] = 0.9f;
    Samples out;
    plug.runMono (in, out);

    song.play (false);
    plug.runMono (silence (1.0), out);
    check ("silent while the transport is stopped", rms (slice (out, 4800, 40000)) < 1e-6);

    song.locate (40.2);
    song.play (true);
    plug.runMono (silence (1.0), out);
    auto clicks = findClicks (out);
    const size_t expected = click + 96 - song.samplesFor (0.2);
    std::printf ("  after a locate, click at %zu, expected %zu\n", clicks.empty () ? 0 : clicks[0], expected);
    check ("resumes at the right place in the loop", clicks.size () == 1 && clicks[0] == expected);

    plug.pressButton (kCommandStop);
    plug.runMono (silence (0.1), out);
    plug.pressButton (kCommandPlay);
    const double now = song.context.projectTimeMusic;
    const size_t untilBar = song.samplesFor (std::ceil (now / 4.0) * 4.0 - now);
    plug.runMono (silence (static_cast<double> (untilBar + bar) / kSampleRate), out);
    clicks = findClicks (out);
    std::printf ("  next bar in %zu samples, first click at %zu\n", untilBar, clicks.empty () ? 0 : clicks[0]);
    check ("Play waits for the next bar line", !clicks.empty () && clicks[0] == untilBar + click + 96);
}

void testSyncTempoRepitches ()
{
    for (double tempo : { 60.0, 180.0, 240.0 })
    {
        Plug plug;
        useSyncMode (plug);
        plug.set (kLooperBars, kBarsOne);
        Song song (plug);
        startRecordingFromTop (plug, song);

        Samples out;
        plug.runMono (sine (1000, 0.25, 2.1), out);
        song.context.tempo = tempo;
        plug.runMono (silence (0.1), out);
        plug.runMono (silence (1.0), out);

        const double frequency = measureFrequency (out);
        const double expected = 1000.0 * tempo / 120.0;
        std::printf ("  %3.0f BPM: %.2f Hz, expected %.0f Hz\n", tempo, frequency, expected);
        check ("the pitch follows the tempo", std::abs (frequency - expected) < expected * 0.01);
    }
}

void testSyncSeam ()
{
    Plug plug;
    useSyncMode (plug);
    plug.set (kLooperBars, kBarsOne);
    Song song (plug);
    startRecordingFromTop (plug, song);

    // Not a whole number of cycles in the bar, so the seam can't line up by itself. Rec moves on to
    // overdubbing at the seam, so the tone then fades out to avoid recording an abrupt end.
    const double hz = 1033.3;
    const double amplitude = 0.5;
    auto in = sine (hz, amplitude, 2.2);
    const auto fadeStart = static_cast<size_t> (2.01 * kSampleRate);
    const auto fadeEnd = static_cast<size_t> (2.05 * kSampleRate);
    for (size_t i = fadeStart; i < in.size (); ++i)
        in[i] *= i < fadeEnd ? static_cast<float> (fadeEnd - i) / static_cast<float> (fadeEnd - fadeStart) : 0.0f;

    Samples out;
    plug.runMono (in, out);
    plug.pressButton (kCommandOverdub);
    plug.runMono (silence (2.5), out);

    double largestStep = 0.0;
    for (size_t i = 1; i < out.size (); ++i)
        largestStep = std::max (largestStep, static_cast<double> (std::abs (out[i] - out[i - 1])));
    const double toneStep = amplitude * 2.0 * 3.14159265 * hz / kSampleRate;
    std::printf ("  largest step %.4f, this tone's normal step %.4f\n", largestStep, toneStep);

    // The tone overlaps itself for a moment, which can double the step. A click would be far larger.
    check ("the seam doesn't click", largestStep < toneStep * 2.1);
}

void testSyncOverdub ()
{
    Plug plug;
    useSyncMode (plug);
    plug.set (kLooperBars, kBarsOne);
    Song song (plug);
    startRecordingFromTop (plug, song);

    Samples out;
    plug.runMono (sine (500, 0.2, 2.0), out);

    // Rec has moved on to overdubbing. This turns it off, then on again partway through the bar.
    plug.runMono (silence (0.3), out);
    plug.pressButton (kCommandOverdub);
    plug.runMono (silence (0.1), out);
    plug.pressButton (kCommandOverdub);
    plug.runMono (sine (3000, 0.1, 2.2), out);
    plug.pressButton (kCommandOverdub);
    plug.settle (0.05);

    plug.runMono (silence (2.0), out);
    const double first = toneLevel (out, 500);
    const double second = toneLevel (out, 3000);
    std::printf ("  layers %.4f and %.4f\n", first, second);
    check ("overdub adds a layer from any position", std::abs (first - 0.2) < 0.01 && std::abs (second - 0.1) < 0.01);
}

void testSyncLocateCancels ()
{
    Plug plug;
    useSyncMode (plug);
    plug.set (kLooperBars, kBarsTwo);
    Song song (plug);
    startRecordingFromTop (plug, song);

    Samples out;
    plug.runMono (sine (500, 0.2, 1.0), out);
    song.locate (100);
    plug.runMono (silence (5.0), out);
    check ("a locate during the first pass cancels the recording", rms (out) < 1e-6);
    check ("no loop is left behind", plug.getStateBytes ().size () == kNoLoopStateBytes);
}

void testSavedLoops ()
{
    {
        Plug saved;
        saved.looperOnly ();
        recordFree (saved, 1000, 0.25, 2.0);

        MemoryStream stream;
        check ("saves a free running loop", saved.component->getState (&stream) == kResultOk);
        std::printf ("  saved %.2f MB\n", static_cast<double> (stream.getSize ()) / 1e6);

        for (double sampleRate : { 48000.0, 96000.0 })
        {
            Plug restored (sampleRate);
            stream.seek (0, IBStream::kIBSeekSet, nullptr);
            check ("setState", restored.component->setState (&stream) == kResultOk);
            stream.seek (0, IBStream::kIBSeekSet, nullptr);
            check ("setComponentState", restored.controller->setComponentState (&stream) == kResultOk);

            const Samples zero (static_cast<size_t> (sampleRate * 3.0), 0.0f);
            Samples out;
            restored.runMono (zero, out);
            const auto tail = slice (out, static_cast<size_t> (sampleRate * 0.5), static_cast<size_t> (sampleRate * 2.0));
            const double frequency = measureFrequency (tail, sampleRate);
            const double level = rms (tail) * std::sqrt (2.0);
            std::printf ("  restored at %.0f Hz: %.2f Hz, level %.3f\n", sampleRate, frequency, level);
            check ("plays at the same pitch and level", std::abs (frequency - 1000.0) < 2.0 && std::abs (level - 0.25) < 0.01);

            MemoryStream savedAgain;
            restored.component->getState (&savedAgain);
            check ("saving it again keeps it", savedAgain.getSize () == stream.getSize ());
        }
    }
    {
        Plug saved;
        useSyncMode (saved);
        saved.set (kLooperBars, kBarsOne);
        Song song (saved);
        startRecordingFromTop (saved, song);

        const size_t click = 12000;
        Samples in (song.samplesFor (4) + 4800, 0.0f);
        in[click] = 0.9f;
        Samples out;
        saved.runMono (in, out);

        MemoryStream stream;
        saved.component->getState (&stream);
        std::printf ("  saved %.2f MB\n", static_cast<double> (stream.getSize ()) / 1e6);

        Plug restored;
        Song restoredSong (restored);
        stream.seek (0, IBStream::kIBSeekSet, nullptr);
        check ("setState", restored.component->setState (&stream) == kResultOk);
        restoredSong.locate (8);
        restoredSong.play (true);
        restored.runMono (silence (2.1), out);

        // The restored instance's position starts slightly differently, so this allows a little
        // room. What it guards against is the loop losing its place on the grid.
        const auto clicks = findClicks (out);
        std::printf ("  restored: %zu click(s), first at %zu\n", clicks.size (), clicks.empty () ? 0 : clicks[0]);
        check ("the restored loop stays on the same grid", clicks.size () == 1 && std::abs (static_cast<long long> (clicks[0]) - static_cast<long long> (click + 96)) < 300);
    }
}

} // namespace Tests
} // namespace LoopTexturer
