#pragma once

#include "public.sdk/source/vst/vstguieditor.h"

namespace LoopTexturer {

// The plug-in window. For now it only holds the drag test area (see Editor.cpp).
class Editor : public Steinberg::Vst::VSTGUIEditor
{
public:
    explicit Editor (void* controller);

    bool PLUGIN_API open (void* parent, const VSTGUI::PlatformType& platformType) SMTG_OVERRIDE;
    void PLUGIN_API close () SMTG_OVERRIDE;
};

} // namespace LoopTexturer
