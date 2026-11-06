#pragma once

#include "Oversampler.h"

#include "hadronyche/dsp/Smoother.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace LoopTexturer {

using Hadronyche::Smoother;

// A stereo multi-mode saturator with drive and dry/wet mix.
//
// It has a constant latency of kLatency samples, whether it's on or off, so switching it doesn't
// shift the audio.
class Saturator
{
public:
    static constexpr int kLatency = Oversampler::kLatency;

    // The order matches kSatModes in Parameters.cpp.
    enum class Mode
    {
        HardClip,
        Soft,
        SineClip,
        Tube,
        Wavefolder,
        Rectifier
    };

    void prepare (double sampleRate, float drive, float mix)
    {
        for (auto& oversampler : _oversamplers)
            oversampler.reset ();
        for (auto& delay : _dryDelay)
            delay.fill (0.0f);
        _dryPosition = 0;
        _dcInput.fill (0.0f);
        _dcOutput.fill (0.0f);

        // A corner frequency of 5 Hz removes the offset without touching the audible low end.
        _dcCoefficient = static_cast<float> (std::exp (-6.283185307179586 * 5.0 / sampleRate));

        _drive.prepare (sampleRate, 20.0);
        _mix.prepare (sampleRate, 20.0);
        _drive.snapTo (drive);
        _mix.snapTo (mix);
    }

    void setMode (Mode mode) { _mode = mode; }
    void setDrive (float linearGain) { _drive.setTarget (linearGain); }
    void setMix (float mix) { _mix.setTarget (mix); }

    void process (float& left, float& right)
    {
        const float drive = _drive.nextFloat ();
        const float mix = _mix.nextFloat ();

        left = processChannel (left, 0, drive, mix);
        right = processChannel (right, 1, drive, mix);

        _dryPosition = _dryPosition + 1 == kLatency ? 0 : _dryPosition + 1;
    }

private:
    static constexpr float kSilentMix = 1e-5f;

    float processChannel (float input, size_t channel, float drive, float mix)
    {
        auto& delay = _dryDelay[channel];
        const float dry = delay[static_cast<size_t> (_dryPosition)];
        delay[static_cast<size_t> (_dryPosition)] = input;

        // Fully dry, the costly part can be skipped. The latency is unaffected, and the filters'
        // old contents are masked by the mix fading in when the saturator is switched back on.
        if (mix < kSilentMix)
            return dry;

        const auto mode = _mode;
        const float shaped = _oversamplers[channel].process (input, [mode, drive] (float sample) { return shape (mode, sample * drive); });

        // The Tube and Rectifier curves shift the signal off centre, so the offset is removed.
        const float centered = shaped - _dcInput[channel] + _dcCoefficient * _dcOutput[channel];
        _dcInput[channel] = shaped;
        _dcOutput[channel] = centered;

        return dry + (centered - dry) * mix;
    }

    static float shape (Mode mode, float x)
    {
        switch (mode)
        {
            case Mode::HardClip: return std::clamp (x, -1.0f, 1.0f);

            case Mode::Soft: return std::tanh (x);

            case Mode::SineClip: return std::sin (1.570796327f * std::clamp (x, -1.0f, 1.0f));

            case Mode::Tube:
            {
                constexpr float bias = 0.25f;
                return std::tanh (x + bias) - std::tanh (bias);
            }

            case Mode::Wavefolder:
            {
                // A triangle-shaped transfer curve: unchanged up to +/-1, then folded back and forth.
                const float wrapped = x + 1.0f - 4.0f * std::floor ((x + 1.0f) / 4.0f);
                return 1.0f - std::abs (wrapped - 2.0f);
            }

            case Mode::Rectifier: return std::tanh (0.3f * x + 0.7f * std::abs (x));
        }
        return x;
    }

    std::array<Oversampler, 2> _oversamplers;
    std::array<std::array<float, kLatency>, 2> _dryDelay {};
    int _dryPosition { 0 };
    std::array<float, 2> _dcInput {};
    std::array<float, 2> _dcOutput {};
    float _dcCoefficient { 0.999f };
    Smoother _drive;
    Smoother _mix;
    Mode _mode { Mode::Soft };
};

} // namespace LoopTexturer
