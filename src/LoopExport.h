#pragma once

#include "LooperBridge.h"

#include <string>

namespace LoopTexturer {

using LoopExportSettings = Ffi::ExportSettings;

// Writes the loop as heard to a new WAV file: at its current speed and through the post saturator,
// without the dry signal or Volume, which the Rust side renders. Returns the file's path as UTF-8, or an
// empty string on failure.
//
// This reads the whole loop, and can take a few seconds for a long loop with the post saturator on.
std::string exportLoop (const LooperBridge& bridge, const LoopExportSettings& settings);

} // namespace LoopTexturer
