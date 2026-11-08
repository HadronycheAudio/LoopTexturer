// The looper's part of the LV2 version, which leaves the VST3 classes as they are.
//
// The VST3 panel reads LooperBridge straight from the processor's memory, which LV2 can't promise, since a host can
// run the UI in another process. So the audio side keeps the processor's introduction to itself, and sends the panel
// copies of the looper's status and of the tape's overview instead, through the wrapper's message ports. The panel side
// builds a mirror LooperBridge of its own from those, on the Rust side, and introduces that to the controller with the
// processor's own message, so that Controller and Editor work unchanged. The buttons' messages go back to the processor
// unchanged.

#include "LooperBridge.h"
#include "Messages.h"

#include "hadronyche/lv2/Message.h"
#include "hadronyche/lv2/MessageLink.h"

#include "pluginterfaces/base/funknown.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using namespace Steinberg;
using Hadronyche::Lv2::Message;
using Hadronyche::Lv2::MessageSender;

namespace LoopTexturer {

namespace {

// Audio side to panel: StatusCopy, in "status".
constexpr const char* kMessageStatus = "LooperLv2Status";
constexpr const char* kAttributeStatus = "status";

// Audio side to panel: a run of the tape's overview buckets, from "first", as a low and a high float each, in "peaks".
constexpr const char* kMessageWave = "LooperLv2Wave";
constexpr const char* kAttributeFirst = "first";
constexpr const char* kAttributePeaks = "peaks";

// Panel to audio side: a panel opened, and needs the whole waveform.
constexpr const char* kMessageHello = "LooperLv2Hello";

// The copy has the tape's own overview buckets, one for one, so that its columns cover what the VST3 panel's do. Its
// tape holds one frame per bucket, and shows as many frames as the real one shows buckets.
constexpr int64_t kWaveBuckets = kOverviewBuckets;
constexpr int64_t kChunkBuckets = 256;
constexpr int64_t kChunkCount = kWaveBuckets / kChunkBuckets;

// So that a whole waveform, about 256 kB, goes over a few ticks rather than at once.
constexpr int kMaxChunksPerTick = 32;


// Both sides are the same binary, so this goes as it is, and a different size means it isn't one of these.
struct StatusCopy
{
    enum Flag : uint32_t
    {
        kSyncMode = 1 << 0,
        kHasContent = 1 << 1,
        kRecording = 1 << 2,
        kOverdubbing = 1 << 3,
        kArmed = 1 << 4,
        kClosing = 1 << 5,
        kPlayArmed = 1 << 6,
        kPlaying = 1 << 7,
        kTransportRunning = 1 << 8,
        kTransportJumped = 1 << 9,
        kHasTape = 1 << 10,
    };

    uint32_t flags;
    uint32_t pad;
    double playhead;       // In the real tape's overview buckets.
    int64_t displayFrames; // The same buckets' worth of the frames shown, rounded.
    int64_t loopFrames;
    double loopQuarters;
    double anchorQuarters;
    double framesPerQuarter;
    double writeSpeed;
    double tempo;
    double sampleRate;
};

class DspLink : public Hadronyche::Lv2::DspLink
{
public:
    bool fromProcessor (Vst::IMessage* message) override
    {
        if (!FIDStringsEqual (message->getMessageID (), kMessageLooperBridge))
            return false;

        // The ID only means something in this process.
        int64 id = 0;
        if (message->getAttributes ()->getInt (kAttributeBridgeId, id) == kResultOk)
            _bridge = LooperBridge::find (id);
        return true;
    }

    bool fromPanel (Vst::IMessage* message, MessageSender&) override
    {
        if (FIDStringsEqual (message->getMessageID (), kMessageHello))
        {
            _sent.fill (false);
            return true;
        }
        return FIDStringsEqual (message->getMessageID (), kMessageLooperBridge);
    }

