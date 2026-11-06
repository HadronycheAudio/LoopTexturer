#pragma once

namespace LoopTexturer {
namespace Tests {

// Saved project compatibility.
void testClassIds ();
void testParameterIds ();
void testParameterRanges ();
void testParameterLabels ();
void testSavedFormat ();
void testOlderProject ();
void testOlderLength ();
void testOlderTapeReplaced ();

// The effects chain.
void testLatency ();
void testGainAndVolume ();
void testPan ();
void testLfos ();
void testFilter ();
void testSaturators ();
void testAliasing ();
void testBrokenInput ();
void testExtremeSettings ();
void testParameterState ();
void testParameterText ();
void measureCpu ();

// The looper in free running mode.
void testFreePlayback ();
void testFreeExactTiming ();
void testFreeLengthRepitches ();
void testFreeEachLength ();
void testOverdub ();
void testFeedbackUnity ();
void testFeedbackBelowUnity ();
void testFeedbackAboveUnity ();
void testFeedbackKeepsLowEnd ();
void testDryWet ();
void testClear ();
void testRecMovesToOverdub ();
void testFreeStopAndPlay ();

// The looper in tempo sync mode.
void testSyncGrid ();
void testSyncLengths ();
void testSyncOpenEnded ();
void testSync64Bars ();
void testSyncTransport ();
void testSyncTempoRepitches ();
void testSyncSeam ();
void testSyncOverdub ();
void testSyncLocateCancels ();
void testSavedLoops ();

// The real panel, driven with mouse messages.
void testPanel ();

// Keeps exported WAVs out of the real exports folder. Must run before the plug-in is loaded.
void useTemporaryExportFolder ();
void removeTemporaryExportFolder ();

} // namespace Tests
} // namespace LoopTexturer
