#pragma once

#include "hadronyche/dsp/Smoother.h"

#include <cstdint>

namespace LoopTexturer {

using Hadronyche::Smoother;

// A free-running LFO with a bipolar output, from -1 to 1.
class Lfo
{
public:
    // The order matches kLfoWaves in Parameters.cpp.
    enum class Wave
    {
        Sine,
        Triangle,
        Square,
        SampleAndHold
    };

    explicit Lfo (uint32_t randomSeed) : _randomState (randomSeed) {}

    void prepare (double sampleRate)
    {
        _sampleRate = sampleRate;
        _phase = 0.0;
        _held = nextRandom ();
        // Rounds off the jumps of the square and sample & hold waves, which would otherwise click.
        _slew.prepare (sampleRate, 2.0);
        _slew.snapTo (shape ());
    }

    void setRate (double hz) { _increment = hz / _sampleRate; }
    void setWave (Wave wave) { _wave = wave; }

    float next ()
    {
        _phase += _increment;
        if (_phase >= 1.0)
        {
            _phase -= 1.0;
            _held = nextRandom ();
        }

        _slew.setTarget (shape ());
        return _slew.nextFloat ();
    }

private:
    float shape () const
    {
        const auto phase = static_cast<float> (_phase);
        switch (_wave)
        {
            case Wave::Sine: return std::sin (6.283185307f * phase);
            case Wave::Triangle:
                if (phase < 0.25f)
                    return 4.0f * phase;
                return phase < 0.75f ? 2.0f - 4.0f * phase : 4.0f * phase - 4.0f;
            case Wave::Square: return phase < 0.5f ? 1.0f : -1.0f;
            case Wave::SampleAndHold: return _held;
        }
        return 0.0f;
    }

    // xorshift32: fast and safe on the audio thread.
    float nextRandom ()
    {
        _randomState ^= _randomState << 13;
        _randomState ^= _randomState >> 17;
        _randomState ^= _randomState << 5;
        return static_cast<float> (_randomState) / 2147483648.0f - 1.0f;
    }

    Smoother _slew;
    Wave _wave { Wave::Sine };
    double _sampleRate { 44100.0 };
    double _phase { 0.0 };
    double _increment { 0.0 };
    float _held { 0.0f };
    uint32_t _randomState;
};

} // namespace LoopTexturer
