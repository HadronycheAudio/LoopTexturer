#pragma once

#include <array>
#include <cmath>

namespace LoopTexturer {

// Runs a nonlinear function at 4x the sample rate on one channel, using linear-phase FIR filters.
//
// The filters' delay is exactly kLatency samples at the base rate, so a dry signal delayed by
// kLatency stays perfectly aligned with the output.
class Oversampler
{
public:
    static constexpr int kFactor = 4;
    static constexpr int kTaps = 4 * 48 + 1;
    static constexpr int kLatency = (kTaps - 1) / kFactor;

    void reset ()
    {
        _upHistory.fill (0.0f);
        _downHistory.fill (0.0f);
        _upPosition = 0;
        _downPosition = 0;
    }

    template <typename Function>
    float process (float input, Function&& function)
    {
        pushUp (input);

        // Keeping the sample at phase 0 gives a delay of a whole number of base rate samples.
        const float output = pushDownAndFilter (function (filterUp (0)));
        for (int phase = 1; phase < kFactor; ++phase)
            pushDown (function (filterUp (phase)));
        return output;
    }

private:
    static constexpr int kUpLength = kTaps / kFactor + 1;

    // A Kaiser-windowed sinc with its cutoff at the base rate's Nyquist frequency and about 90 dB of
    // stopband attenuation.
    static const std::array<float, kTaps>& getTaps ()
    {
        static const std::array<float, kTaps> taps = [] {
            const auto besselI0 = [] (double x) {
                double sum = 1.0;
                double term = 1.0;
                for (int k = 1; k < 32; ++k)
                {
                    term *= (x / (2.0 * k)) * (x / (2.0 * k));
                    sum += term;
                }
                return sum;
            };

            constexpr double pi = 3.141592653589793;
            constexpr double beta = 8.96;
            constexpr double middle = (kTaps - 1) / 2.0;

            std::array<double, kTaps> window {};
            double sum = 0.0;
            for (int n = 0; n < kTaps; ++n)
            {
                const double t = (n - middle) / middle;
                const double offset = (n - middle) / kFactor;
                const double sinc = offset == 0.0 ? 1.0 : std::sin (pi * offset) / (pi * offset);
                window[static_cast<size_t> (n)] = sinc * besselI0 (beta * std::sqrt (1.0 - t * t)) / besselI0 (beta);
                sum += window[static_cast<size_t> (n)];
            }

            std::array<float, kTaps> result {};
            for (size_t n = 0; n < kTaps; ++n)
                result[n] = static_cast<float> (window[n] / sum);
            return result;
        }();
        return taps;
    }

    void pushUp (float input)
    {
        _upPosition = _upPosition == 0 ? kUpLength - 1 : _upPosition - 1;
        _upHistory[static_cast<size_t> (_upPosition)] = input;
        _upHistory[static_cast<size_t> (_upPosition + kUpLength)] = input;
    }

    float filterUp (int phase) const
    {
        const auto& taps = getTaps ();
        const float* history = _upHistory.data () + _upPosition;
        float sum = 0.0f;
        int k = 0;
        for (int n = phase; n < kTaps; n += kFactor, ++k)
            sum += taps[static_cast<size_t> (n)] * history[k];
        return sum * kFactor;
    }

    void pushDown (float input)
    {
        _downPosition = _downPosition == 0 ? kTaps - 1 : _downPosition - 1;
        _downHistory[static_cast<size_t> (_downPosition)] = input;
        _downHistory[static_cast<size_t> (_downPosition + kTaps)] = input;
    }

    float pushDownAndFilter (float input)
    {
        pushDown (input);

        const auto& taps = getTaps ();
        const float* history = _downHistory.data () + _downPosition;
        float sum = 0.0f;
        for (int n = 0; n < kTaps; ++n)
            sum += taps[static_cast<size_t> (n)] * history[n];
        return sum;
    }

    // Each history is stored twice in a row, so the filters can read it without wrapping around.
    std::array<float, kUpLength * 2> _upHistory {};
    std::array<float, kTaps * 2> _downHistory {};
    int _upPosition { 0 };
    int _downPosition { 0 };
};

} // namespace LoopTexturer
