#include "Controller.h"

#include "Editor.h"
#include "LooperBridge.h"
#include "Messages.h"

using namespace Steinberg;

namespace LoopTexturer {

IPlugView* Controller::createEditor ()
{
    return new Editor (this);
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
