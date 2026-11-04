#pragma once

#include <cmath>

namespace LoopTexturer {

// One-pole smoothing, which removes clicks and zipper noise from parameter changes.
//
// The state is double precision because in single precision, the steps become too small to register
// before reaching the target, and it stalls about 0.01% short. The looper's speed would show that.
class Smoother
{
public:
    void prepare (double sampleRate, double timeMs)
    {
        _coefficient = std::exp (-1000.0 / (sampleRate * timeMs));
    }

    void setTarget (double target) { _target = target; }

    void snapTo (double value)
    {
        _target = value;
        _current = value;
    }

    float next () { return static_cast<float> (nextPrecise ()); }

    double nextPrecise ()
    {
        _current = _target + (_current - _target) * _coefficient;
        return _current;
    }

private:
    double _coefficient { 0.0 };
    double _target { 0.0 };
    double _current { 0.0 };
};

inline float dbToGain (double db)
{
    return static_cast<float> (std::pow (10.0, db / 20.0));
}

} // namespace LoopTexturer
