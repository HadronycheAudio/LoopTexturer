#include "Harness.h"

#include "Messages.h"

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "public.sdk/source/common/memorystream.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

using namespace Steinberg;

DEF_CLASS_IID (Vst::IEditController)
DEF_CLASS_IID (Vst::IComponent)
DEF_CLASS_IID (Vst::IAudioProcessor)
DEF_CLASS_IID (Vst::IConnectionPoint)
DEF_CLASS_IID (Vst::IMessage)
DEF_CLASS_IID (Vst::IAttributeList)
DEF_CLASS_IID (Vst::IHostApplication)
DEF_CLASS_IID (IPlugView)

namespace LoopTexturer {
namespace Tests {

namespace {

constexpr double kPi = 3.14159265358979;

#define TESTS_NO_REFCOUNT                                                                     \
    tresult PLUGIN_API queryInterface (const TUID, void**) override { return kNoInterface; }  \
    uint32 PLUGIN_API addRef () override { return 1; }                                        \
    uint32 PLUGIN_API release () override { return 1; }

class Attributes : public Vst::IAttributeList
{
public:
    TESTS_NO_REFCOUNT

    tresult PLUGIN_API setInt (AttrID id, int64 value) override
    {
        _ints[id] = value;
        return kResultOk;
    }

    tresult PLUGIN_API getInt (AttrID id, int64& value) override
    {
        const auto it = _ints.find (id);
        if (it == _ints.end ())
            return kResultFalse;
        value = it->second;
        return kResultOk;
    }

    tresult PLUGIN_API setFloat (AttrID, double) override { return kResultFalse; }
    tresult PLUGIN_API getFloat (AttrID, double&) override { return kResultFalse; }
    tresult PLUGIN_API setString (AttrID, const Vst::TChar*) override { return kResultFalse; }
    tresult PLUGIN_API getString (AttrID, Vst::TChar*, uint32) override { return kResultFalse; }
    tresult PLUGIN_API setBinary (AttrID, const void*, uint32) override { return kResultFalse; }
    tresult PLUGIN_API getBinary (AttrID, const void*&, uint32&) override { return kResultFalse; }

private:
    std::map<std::string, int64> _ints;
};

class Message : public Vst::IMessage
{
public:
    tresult PLUGIN_API queryInterface (const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef () override { return static_cast<uint32> (++_references); }

    uint32 PLUGIN_API release () override
    {
        const int left = --_references;
        if (left == 0)
            delete this;
        return static_cast<uint32> (left);
    }

    FIDString PLUGIN_API getMessageID () override { return _id.c_str (); }
    void PLUGIN_API setMessageID (FIDString id) override { _id = id; }
    Vst::IAttributeList* PLUGIN_API getAttributes () override { return &_attributes; }

private:
    int _references { 1 };
    std::string _id;
    Attributes _attributes;
};

// What the plug-in needs from a host: creating messages.
class Host : public Vst::IHostApplication
{
public:
    tresult PLUGIN_API queryInterface (const TUID iid, void** object) override
    {
        if (FUnknownPrivate::iidEqual (iid, Vst::IHostApplication::iid) || FUnknownPrivate::iidEqual (iid, FUnknown::iid))
        {
            *object = static_cast<Vst::IHostApplication*> (this);
            return kResultOk;
        }
        *object = nullptr;
        return kNoInterface;
    }

    uint32 PLUGIN_API addRef () override { return 1; }
    uint32 PLUGIN_API release () override { return 1; }

    tresult PLUGIN_API getName (Vst::String128 name) override
    {
        name[0] = 0;
        return kResultOk;
    }

    tresult PLUGIN_API createInstance (TUID classId, TUID, void** object) override
    {
        if (FUnknownPrivate::iidEqual (classId, Vst::IMessage::iid))
        {
            *object = new Message;
            return kResultOk;
        }
        *object = nullptr;
        return kResultFalse;
    }
};

class ValueQueue : public Vst::IParamValueQueue
{
public:
    TESTS_NO_REFCOUNT

    ValueQueue (Vst::ParamID id, double value) : _id (id), _value (value) {}

    Vst::ParamID PLUGIN_API getParameterId () override { return _id; }
    int32 PLUGIN_API getPointCount () override { return 1; }

    tresult PLUGIN_API getPoint (int32, int32& offset, Vst::ParamValue& value) override
    {
        offset = 0;
        value = _value;
        return kResultTrue;
    }

