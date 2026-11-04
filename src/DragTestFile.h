#pragma once

#include <string>

namespace LoopTexturer {

// Creates the test WAV if it doesn't exist yet: 2 seconds of a 440 Hz sine, 48 kHz, 16-bit stereo.
// Returns its full path as UTF-8, or an empty string on failure.
std::string ensureDragTestFile ();

} // namespace LoopTexturer
