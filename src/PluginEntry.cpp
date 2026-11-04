// VST3 module entry: registers the processor and controller classes with the host.

#include "Controller.h"
#include "PluginConfig.h"
#include "Processor.h"

#include "public.sdk/source/main/pluginfactory.h"

BEGIN_FACTORY_DEF (LOOPTEXTURER_VENDOR, LOOPTEXTURER_URL, LOOPTEXTURER_EMAIL)

    DEF_CLASS2 (INLINE_UID_FROM_FUID (LoopTexturer::Processor::getClassFUID ()),
                PClassInfo::kManyInstances,
                kVstAudioEffectClass,
                LOOPTEXTURER_NAME,
                Vst::kDistributable,
                Vst::PlugType::kFx,
                LOOPTEXTURER_VERSION,
                kVstVersionString,
                LoopTexturer::Processor::createInstance)

    DEF_CLASS2 (INLINE_UID_FROM_FUID (LoopTexturer::Controller::getClassFUID ()),
                PClassInfo::kManyInstances,
                kVstComponentControllerClass,
                LOOPTEXTURER_NAME,
                0,
                "",
                LOOPTEXTURER_VERSION,
                kVstVersionString,
                LoopTexturer::Controller::createInstance)

END_FACTORY
