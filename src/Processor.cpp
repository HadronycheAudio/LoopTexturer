#include "Processor.h"

#include "Controller.h"
#include "LooperBridge.h"
#include "Messages.h"

using namespace Steinberg;

namespace LoopTexturer {

Processor::Processor () : RustProcessor (Controller::getClassFUID ())
{
}

tresult PLUGIN_API Processor::connect (Vst::IConnectionPoint* other)
{
    const tresult result = AudioEffect::connect (other);
    if (result == kResultOk)
    {
        if (auto* message = allocateMessage ())
        {
            message->setMessageID (kMessageLooperBridge);
            message->getAttributes ()->setInt (kAttributeBridgeId, Ffi::looptexturer_bridge_id (shared ()));
            sendMessage (message);
            message->release ();
        }
    }
    return result;
}

tresult PLUGIN_API Processor::notify (Vst::IMessage* message)
{
    if (message && FIDStringsEqual (message->getMessageID (), kMessageLooperCommand))
    {
        int64 command = 0;
        if (message->getAttributes ()->getInt (kAttributeCommand, command) == kResultOk)
            Ffi::looptexturer_bridge_send_command (shared (), static_cast<int32> (command));
        return kResultOk;
    }
    return AudioEffect::notify (message);
}

} // namespace LoopTexturer