    void tick (MessageSender& toPanel) override
    {
        if (!_bridge)
            return;

        const LooperStatus status = _bridge->getStatus ();
        const auto tape = _bridge->getActiveTape ();
        const int64_t frames = status.displayFrames;
        const int64_t bucketFrames = tape ? tape->getBucketFrames () : 1;
        const int64_t buckets = tape && frames > 0 ? std::min ((frames + bucketFrames - 1) / bucketFrames, kWaveBuckets) : 0;

        StatusCopy copy {};
        const auto flag = [&copy] (bool isSet, uint32_t bit) {
            if (isSet)
                copy.flags |= bit;
        };
        flag (status.isSyncMode, StatusCopy::kSyncMode);
        flag (status.hasContent, StatusCopy::kHasContent);
        flag (status.isRecording, StatusCopy::kRecording);
        flag (status.isOverdubbing, StatusCopy::kOverdubbing);
        flag (status.isArmed, StatusCopy::kArmed);
        flag (status.isClosing, StatusCopy::kClosing);
        flag (status.isPlayArmed, StatusCopy::kPlayArmed);
        flag (status.isPlaying, StatusCopy::kPlaying);
        flag (status.isTransportRunning, StatusCopy::kTransportRunning);
        flag (status.transportJumped, StatusCopy::kTransportJumped);
        flag (tape != nullptr, StatusCopy::kHasTape);
        copy.playhead = buckets > 0 ? status.playhead / static_cast<double> (bucketFrames) : 0.0;
        copy.displayFrames = buckets > 0 ? std::max<int64_t> (1, (frames + bucketFrames / 2) / bucketFrames) : 0;
        copy.loopFrames = status.loopFrames;
        copy.loopQuarters = status.loopQuarters;
        copy.anchorQuarters = status.anchorQuarters;
        copy.framesPerQuarter = status.framesPerQuarter;
        copy.writeSpeed = status.writeSpeed;
        copy.tempo = status.tempo;
        copy.sampleRate = status.sampleRate;

        if (auto* message = Message::create ())
        {
            message->setMessageID (kMessageStatus);
            message->setBinary (kAttributeStatus, &copy, sizeof (copy));
            toPanel.send (message);
            message->release ();
        }

        for (int64_t bucket = 0; bucket < buckets; ++bucket)
        {
            _peaks[static_cast<size_t> (2 * bucket)] = tape->getOverviewMin (bucket);
            _peaks[static_cast<size_t> (2 * bucket + 1)] = tape->getOverviewMax (bucket);
        }
        sendChanges (toPanel, buckets);
    }

private:
    void sendChanges (MessageSender& toPanel, int64_t buckets)
    {
        const int64_t chunks = (buckets + kChunkBuckets - 1) / kChunkBuckets;
        if (chunks == 0)
            return;

        // One chunk a tick goes whether it changed or not, in case a host dropped it.
        const int64_t refreshed = _ticks++ % chunks;
        int sentNow = 0;
        for (int64_t chunk = 0; chunk < chunks && sentNow < kMaxChunksPerTick; ++chunk)
        {
            const size_t start = static_cast<size_t> (2 * chunk * kChunkBuckets);
            const size_t count = static_cast<size_t> (2 * std::min (kChunkBuckets, buckets - chunk * kChunkBuckets));
            const bool isSame = std::equal (_peaks.begin () + start, _peaks.begin () + start + count, _sentPeaks.begin () + start);
            if (_sent[static_cast<size_t> (chunk)] && isSame && chunk != refreshed)
                continue;
            ++sentNow;

            auto* message = Message::create ();
            message->setMessageID (kMessageWave);
            message->setInt (kAttributeFirst, chunk * kChunkBuckets);
            message->setBinary (kAttributePeaks, _peaks.data () + start, static_cast<uint32> (count * sizeof (float)));
            toPanel.send (message);
            message->release ();

            std::copy (_peaks.begin () + start, _peaks.begin () + start + count, _sentPeaks.begin () + start);
            _sent[static_cast<size_t> (chunk)] = true;
        }
    }

    std::shared_ptr<LooperBridge> _bridge;
    std::vector<float> _peaks = std::vector<float> (2 * kWaveBuckets);
    std::vector<float> _sentPeaks = std::vector<float> (2 * kWaveBuckets);
    std::array<bool, kChunkCount> _sent {};
    int64_t _ticks { 0 };
};

class UiLink : public Hadronyche::Lv2::UiLink
{
public:
    void start (MessageSender& toProcessor) override
    {
        auto* message = Message::create ();
        message->setMessageID (kMessageHello);
        toProcessor.send (message);
        message->release ();
    }

