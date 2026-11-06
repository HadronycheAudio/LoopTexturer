// Usage: LoopTexturerTests <path to the plug-in's DLL> [test name]
//        LoopTexturerTests --list
//
// With no test name, everything runs except "cpu", which only reports a measurement.

#include "Harness.h"
#include "Tests.h"

#include "hadronyche/testhost/Runner.h"

#include <iterator>

using namespace LoopTexturer::Tests;

namespace {

const Test kTests[] = {
    { L"class-ids", testClassIds },
    { L"parameter-ids", testParameterIds },
    { L"parameter-ranges", testParameterRanges },
    { L"parameter-labels", testParameterLabels },
    { L"saved-format", testSavedFormat },
    { L"older-project", testOlderProject },
    { L"older-length", testOlderLength },
    { L"older-tape-replaced", testOlderTapeReplaced },

    { L"latency", testLatency },
    { L"gain-volume", testGainAndVolume },
    { L"pan", testPan },
    { L"lfo", testLfos },
    { L"filter", testFilter },
    { L"saturator", testSaturators },
    { L"aliasing", testAliasing },
    { L"broken-input", testBrokenInput },
    { L"extreme-settings", testExtremeSettings },
    { L"parameter-state", testParameterState },
    { L"parameter-text", testParameterText },

    { L"free-playback", testFreePlayback },
    { L"free-exact-timing", testFreeExactTiming },
    { L"free-length-repitches", testFreeLengthRepitches },
    { L"free-each-length", testFreeEachLength },
    { L"overdub", testOverdub },
    { L"feedback-unity", testFeedbackUnity },
    { L"feedback-below-unity", testFeedbackBelowUnity },
    { L"feedback-above-unity", testFeedbackAboveUnity },
    { L"feedback-keeps-low-end", testFeedbackKeepsLowEnd },
    { L"dry-wet", testDryWet },
    { L"clear", testClear },
    { L"rec-moves-to-overdub", testRecMovesToOverdub },
    { L"free-stop-play", testFreeStopAndPlay },

    { L"sync-grid", testSyncGrid },
    { L"sync-lengths", testSyncLengths },
    { L"sync-open-ended", testSyncOpenEnded },
    { L"sync-64-bars", testSync64Bars },
    { L"sync-transport", testSyncTransport },
    { L"sync-tempo-repitches", testSyncTempoRepitches },
    { L"sync-seam", testSyncSeam },
    { L"sync-overdub", testSyncOverdub },
    { L"sync-locate-cancels", testSyncLocateCancels },
    { L"saved-loops", testSavedLoops },

#if defined(_WIN32)
    { L"panel", testPanel },
#endif
};

} // namespace

HADRONYCHE_TEST_MAIN
{
#if defined(_WIN32)
    return runTests (argc, argv, { "LoopTexturerTests", kTests, std::size (kTests), measureCpu, useTemporaryExportFolder, removeTemporaryExportFolder });
#else
    return runTests (argc, argv, { "LoopTexturerTests", kTests, std::size (kTests), measureCpu });
#endif
}
