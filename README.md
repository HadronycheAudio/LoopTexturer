# LoopTexturer

<img src="docs/images/VST_Compatible_Logo_Steinberg_with_TM.png" alt="VST Compatible" width="110">

A VST® 3 insert plug-in for sound design in Cubase, combining an effects chain with a tape-style looper. It's built directly on the Steinberg VST 3 SDK.

`docs/signal-path.md` describes the signal chain and how each module behaves.

## Download

**On Windows,** get the installer from the [latest release](https://github.com/HadronycheAudio/LoopTexturer/releases/latest). It needs 64-bit Windows 10 or later, and installs to `C:\Program Files\Common Files\VST3`. The installer isn't code-signed, so Windows will probably warn you: choose **More info**, then **Run anyway**.

**On a Mac,** get `LoopTexturer-<version>-macOS.pkg` from the same page. It's a VST 3 and an Audio Unit, for Intel and Apple Silicon, on macOS 10.15 or later. It installs to `/Library/Audio/Plug-Ins`. The installer isn't signed by Apple, so macOS will refuse it the first time: the release notes explain how to open it. See [macOS: untested](#macos-untested) first.

**On Linux,** get `LoopTexturer-<version>-Linux-x86_64.tar.gz` from the same page. It's a VST 3 for x86_64, and it needs glibc 2.34 or later, as on Ubuntu 22.04, Debian 12 or newer. Extract it where hosts look:

```bash
mkdir -p ~/.vst3
tar -xzf LoopTexturer-<version>-Linux-x86_64.tar.gz -C ~/.vst3
```

It needs a few common libraries, which you probably have. On Debian or Ubuntu:

```bash
sudo apt install libxcb-cursor0 libxcb-util1 libxkbcommon-x11-0 libcairo2 libpango-1.0-0 libpangocairo-1.0-0
```

There's also an LV2 version, in `LoopTexturer-<version>-Linux-x86_64-LV2.tar.gz`, with the same panel. Extract it into `~/.lv2`. See [Linux: lightly tested](#linux-lightly-tested) first.

## macOS: untested

> [!WARNING]
> **The Mac versions are untested.** They pass automated tests and Apple's Audio Unit validation on build machines, on both Intel and Apple Silicon, but nobody has used them in a real host on a real Mac yet. Nothing has checked how they sound, or how the panel looks.

**Are you a Mac user who's ready to commit to testing? You're more than welcome!** Please [open an issue](https://github.com/HadronycheAudio/LoopTexturer/issues/new) saying which Mac, macOS version and host you use.

To build it yourself, you need Xcode, CMake 3.25 or newer, and an internet connection the first time, which fetches Apple's AudioUnitSDK:

```bash
git clone --recurse-submodules https://github.com/HadronycheAudio/LoopTexturer.git
cd LoopTexturer
cmake -S . -B build -G Xcode
cmake --build build --config Release --target LoopTexturer LoopTexturer-au
```

This produces `build/VST3/Release/LoopTexturer.vst3` and `build/VST3/Release/LoopTexturer.component`, for Intel and Apple Silicon, and runs the Steinberg validator on the plug-in. The build also copies the Audio Unit to `~/Library/Audio/Plug-Ins/Components`, where it keeps pointing at your build folder. To use the VST 3, copy it to `~/Library/Audio/Plug-Ins/VST3`.

To run the tests, which leave out the Windows-only panel test:

```bash
cmake --build build --config Release --target LoopTexturerTests
ctest --test-dir build -C Release --output-on-failure
```

## Linux: lightly tested

> [!NOTE]
> **The Linux version is new.** It passes the automated tests on Ubuntu 22.04 and 24.04 and Debian 12 and 13, and it has been tried briefly by hand in REAPER on Debian 13. Other hosts and distributions haven't been tried. Please [open an issue](https://github.com/HadronycheAudio/LoopTexturer/issues/new) saying which distribution and host you use if something goes wrong.

- **There's no dragging to the timeline on Linux.** VSTGUI can't start a drag there, so the feature is turned off. To get the loop as audio, render the track, or route its output to another track and record it.

To build it yourself, you need CMake 3.25 or newer, and the packages that this script installs on Debian or Ubuntu:

```bash
git clone --recurse-submodules https://github.com/HadronycheAudio/LoopTexturer.git
cd LoopTexturer
bash external/hadronychecore/tools/linux/install-build-packages.sh
cmake -S . -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release --target LoopTexturer
```

This produces `build/Release/VST3/Release/LoopTexturer.vst3`, and runs the Steinberg validator on the plug-in. To use it, copy it to `~/.vst3`.

To build the LV2 too, install `lv2-dev`, and build the target `LoopTexturer-lv2`. This produces `build/Release/LV2/LoopTexturer.lv2`, and to use it, copy it to `~/.lv2`. The tests that compare it with the VST 3 also need the target `LoopTexturerLv2Compare`, and the one that drives its looper's panel needs `LoopTexturerLv2Looper`.

To run the tests, which include one that opens the panel and so needs a display or `xvfb-run`:

```bash
cmake --build build/Release --target LoopTexturerTests
xvfb-run -a ctest --test-dir build/Release --output-on-failure
```

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
- Drag the waveform to the Cubase timeline to get the loop as you hear it, as a WAV file. These files are kept in `%LOCALAPPDATA%\LoopTexturer\Exports`. On Linux, there's no dragging: render the track, or route its output to another track and record it.

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
| `installer/` | The Windows installer. HadronycheCore's `tools/windows/` tests it, and its `tools/macos/` and `tools/linux/` build the macOS installer and the Linux tarballs |

## Contributing

LoopTexturer is free software that follows its author's own workflow, and there's no company or support promise behind it. If you find it handy and want it to work better with your host, your distribution or your way of working, you're welcome to take it further. Please [open an issue](https://github.com/HadronycheAudio/LoopTexturer/issues/new) or a pull request. The LV2 version is new, and help testing in other hosts is especially useful. `AGENTS.md` lists the rules that keep people's saved projects safe, so please read it before changing anything.

## Licence

LoopTexturer is released under the MIT licence: see `LICENSE`.

It's built with the following, which keep their own licences:

- VST3 SDK: MIT (`external/vst3sdk/LICENSE.txt`)
- VSTGUI (`external/vst3sdk/vstgui4/LICENSE`)
- HadronycheCore: MIT (`external/hadronychecore/LICENSE`)
- Apple's AudioUnitSDK, on macOS only: Apache 2.0

VST is a registered trademark of Steinberg Media Technologies GmbH.