    bool fromDsp (Vst::IMessage* message, Vst::IConnectionPoint& controller) override
    {
        const auto id = message->getMessageID ();
        if (FIDStringsEqual (id, kMessageStatus))
        {
            const void* data = nullptr;
            uint32 size = 0;
            if (message->getAttributes ()->getBinary (kAttributeStatus, data, size) == kResultOk && size == sizeof (StatusCopy))
            {
                StatusCopy copy;
                std::memcpy (&copy, data, sizeof (copy));
                apply (copy, controller);
            }
            return true;
        }
        if (FIDStringsEqual (id, kMessageWave))
        {
            int64 first = 0;
            const void* data = nullptr;
            uint32 size = 0;
            if (_bridge && message->getAttributes ()->getInt (kAttributeFirst, first) == kResultOk
                && message->getAttributes ()->getBinary (kAttributePeaks, data, size) == kResultOk)
                applyPeaks (first, data, size);
            return true;
        }

        // The audio side never passes the processor's own introduction on, and an ID from another process would find
        // nothing here, or the wrong bridge.
        return FIDStringsEqual (id, kMessageLooperBridge);
    }

private:
    void apply (const StatusCopy& copy, Vst::IConnectionPoint& controller)
    {
        if (!_bridge)
        {
            // Introduced only once something arrived, so that a host passing no messages shows the VST3's own text
            // for an unconnected looper.
            _bridge = LooperBridge::createMirror ();

            auto* introduction = Message::create ();
            introduction->setMessageID (kMessageLooperBridge);
            introduction->setInt (kAttributeBridgeId, _bridge->getId ());
            controller.notify (introduction);
            introduction->release ();
        }

        const auto has = [&copy] (uint32_t bit) { return (copy.flags & bit) != 0; };
        LooperStatus status {};
        status.isSyncMode = has (StatusCopy::kSyncMode);
        status.hasContent = has (StatusCopy::kHasContent);
        status.isRecording = has (StatusCopy::kRecording);
        status.isOverdubbing = has (StatusCopy::kOverdubbing);
        status.isArmed = has (StatusCopy::kArmed);
        status.isClosing = has (StatusCopy::kClosing);
        status.isPlayArmed = has (StatusCopy::kPlayArmed);
        status.isPlaying = has (StatusCopy::kPlaying);
        status.isTransportRunning = has (StatusCopy::kTransportRunning);
        status.transportJumped = has (StatusCopy::kTransportJumped);
        status.hasTape = has (StatusCopy::kHasTape);
        status.playhead = copy.playhead;
        status.displayFrames = std::clamp<int64_t> (copy.displayFrames, 0, kWaveBuckets);
        status.loopFrames = copy.loopFrames;
        status.loopQuarters = copy.loopQuarters;
        status.anchorQuarters = copy.anchorQuarters;
        status.framesPerQuarter = copy.framesPerQuarter;
        status.writeSpeed = copy.writeSpeed;
        status.tempo = copy.tempo;
        status.sampleRate = copy.sampleRate;
        _bridge->setStatus (status);
    }

    void applyPeaks (int64 first, const void* data, uint32 size)
    {
        const int64_t count = size / (2 * sizeof (float));
        if (first < 0 || first > kWaveBuckets || count > kWaveBuckets - first)
            return;

        std::vector<float> peaks (static_cast<size_t> (2 * count));
        std::memcpy (peaks.data (), data, peaks.size () * sizeof (float));
        _bridge->setOverview (first, peaks.data (), count);
    }

    std::shared_ptr<LooperBridge> _bridge;
};

} // namespace

} // namespace LoopTexturer

std::unique_ptr<Hadronyche::Lv2::DspLink> Hadronyche::Lv2::createDspLink ()
{
    return std::make_unique<LoopTexturer::DspLink> ();
}

std::unique_ptr<Hadronyche::Lv2::UiLink> Hadronyche::Lv2::createUiLink ()
{
    return std::make_unique<LoopTexturer::UiLink> ();
}
