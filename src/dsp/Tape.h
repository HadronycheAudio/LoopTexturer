#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>

namespace LoopTexturer {

// Stereo audio storage for the looper, with a peak overview for drawing the waveform.
//
// The audio thread writes while the UI reads, so every value is a relaxed atomic. On x64 that costs
// the same as a plain float.
class Tape
{
public:
    static constexpr int64_t kOverviewBuckets = 32768;

    // Allocating and zeroing can take a while, so this must never run on the audio thread.
    explicit Tape (int64_t frames)
    : _capacity (frames),
      _bucketFrames (std::max<int64_t> (1, (frames + kOverviewBuckets - 1) / kOverviewBuckets)),
      _left (new std::atomic<float>[static_cast<size_t> (frames)] ()),
      _right (new std::atomic<float>[static_cast<size_t> (frames)] ()),
      _overviewMin (new std::atomic<float>[kOverviewBuckets] ()),
      _overviewMax (new std::atomic<float>[kOverviewBuckets] ())
    {
    }

    int64_t getCapacity () const { return _capacity; }
    int64_t getBucketFrames () const { return _bucketFrames; }

    float load (int channel, int64_t index) const
    {
        return channelData (channel)[index].load (std::memory_order_relaxed);
    }

    void store (int channel, int64_t index, float value)
    {
        channelData (channel)[index].store (value, std::memory_order_relaxed);
    }

    // Reads at a fractional position in a loop of the given length. A width of one cell or more
    // averages that many cells, which limits aliasing when playing faster than the audio was recorded.
    float read (int channel, double head, int64_t frames, double width) const
    {
        // Rounding can put the position right on the end of the loop.
        const int64_t first = std::min (static_cast<int64_t> (head), frames - 1);
        if (width < 1.0)
        {
            const auto fraction = static_cast<float> (head - static_cast<double> (first));
            const int64_t next = first + 1 >= frames ? 0 : first + 1;
            const float a = load (channel, first);
            return a + (load (channel, next) - a) * fraction;
        }

        const int count = std::min (static_cast<int> (width) + 1, kMaxAveragedCells);
        float sum = 0.0f;
        int64_t index = first;
        for (int i = 0; i < count; ++i)
        {
            sum += load (channel, index);
            index = index == 0 ? frames - 1 : index - 1;
        }
        return sum / static_cast<float> (count);
    }

    float getOverviewMin (int64_t bucket) const { return _overviewMin[bucket].load (std::memory_order_relaxed); }
    float getOverviewMax (int64_t bucket) const { return _overviewMax[bucket].load (std::memory_order_relaxed); }

    void setOverview (int64_t bucket, float min, float max)
    {
        _overviewMin[bucket].store (min, std::memory_order_relaxed);
        _overviewMax[bucket].store (max, std::memory_order_relaxed);
    }

    void clearOverview ()
    {
        for (int64_t bucket = 0; bucket < kOverviewBuckets; ++bucket)
            setOverview (bucket, 0.0f, 0.0f);
    }

private:
    static constexpr int kMaxAveragedCells = 64;

    const std::atomic<float>* channelData (int channel) const { return channel == 0 ? _left.get () : _right.get (); }
    std::atomic<float>* channelData (int channel) { return channel == 0 ? _left.get () : _right.get (); }

    const int64_t _capacity;
    const int64_t _bucketFrames;
    std::unique_ptr<std::atomic<float>[]> _left;
    std::unique_ptr<std::atomic<float>[]> _right;
    std::unique_ptr<std::atomic<float>[]> _overviewMin;
    std::unique_ptr<std::atomic<float>[]> _overviewMax;
};

} // namespace LoopTexturer