    tresult PLUGIN_API addPoint (int32, Vst::ParamValue, int32&) override { return kResultFalse; }

private:
    Vst::ParamID _id;
    double _value;
};

Host gHost;
HMODULE gModule = nullptr;
IPluginFactory* gFactory = nullptr;
bool (*gExitDll) () = nullptr;
TUID gComponentId;
TUID gControllerId;
int gFailures = 0;

} // namespace

class ParameterChanges : public Vst::IParameterChanges
{
public:
    TESTS_NO_REFCOUNT

    int32 PLUGIN_API getParameterCount () override { return static_cast<int32> (queues.size ()); }
    Vst::IParamValueQueue* PLUGIN_API getParameterData (int32 index) override { return &queues[static_cast<size_t> (index)]; }
    Vst::IParamValueQueue* PLUGIN_API addParameterData (const Vst::ParamID&, int32&) override { return nullptr; }

    std::vector<ValueQueue> queues;
};

bool loadPlugin (const wchar_t* path)
{
    gModule = LoadLibraryW (path);
    if (!gModule)
        return false;

    auto initDll = reinterpret_cast<bool (*) ()> (GetProcAddress (gModule, "InitDll"));
    gExitDll = reinterpret_cast<bool (*) ()> (GetProcAddress (gModule, "ExitDll"));
    auto getPluginFactory = reinterpret_cast<GetFactoryProc> (GetProcAddress (gModule, "GetPluginFactory"));
    if (!initDll || !gExitDll || !getPluginFactory || !initDll ())
        return false;

    gFactory = getPluginFactory ();
    for (int32 i = 0; i < gFactory->countClasses (); ++i)
    {
        PClassInfo info;
        gFactory->getClassInfo (i, &info);
        if (std::strcmp (info.category, kVstAudioEffectClass) == 0)
            std::memcpy (gComponentId, info.cid, sizeof (TUID));
        else if (std::strcmp (info.category, kVstComponentControllerClass) == 0)
            std::memcpy (gControllerId, info.cid, sizeof (TUID));
    }
    return true;
}

void unloadPlugin ()
{
    gFactory->release ();
    gExitDll ();
    FreeLibrary (gModule);
}

IPluginFactory* getFactory ()
{
    return gFactory;
}

void check (const char* what, bool ok)
{
    std::printf ("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++gFailures;
}

int getFailureCount ()
{
    return gFailures;
}

Plug::Plug (double sampleRate) : _changes (new ParameterChanges), _sampleRate (sampleRate)
{
    gFactory->createInstance (gComponentId, Vst::IComponent::iid, reinterpret_cast<void**> (&component));
    component->initialize (&gHost);
    component->queryInterface (Vst::IAudioProcessor::iid, reinterpret_cast<void**> (&processor));

    gFactory->createInstance (gControllerId, Vst::IEditController::iid, reinterpret_cast<void**> (&controller));
    controller->initialize (&gHost);

    component->queryInterface (Vst::IConnectionPoint::iid, reinterpret_cast<void**> (&_componentConnection));
    controller->queryInterface (Vst::IConnectionPoint::iid, reinterpret_cast<void**> (&_controllerConnection));
    _componentConnection->connect (_controllerConnection);
    _controllerConnection->connect (_componentConnection);

    Vst::ProcessSetup setup { Vst::kRealtime, Vst::kSample32, kBlockSize, sampleRate };
    processor->setupProcessing (setup);
    component->activateBus (Vst::kAudio, Vst::kInput, 0, true);
    component->activateBus (Vst::kAudio, Vst::kOutput, 0, true);
    component->setActive (true);
    processor->setProcessing (true);
}

Plug::~Plug ()
{
    processor->setProcessing (false);
    component->setActive (false);
    _componentConnection->disconnect (_controllerConnection);
    _controllerConnection->disconnect (_componentConnection);
    _componentConnection->release ();
    _controllerConnection->release ();
    controller->terminate ();
    controller->release ();
    processor->release ();
    component->terminate ();
    component->release ();
    delete _changes;
}

void Plug::set (ParamId id, double plain)
{
    const double normalized = controller->plainParamToNormalized (id, plain);
    controller->setParamNormalized (id, normalized);
    _changes->queues.emplace_back (id, normalized);
}

void Plug::run (const Samples& left, const Samples& right, Samples& outLeft, Samples& outRight)
{
    const int total = static_cast<int> (left.size ());
    outLeft.resize (left.size ());
    outRight.resize (left.size ());

    for (int position = 0; position < total; position += kBlockSize)
    {
        const int count = std::min (kBlockSize, total - position);
        float* inputs[2] = { const_cast<float*> (&left[static_cast<size_t> (position)]), const_cast<float*> (&right[static_cast<size_t> (position)]) };
        float* outputs[2] = { &outLeft[static_cast<size_t> (position)], &outRight[static_cast<size_t> (position)] };

        Vst::AudioBusBuffers inputBus {};
        inputBus.numChannels = 2;
        inputBus.channelBuffers32 = inputs;
        Vst::AudioBusBuffers outputBus {};
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = outputs;

        Vst::ProcessData data {};
        data.processMode = Vst::kRealtime;
        data.symbolicSampleSize = Vst::kSample32;
        data.numSamples = count;
        data.numInputs = 1;
        data.numOutputs = 1;
        data.inputs = &inputBus;
        data.outputs = &outputBus;
        data.inputParameterChanges = _changes->queues.empty () ? nullptr : _changes;
        data.processContext = context;
        processor->process (data);
        _changes->queues.clear ();

        if (context && (context->state & Vst::ProcessContext::kPlaying))
        {
            context->projectTimeMusic += count * context->tempo / (60.0 * _sampleRate);
            context->projectTimeSamples += count;
            const double bar = context->timeSigNumerator * 4.0 / context->timeSigDenominator;
            context->barPositionMusic = std::floor (context->projectTimeMusic / bar) * bar;
        }
    }
}

void Plug::runMono (const Samples& in, Samples& out)
{
    Samples right;
    run (in, in, out, right);
}

void Plug::settle (double seconds)
{
    const Samples zero (static_cast<size_t> (seconds * _sampleRate), 0.0f);
    Samples left;
    Samples right;
    run (zero, zero, left, right);
}

void Plug::neutral ()
{
    set (kFilterMode, 1);
    set (kFilterFreq, 20);
    set (kSatOn, 0);
    set (kPostSatOn, 0);
}

void Plug::looperOnly ()
{
    neutral ();
    set (kLooperDryWet, 100);
}

void Plug::pressButton (int command)
{
    TUID messageId;
    Vst::IMessage::iid.toTUID (messageId);
    Vst::IMessage* message = nullptr;
    gHost.createInstance (messageId, messageId, reinterpret_cast<void**> (&message));
    message->setMessageID (kMessageLooperCommand);
    message->getAttributes ()->setInt (kAttributeCommand, command);
    _componentConnection->notify (message);
    message->release ();
}

std::vector<char> Plug::getStateBytes ()
{
    MemoryStream stream;
    component->getState (&stream);
    return std::vector<char> (stream.getData (), stream.getData () + stream.getSize ());
}

Song::Song (Plug& plug, double tempo, int numerator, int denominator)
{
    context.state = Vst::ProcessContext::kProjectTimeMusicValid | Vst::ProcessContext::kTempoValid
                    | Vst::ProcessContext::kTimeSigValid | Vst::ProcessContext::kBarPositionValid;
    context.sampleRate = kSampleRate;
    context.tempo = tempo;
    context.timeSigNumerator = numerator;
    context.timeSigDenominator = denominator;
    plug.context = &context;
}

void Song::play (bool on)
{
    if (on)
        context.state |= Vst::ProcessContext::kPlaying;
    else
        context.state &= ~Vst::ProcessContext::kPlaying;
}

void Song::locate (double quarters)
{
    context.projectTimeMusic = quarters;
    context.projectTimeSamples = static_cast<Vst::TSamples> (quarters * 60.0 / context.tempo * kSampleRate);
    const double bar = context.timeSigNumerator * 4.0 / context.timeSigDenominator;
    context.barPositionMusic = std::floor (quarters / bar) * bar;
}

size_t Song::samplesFor (double quarters) const
{
    return static_cast<size_t> (std::llround (quarters * 60.0 / context.tempo * kSampleRate));
}

void useSyncMode (Plug& plug)
{
    plug.looperOnly ();
    plug.set (kLooperMode, 1);

    // The tape is sized from the tempo, so this needs a valid one. The slowest tempo in the tests
    // gives a tape that's large enough for all of them. This also gives the level changes above
    // time to finish.
    Song song (plug, 60.0);
    plug.settle (0.3);

    // How long the tape takes depends on the build and the machine, and a host can't ask whether
    // it's there, so this finds out from a short test recording: only a recorded click comes back.
    song.context.tempo = 480.0;
    plug.set (kLooperBars, kBarsHalf);

    constexpr int kMaxAttempts = 300;
    bool isReady = false;
    Samples in (static_cast<size_t> (kSampleRate * 0.6), 0.0f);
    in[1000] = 0.9f;
    Samples out;
    for (int attempt = 0; attempt < kMaxAttempts && !isReady; ++attempt)
    {
        song.locate (0.0);
        song.play (true);
        plug.pressButton (Looper::kCommandRec);
        plug.runMono (in, out);
        isReady = findClicks (out).size () >= 1;

        song.play (false);
        plug.pressButton (Looper::kCommandStop);
        plug.pressButton (Looper::kCommandClear);
        plug.settle (0.01);

        if (!isReady)
            Sleep (100);
    }
    check ("the tempo sync tape was allocated", isReady);

    plug.set (kLooperBars, kBarsOne);
    plug.settle (0.05);
    plug.context = nullptr;
}

void recordFree (Plug& plug, double hz, double amplitude, double lengthSeconds)
{
    plug.set (kLooperFreeLength, lengthSeconds);
    plug.settle (0.8);
    plug.pressButton (Looper::kCommandRec);

    Samples out;
    plug.runMono (sine (hz, amplitude, lengthSeconds), out);
    plug.runMono (silence (0.1), out);
    plug.pressButton (Looper::kCommandOverdub);
    plug.settle (0.05);
}

Samples sine (double hz, double amplitude, double seconds, double sampleRate)
{
    Samples samples (static_cast<size_t> (seconds * sampleRate));
    for (size_t i = 0; i < samples.size (); ++i)
        samples[i] = static_cast<float> (amplitude * std::sin (2.0 * kPi * hz * static_cast<double> (i) / sampleRate));
    return samples;
}

Samples silence (double seconds)
{
    return Samples (static_cast<size_t> (seconds * kSampleRate), 0.0f);
}

Samples slice (const Samples& samples, size_t from, size_t count)
{
    return Samples (samples.begin () + static_cast<std::ptrdiff_t> (from),
                    samples.begin () + static_cast<std::ptrdiff_t> (std::min (samples.size (), from + count)));
}

double rms (const Samples& samples, size_t from)
{
    double energy = 0.0;
    for (size_t i = from; i < samples.size (); ++i)
        energy += static_cast<double> (samples[i]) * samples[i];
    return std::sqrt (energy / static_cast<double> (std::max<size_t> (1, samples.size () - from)));
}

double peak (const Samples& samples)
{
    double highest = 0.0;
    for (float sample : samples)
        highest = std::max (highest, static_cast<double> (std::abs (sample)));
    return highest;
}

double db (double value)
{
    return 20.0 * std::log10 (std::max (value, 1e-12));
}

double toneLevel (const Samples& samples, double hz, size_t from, double sampleRate)
{
    const size_t count = samples.size () - from;
    double real = 0.0;
    double imaginary = 0.0;
    double windowSum = 0.0;
    for (size_t i = 0; i < count; ++i)
    {
        const double window = 0.5 - 0.5 * std::cos (2.0 * kPi * static_cast<double> (i) / static_cast<double> (count - 1));
        const double phase = 2.0 * kPi * hz * static_cast<double> (i) / sampleRate;
        real += window * samples[from + i] * std::cos (phase);
        imaginary += window * samples[from + i] * std::sin (phase);
        windowSum += window;
    }
    return 2.0 * std::sqrt (real * real + imaginary * imaginary) / windowSum;
}

double measureFrequency (const Samples& samples, double sampleRate)
{
    int crossings = 0;
    int first = -1;
    int last = -1;
    for (size_t i = 1; i < samples.size (); ++i)
    {
        if (samples[i - 1] < 0.0f && samples[i] >= 0.0f)
        {
            if (first < 0)
                first = static_cast<int> (i);
            last = static_cast<int> (i);
            ++crossings;
        }
    }
    return crossings < 2 ? 0.0 : (crossings - 1) * sampleRate / (last - first);
}

std::vector<size_t> findClicks (const Samples& samples, float threshold)
{
    std::vector<size_t> found;
    for (size_t i = 0; i < samples.size (); ++i)
    {
        if (std::abs (samples[i]) > threshold && (found.empty () || i > found.back () + 100))
            found.push_back (i);
    }
    return found;
}

size_t findPeakIndex (const Samples& samples)
{
    const auto highest = std::max_element (samples.begin (), samples.end (), [] (float a, float b) { return std::abs (a) < std::abs (b); });
    return static_cast<size_t> (highest - samples.begin ());
}

} // namespace Tests
} // namespace LoopTexturer
