// Usage: LoopTexturerTests <path to the plug-in's DLL> [test name]
//        LoopTexturerTests --list
//
// With no test name, everything runs except "cpu", which only reports a measurement.

#include "Harness.h"
#include "Tests.h"

#include <cstdio>
#include <cwchar>

using namespace LoopTexturer::Tests;

namespace {

struct Test
{
    const wchar_t* name;
    void (*function) ();
};

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

    { L"panel", testPanel },
};

} // namespace

int wmain (int argc, wchar_t** argv)
{
    if (argc >= 2 && std::wcscmp (argv[1], L"--list") == 0)
    {
        for (const auto& test : kTests)
            std::wprintf (L"%ls\n", test.name);
        std::wprintf (L"cpu\n");
        return 0;
    }

    if (argc < 2)
    {
        std::printf ("Usage: LoopTexturerTests <path to the plug-in's DLL> [test name]\n");
        return 2;
    }

    useTemporaryExportFolder ();

    if (!loadPlugin (argv[1]))
    {
        std::wprintf (L"Could not load the plug-in: %ls\n", argv[1]);
        removeTemporaryExportFolder ();
        return 2;
    }

    const wchar_t* filter = argc >= 3 ? argv[2] : nullptr;
    bool foundTest = false;

    if (filter && std::wcscmp (filter, L"cpu") == 0)
    {
        foundTest = true;
        measureCpu ();
    }

    for (const auto& test : kTests)
    {
        if (filter && std::wcscmp (filter, test.name) != 0)
            continue;

        foundTest = true;
        std::wprintf (L"[%ls]\n", test.name);
        std::fflush (stdout);
        test.function ();
    }

    unloadPlugin ();
    removeTemporaryExportFolder ();

    if (!foundTest)
    {
        std::wprintf (L"No test named %ls. Use --list to see them.\n", filter);
        return 2;
    }

    std::printf ("\n%d failure(s)\n", getFailureCount ());
    return getFailureCount () == 0 ? 0 : 1;
}
