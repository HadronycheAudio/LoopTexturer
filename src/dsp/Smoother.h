#pragma once

#include <cmath>

namespace LoopTexturer {

// One-pole smoothing, which removes clicks and zipper noise from parameter changes.
class Smoother
{
public:
    void prepare (double sampleRate, double timeMs)
    {
        _coefficient = static_cast<float> (std::exp (-1000.0 / (sampleRate * timeMs)));
    }

    void setTarget (float target) { _target = target; }

    void snapTo (float value)
    {
        _target = value;
        _current = value;
    }

    float next ()
    {
        _current = _target + (_current - _target) * _coefficient;
        return _current;
    }

private:
    float _coefficient { 0.0f };
    float _target { 0.0f };
    float _current { 0.0f };
};

inline float dbToGain (double db)
{
    return static_cast<float> (std::pow (10.0, db / 20.0));
}

} // namespace LoopTexturer
