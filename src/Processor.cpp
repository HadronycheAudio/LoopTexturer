#include "Processor.h"

#include "Controller.h"

#include <cstring>

using namespace Steinberg;

namespace LoopTexturer {

Processor::Processor ()
{
    setControllerClass (Controller::getClassFUID ());
}

tresult PLUGIN_API Processor::initialize (FUnknown* context)
{
    const tresult result = AudioEffect::initialize (context);
    if (result != kResultOk)
        return result;

    addAudioInput (STR16 ("Stereo In"), Vst::SpeakerArr::kStereo);
    addAudioOutput (STR16 ("Stereo Out"), Vst::SpeakerArr::kStereo);
    return kResultOk;
}

tresult PLUGIN_API Processor::setBusArrangements (Vst::SpeakerArrangement* inputs, int32 numIns,
                                                  Vst::SpeakerArrangement* outputs, int32 numOuts)
{
    // One input and one output bus with the same non-empty layout.
    if (numIns == 1 && numOuts == 1 && inputs[0] == outputs[0] && Vst::SpeakerArr::getChannelCount (outputs[0]) != 0)
        return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);

    return kResultFalse;
}

tresult PLUGIN_API Processor::canProcessSampleSize (int32 symbolicSampleSize)
{
    return symbolicSampleSize == Vst::kSample32 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API Processor::process (Vst::ProcessData& data)
{
    if (data.numOutputs == 0 || data.outputs[0].numChannels == 0 || data.numSamples == 0)
        return kResultOk;

    auto& output = data.outputs[0];
    const auto byteCount = sizeof (float) * static_cast<size_t> (data.numSamples);
    const int32 inputChannels = data.numInputs > 0 ? data.inputs[0].numChannels : 0;

    for (int32 c = 0; c < output.numChannels; ++c)
    {
        if (c >= inputChannels)
            std::memset (output.channelBuffers32[c], 0, byteCount);
        else if (output.channelBuffers32[c] != data.inputs[0].channelBuffers32[c])
            std::memcpy (output.channelBuffers32[c], data.inputs[0].channelBuffers32[c], byteCount);
    }
    output.silenceFlags = data.numInputs > 0 ? data.inputs[0].silenceFlags : 0;

    return kResultOk;
}

// There is no state to save yet.
tresult PLUGIN_API Processor::setState (IBStream*)
{
    return kResultOk;
}

tresult PLUGIN_API Processor::getState (IBStream*)
{
    return kResultOk;
}

} // namespace LoopTexturer
