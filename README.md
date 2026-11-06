# LoopTexturer

<img src="docs/images/VST_Compatible_Logo_Steinberg_with_TM.png" alt="VST Compatible" width="110">

A VST® 3 insert plug-in for sound design in Cubase, combining an effects chain with a tape-style looper. It's built directly on the Steinberg VST 3 SDK.

`docs/signal-path.md` describes the signal chain and how each module behaves.

## Download

**On Windows,** get the installer from the [latest release](https://github.com/HadronycheAudio/LoopTexturer/releases/latest). It needs 64-bit Windows 10 or later, and installs to `C:\Program Files\Common Files\VST3`. The installer isn't code-signed, so Windows will probably warn you: choose **More info**, then **Run anyway**.

**On a Mac,** get `LoopTexturer-<version>-macOS.pkg` from the same page. It's a VST 3 and an Audio Unit, for Intel and Apple Silicon, on macOS 10.15 or later. It installs to `/Library/Audio/Plug-Ins`. The installer isn't signed by Apple, so macOS will refuse it the first time: the release notes explain how to open it. The Mac version hasn't been used in a real host yet.

## Requirements for building

- Visual Studio 2022 with the "Desktop development with C++" workload
- CMake 3.25 or newer
- Git

## Getting the code

The SDK and HadronycheCore, the code shared by the Hadronyche plug-ins, are pinned git submodules in `external/`:

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

## Testing

```powershell
cmake --build build --config Release --target LoopTexturerTests
ctest --test-dir build -C Release --output-on-failure -j 4
```

The tests load the built plug-in and drive it like a host, including its panel. They measure its output and check that it stays compatible with saved projects. They don't touch your mouse or your exports folder. `AGENTS.md` explains how they work.

## Installing your own build

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
| `tests/` | Tests that drive the built plug-in like a host |
| `installer/` | The Windows installer, and the script that tests it. The macOS installer is built by HadronycheCore's `tools/macos/` |

## Licence

LoopTexturer is released under the MIT licence: see `LICENSE`.

It's built with the following, which keep their own licences:

- VST3 SDK: MIT (`external/vst3sdk/LICENSE.txt`)
- VSTGUI (`external/vst3sdk/vstgui4/LICENSE`)
- HadronycheCore: MIT (`external/hadronychecore/LICENSE`)
- Apple's AudioUnitSDK, on macOS only: Apache 2.0

VST is a registered trademark of Steinberg Media Technologies GmbH.
