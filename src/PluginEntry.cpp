// VST3 module entry: registers the processor and controller classes with the host.

#include "Controller.h"
#include "PluginConfig.h"
#include "Processor.h"

#include "hadronyche/vst/PluginFactory.h"

// Not distributable: the processor and the UI share the looper's memory.
HADRONYCHE_PLUGIN_FACTORY (LoopTexturer, LOOPTEXTURER, Vst::PlugType::kFx, 0)
