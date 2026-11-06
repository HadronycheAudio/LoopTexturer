#include "Controller.h"

#include "Editor.h"
#include "LooperBridge.h"
#include "Messages.h"
#include "Parameters.h"

#include "hadronyche/vst/SpecParameter.h"

using namespace Steinberg;

namespace LoopTexturer {

tresult PLUGIN_API Controller::initialize (FUnknown* context)
{
    const tresult result = EditController::initialize (context);
    if (result != kResultOk)
        return result;

    for (Vst::ParamID id = 0; id < kParamCount; ++id)
        parameters.addParameter (new Hadronyche::SpecParameter (id, getParamSpec (id)));

    return kResultOk;
}

tresult PLUGIN_API Controller::setComponentState (IBStream* state)
{
    if (!state)
        return kResultFalse;

    const bool ok = readParamState (state, [this] (Vst::ParamID id, double value) { setParamNormalized (id, value); });
    return ok ? kResultOk : kResultFalse;
}

IPlugView* PLUGIN_API Controller::createView (FIDString name)
{
    if (FIDStringsEqual (name, Vst::ViewType::kEditor))
        return new Editor (this);

    return nullptr;
}

tresult PLUGIN_API Controller::notify (Vst::IMessage* message)
{
    if (message && FIDStringsEqual (message->getMessageID (), kMessageLooperBridge))
    {
        int64 id = 0;
        if (message->getAttributes ()->getInt (kAttributeBridgeId, id) == kResultOk)
            _looperBridge = LooperBridge::find (id);
        return kResultOk;
    }
    return EditController::notify (message);
}

void Controller::sendLooperCommand (int command)
{
    if (auto* message = allocateMessage ())
    {
        message->setMessageID (kMessageLooperCommand);
        message->getAttributes ()->setInt (kAttributeCommand, command);
        sendMessage (message);
        message->release ();
    }
}

} // namespace LoopTexturer
