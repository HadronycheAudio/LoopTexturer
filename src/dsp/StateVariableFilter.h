#pragma once

#include "hadronyche/dsp/Svf.h"

#include <array>
#include <cstddef>

namespace LoopTexturer {

// A stereo topology-preserving state-variable filter, which stays stable under fast modulation.
class StateVariableFilter
{
public:
    // The order matches kFilterModes in Parameters.cpp.
    using Mode = Hadronyche::SvfMode;

    void prepare (double sampleRate)
    {
        _sampleRate = sampleRate;
        _states = {};
    }

    void setMode (Mode mode) { _mode = mode; }

    void setCoefficients (double frequency, double q)
    {
        _coefficients = Hadronyche::svfCoefficients (Hadronyche::svfWarp (frequency, _sampleRate), q);
    }

    float process (float input, size_t channel)
    {
        return Hadronyche::svfOutput (_mode, input, _coefficients.k, Hadronyche::svfTick (input, _coefficients, _states[channel]));
    }

private:
    double _sampleRate { 44100.0 };
    Mode _mode { Mode::LowPass };
    Hadronyche::SvfCoefficients _coefficients;
    std::array<Hadronyche::SvfState, 2> _states {};
};

} // namespace LoopTexturer
