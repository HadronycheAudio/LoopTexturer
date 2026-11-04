#include "Controller.h"

#include "Editor.h"

using namespace Steinberg;

namespace LoopTexturer {

tresult PLUGIN_API Controller::initialize (FUnknown* context)
{
    const tresult result = EditController::initialize (context);
    if (result != kResultOk)
        return result;

    // Add parameters here, e.g. parameters.addParameter (...)
    return kResultOk;
}

IPlugView* PLUGIN_API Controller::createView (FIDString name)
{
    if (FIDStringsEqual (name, Vst::ViewType::kEditor))
        return new Editor (this);

    return nullptr;
}

} // namespace LoopTexturer
