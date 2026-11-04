#pragma once

namespace LoopTexturer {

// VST3 messages between the processor and the controller.

// Processor to controller: gives the ID of the shared LooperBridge.
constexpr const char* kMessageLooperBridge = "LooperBridge";
constexpr const char* kAttributeBridgeId = "id";

// Controller to processor: a button was pressed.
constexpr const char* kMessageLooperCommand = "LooperCommand";
constexpr const char* kAttributeCommand = "command";

} // namespace LoopTexturer
