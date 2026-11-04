#pragma once

#include "LooperBridge.h"

#include <string>

namespace LoopTexturer {

struct LoopExportSettings
{
    bool postSaturatorOn;
    int postSaturatorMode;
    double postSaturatorDriveDb;
    double postSaturatorMix; // 0..1
    double freeLengthSeconds;
};

// Writes the loop as heard to a new WAV file: at its current speed and through the post saturator,
// without the dry signal or Volume. Returns the file's path as UTF-8, or an empty string on failure.
//
// This reads the whole loop, and can take a few seconds for a long loop with the post saturator on.
std::string exportLoop (const LooperBridge& bridge, const LoopExportSettings& settings);

} // namespace LoopTexturer
