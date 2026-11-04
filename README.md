# LoopTexturer

A VST3 insert plug-in, built directly on the Steinberg VST3 SDK. The target DAW is Cubase.

## Requirements

- Visual Studio 2022 with the "Desktop development with C++" workload
- CMake 3.25 or newer
- Git

## Getting the code

The SDK is a pinned git submodule in `external/`:

```powershell
git clone --recursive <repo-url>
# or, in an existing clone:
git submodule update --init --recursive
```

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

## Testing the drag

1. Open Cubase and add LoopTexturer as an insert on any audio track.
2. Drag from the plug-in window onto an audio track in the project window.
3. Check that a 2-second clip of a 440 Hz tone appears where you dropped it.

The plug-in window shows the result of each drag. "Dropped" means the target accepted the file.

The test file is created on the first drag at `%LOCALAPPDATA%\LoopTexturer\drag-test.wav`. It's 48 kHz, so Cubase may offer to convert it in a project at another sample rate.

## How the drag works

VST3 has no drag and drop API. The plug-in writes a WAV file and starts an ordinary operating system file drag using VSTGUI, so Cubase handles it like a file dragged from Explorer.

## Project layout

| Path | Purpose |
| --- | --- |
| `src/PluginConfig.h` | Plug-in name, vendor details and class IDs |
| `src/PluginEntry.cpp` | VST3 module entry, registers the classes below |
| `src/Processor.*` | Audio processing, currently a pass-through |
| `src/Controller.*` | Parameters, and creates the editor |
| `src/Editor.*` | Plug-in window and the drag test area |
| `src/DragTestFile.*` | Writes the test WAV |

## Not implemented yet

- Recording audio and writing it to a WAV. `DragTestFile` is a stand-in for this.
- Parameters and saved state.

Before release, replace the placeholder vendor details in `src/PluginConfig.h`.

## Licence

VST3 SDK: MIT (`external/vst3sdk/LICENSE.txt`)
