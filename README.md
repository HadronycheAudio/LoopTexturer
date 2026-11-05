# LoopTexturer

A VST3 insert plug-in for sound design in Cubase, combining an effects chain with a tape-style looper. It's built directly on the Steinberg VST3 SDK.

`docs/signal-path.md` describes the signal chain and how each module behaves.

## Requirements

- Visual Studio 2022 with the "Desktop development with C++" workload
- CMake 3.25 or newer
- Git

## Getting the code

The SDK is a pinned git submodule in `external/`:

```powershell
git -c core.longpaths=true clone --recurse-submodules <repo-url>
# or, in an existing clone:
git -c core.longpaths=true submodule update --init --recursive
```

Long paths are needed if you clone into a deep folder, because some of the SDK's sample files would otherwise exceed the Windows path limit.

## Building

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target LoopTexturer
```

This produces `build/VST3/Release/LoopTexturer.vst3`. Each build also runs the Steinberg validator on the plug-in, and a validation failure fails the build.

To open the project in the Visual Studio IDE, use `build/LoopTexturer.sln`.

## Installing

Close Cubase, then run this from an elevated PowerShell in the project folder:

```powershell
Copy-Item -Recurse -Force build/VST3/Release/LoopTexturer.vst3 "C:\Program Files\Common Files\VST3\"
```

## Using the looper

**Free running** works like an endless tape loop, whether or not Cubase is playing:

- Length sets the tape speed, so changing it repitches the loop.
- Rec replaces what's on the tape for one pass, then moves on to overdubbing. Pressing Rec before the pass ends stops recording instead.
- Stop halts the tape, and Play relaunches it from the start of the loop.

**Tempo sync** follows the Cubase transport:

- Choose a length in bars, or Open to end the recording yourself. The maximum is 64 bars.
- Rec and Play start at the next bar line. Overdub starts right away.
- When the recording ends, the looper moves on to overdubbing.
- Stop silences the loop right away, and Play brings it back at the next bar line, in phase.
- A tempo change repitches the loop.

In both modes:

- Feedback at 100% leaves the loop unchanged. Lower values fade it, and values above 100% push it into distortion. Either way, this keeps changing the loop on every pass.
- Dry/wet at 50% plays both the live input and the loop at full level.
- Drag the waveform to the Cubase timeline to get the loop as you hear it, as a WAV file. These files are kept in `%LOCALAPPDATA%\LoopTexturer\Exports`.

## Project layout

| Path | Purpose |
| --- | --- |
| `src/PluginConfig.h` | Plug-in name, vendor details and class IDs |
| `src/PluginEntry.cpp` | VST3 module entry, registers the classes below |
| `src/Parameters.*` | Parameter IDs, ranges and text, plus saving and loading their values |
| `src/Processor.*` | Runs the signal chain, and saves and restores the loop |
| `src/Controller.*` | Registers the parameters and creates the editor |
| `src/Editor.*` | The single-panel plug-in window, including the waveform and drag |
| `src/LooperBridge.*` | What the processor and the UI share: the looper's status and audio |
| `src/LoopExport.*` | Writes the loop as heard to a WAV file |
| `src/Messages.h` | VST3 message names between the processor and the controller |
| `src/dsp/` | LFO, filter, saturator, oversampler, looper and parameter smoothing |

## Licence

VST3 SDK: MIT (`external/vst3sdk/LICENSE.txt`)
