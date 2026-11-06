#pragma once

#include "hadronyche/dsp/Gain.h"

namespace LoopTexturer {

// The signal path here is in single precision, and these gains are held in float on their way to it. Going through
// double instead would change the smoothers' targets, and so the sound of saved projects.
inline float dbToGain (double db)
{
    return static_cast<float> (Hadronyche::dbToGain (db));
}

} // namespace LoopTexturer
