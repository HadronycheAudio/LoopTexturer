# AGENTS.md

Guidance for coding agents working in this repository. Read [Rules that protect users' work](#rules-that-protect-users-work) before changing anything.

LoopTexturer is a VST3 insert plug-in for sound design in Cubase on Windows: an effects chain plus a tape-style looper whose audio can be dragged onto the timeline. It uses the Steinberg VST3 SDK and VSTGUI directly. JUCE and ARA were considered and deliberately rejected, so don't reintroduce them.

`docs/signal-path.md` is the behaviour specification. `README.md` covers building and usage.

## Commands

The only supported platform is Windows x64 with Visual Studio 2022 and CMake 3.25 or newer.

```powershell
# Clone. Without long paths, cloning into a folder deeper than about 100 characters fails partway
# through the SDK, because some of its sample files exceed the Windows path limit.
git -c core.longpaths=true clone --recurse-submodules <repo-url>

# In an existing clone, if submodules are missing
git -c core.longpaths=true submodule update --init --recursive

# Configure
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Build. This also runs the Steinberg validator, and a failure fails the build.
cmake --build build --config Release --target LoopTexturer
cmake --build build --config Debug --target LoopTexturer

# Run the validator on its own, for example with extra output
build/bin/Release/validator.exe build/VST3/Release/LoopTexturer.vst3

# Install for testing in Cubase. Close Cubase first, and use an elevated shell.
Copy-Item -Recurse -Force build/VST3/Release/LoopTexturer.vst3 "C:\Program Files\Common Files\VST3\"
```

There is no linter, formatter or test suite in this repository, so there's no way to run a single test. See [Verifying a change](#verifying-a-change).

Follow the existing style, which comes from the SDK: a space before call parentheses (`foo (x)`), `_member` for private members, `kName` for constants, and comments that explain why rather than what.

## Rules that protect users' work

People use this plug-in in real projects. Breaking these rules damages saved projects or audio, and usually nothing fails at build time.

### Saved projects

- **Never change the class IDs in `src/PluginConfig.h`.** Cubase uses them to find the plug-in in saved projects.
- **Never renumber, reorder or remove entries in `ParamId`** (`src/Parameters.h`). Their values are saved in projects and automation. Add new parameters just before `kParamCount`, and add the matching row to `kSpecs` in `src/Parameters.cpp`, which is listed in the same order.
- **Don't change the range or curve of an existing parameter.** Hosts store values normalized to 0..1, so this silently changes what saved projects sound like. If it's unavoidable, migrate old values and bump the state version.
- **The label lists in `src/Parameters.cpp` must match the enums in `src/dsp/`** (`Lfo::Wave`, `StateVariableFilter::Mode`, `Saturator::Mode`), which are cast directly from parameter values. Only ever add to the end of these lists.
- **Changing the saved format requires bumping `kStateVersion` and still reading every older version.** The format is:
    1. `int32` version, `int32` count, then `int32` ID and `double` normalized value pairs (`writeParamState`).
    2. From version 2, the loop (`Processor::writeLoopState`): `int32` has-loop flag, and if set, `int32` mode, `int64` frames, 4 `double`s (quarters, anchor, frames per quarter, write speed), then all left samples and all right samples as `float`.
- **Loading must tolerate missing or damaged data.** An older state has no loop section, which reads as having no loop.

### The audio thread

`Processor::process` and everything it calls runs on the audio thread: `Looper::beginBlock`, `processSample`, `endBlock` and all of `src/dsp/`, apart from what's listed below.

- **No allocation, locks, file access, system calls or anything else that can block.** This includes `std::vector` growth, `std::string`, `std::function` copies and `shared_ptr` copies. It's the reason for much of the design below, and the failure is audio dropouts that may not show up in testing.
- **Work that takes too long for one block has to be spread out or moved elsewhere.** `Looper::eraseSomeOfFreeTape` clears a little per block, and large tapes are allocated and freed by the processor's worker thread.
- `Tape`'s constructor, `Looper::prepare`, `LooperBridge`'s tape functions and `exportLoop` are not safe on the audio thread.

### Sound and timing

- **The reported latency must always be true.** It's 96 samples: 48 from each saturator (`Oversampler::kLatency`), at every sample rate and whether or not they're on, so that toggling them doesn't shift audio. Anything that changes it must update `Processor::getLatencySamples`, the dry delay in `Processor::process`, the `inputLatency` given to `Looper`, and the samples skipped in `exportLoop`.
- **`Oversampler::kTaps` must stay of the form 4n + 1.** That's what makes its latency a whole number of samples, so that the dry signal stays perfectly aligned.
- **The looper reads and writes with one head at one position.** Separate heads, or any delay in the feedback path, would misalign overdubs and shift the loop on every pass.
- **At 100% feedback, the loop must stay bit-exact.** Feedback works directly on stored cells, and `writeSpan` skips writing when nothing changes. Don't route feedback through an interpolated read.
- **In tempo sync, the head's position is calculated from the host's musical position.** Don't accumulate it instead, or the loop will drift off the grid. This is also what makes a tempo change repitch the loop.
- **`Smoother` must stay double precision.** In single precision it stalls about 0.01% short of its target, which makes free running loops the wrong length.

### Decisions made by the project owner

These were chosen deliberately, so don't "fix" them without the owner's agreement:

- The post saturator processes only the loop's output, before dry/wet. Feedback doesn't pass through it, so it can never change the recorded loop.
- Feedback goes up to 200%, and it runs on every pass, not only while overdubbing.
- After its first pass, Rec moves on to overdubbing in both modes.
- Switching looper mode clears the loop, after confirmation in the UI. The mode parameter isn't automatable, so automation can't wipe a loop.
- The looper's buttons are VST3 messages rather than parameters.
- The dragged WAV is the loop as heard: at its current speed and through the post saturator, without the dry signal or Volume.
- Exported WAVs are never deleted, because Cubase may keep referencing them.
- Saturators add constant latency, because this is a sound design tool rather than a performance tool.

In `docs/signal-path.md`, **Requirement** marks the owner's decisions and **Proposed** marks suggestions that were accepted by default. Update it when behaviour changes, and don't turn a Proposed item into a Requirement yourself.

### The repository

- **Don't edit anything in `external/vst3sdk`.** It's a submodule pinned to `v3.8.1_build_84`. `git submodule status` shows a misleading `v3.7.3...` label for that same commit, so check with `git -C external/vst3sdk describe --tags`.
- Don't change git configuration. Commit or push only when asked.

## Architecture

### Signal path

`Processor::process` runs this for every sample:

```
In → Gain → Pan (LFO) → Filter (LFO) → Saturator ─┬─ 48-sample delay ──────────────┬→ Dry/wet → Volume → Out
                                                  └→ Looper → Post saturator ──────┘
```

### Threads and ownership

Understanding this is necessary before changing the looper:

| Thread | Runs | Talks to the others through |
| --- | --- | --- |
| Audio | `Processor::process`, the `Looper` engine | Atomics only |
| UI | `Editor`, `WaveformView`, `exportLoop` | Reads `LooperBridge`, sends VST3 messages |
| Host | `setState`, `getState`, `setActive` | Atomics, and handing over a `LoadedLoop` |
| Worker (`Processor::runWorker`) | Allocates and frees tapes | `requestedSyncFrames`, `offerSyncTape`, `_tapeChangeMutex` |

How the pieces connect:

- **Parameters:** the host sends changes through `process`, and they land in `Processor::_params` as atomics. The last value per block is used, followed by smoothing. `getParamSpec` is the one definition of every parameter's range and text, shared by `Processor`, `Controller` and `Editor`.
- **Buttons:** `Editor` → `Controller::sendLooperCommand` → VST3 message → `Processor::notify` → `Looper::sendCommand`, an atomic bitmask read at the start of the next block.
- **Status and audio for the UI:** `LooperBridge` holds status atomics written once per block, and the tapes. `Tape` stores samples as relaxed atomics so that the UI can read while the audio thread writes without a data race.
- **Finding the bridge:** the processor sends only the bridge's ID in a message, and the controller looks it up in a registry of weak pointers. This avoids dangling pointers, whatever order the host destroys things in. It relies on the plug-in not being distributable (the `0` flag in `src/PluginEntry.cpp`), so that both sides share one process.
- **Tape lifetime:** `LooperBridge` owns tapes through `shared_ptr`, and the audio thread holds plain pointers. The worker releases only tapes outside `Looper::getTapesInUse`. Hold `_tapeChangeMutex` while adding a tape until the looper knows about it, or the worker could release it.
- **Restoring a loop:** `setState` writes parameters first and then hands over the `LoadedLoop`. If the order were reversed, the audio thread could see the old mode, treat it as a mode switch and clear the new loop.

### The looper

`src/dsp/Looper.cpp` is a tape with one head. The modes only differ in what moves it:

- **Free running:** a 10 s tape, where the length knob sets the head's speed. Changing the speed repitches the loop. Stop leaves the tape standing still.
- **Tempo sync:** a tape sized for 64 bars at the current tempo plus 25%, allocated by the worker when you enter this mode. `_syncState` goes from `Empty` to `Recording` to `Loop`.

`writeSpan` fills every cell the head passed, ramping between input samples, so audio recorded at any speed plays back cleanly at that speed. `Tape::read` averages cells when playing faster than the audio was recorded, to limit aliasing. Punching in and out uses short envelopes to avoid clicks, and at the end of a recording these also crossfade the loop's seam.

A locate during a recording's first pass cancels it, because its length isn't settled yet.

### The editor

`Editor.cpp` builds the whole panel in code, with no `.uidesc` file. A 33 ms timer in `syncControls` pushes parameter values and looper status to the controls.

- `Binding` connects a control to a parameter, with the control's tag being its `ParamId`. Tags from 1000 upwards are buttons rather than parameters, and `valueChanged` and `controlBeginEdit` depend on that split.
- A new `COptionMenu` needs `setCurrent (0)`, or it stays blank at a value of 0.
- Dragging is an ordinary operating system file drag through `CView::doDrag`, because VST3 has no drag API. The host just sees a file being dropped on it.

## Verifying a change

Building only proves that the plug-in follows the VST3 rules. The validator doesn't check sound, the looper, the panel or saved loops, and it runs with no transport.

There are no tests in the repository, so check behaviour directly, in proportion to the change:

1. **Build both Debug and Release.** Both must finish with no compiler errors, and the validator must pass.
2. **Measure anything that changes the audio.** Write a small throwaway host that loads the built plug-in through the VST3 interfaces and measures its output. Keep it out of the repository unless asked to add it.
3. **Test it in Cubase** if you can, or say clearly that you couldn't. Don't report behaviour you haven't observed as working.

If you write such a host:

- It has to provide `IHostApplication` so that `allocateMessage` works, and connect the processor and controller through `IConnectionPoint`.
- It usually runs faster than real time, so allow for the worker thread: sleep after entering tempo sync, with a valid tempo in `ProcessContext`, before pressing Rec.
- Let the speed settle for about 0.8 s after changing the free running length, before recording.
- `DoDragDrop` only wakes on mouse input, so a synthetic drag needs mouse messages posted to the thread.
- Windows can capture the panel with `PrintWindow` and `PW_RENDERFULLCONTENT`, which works on an off-screen window.

These results are known to be correct, and a change shouldn't move them unless that's its purpose:

| Check | Expected |
| --- | --- |
| Latency, at 44.1, 48 and 96 kHz, saturators on or off | Reported and measured both exactly 96 samples |
| Chain with the filter as a 20 Hz high-pass and both saturators off | Unity gain |
| Pan | Constant power, with both channels unchanged in the centre |
| Free running: 1 kHz recorded and replayed | 1000.00 Hz, within 0.2 dB, at every length |
| Length from 2 s to 1 s, or to 4 s | Pitch doubles, or halves |
| Tempo sync: a click in the recording | Repeats on the exact sample, offset by 96 samples |
| 100% feedback, including overdubbing silence | Saved audio is byte-identical |
| 200% feedback | Output stays below about 1.5, with no NaNs and no DC build-up |
| Saved loop restored at another sample rate | Same pitch |
| Dragged WAV | Length and pitch match the current speed or tempo |

## Known limits

- It has only been built and used on Windows, and Cubase is the only host it's been used in.
- Sample-exact grid alignment assumes that Cubase's reported position refers to the audio entering the plug-in. This hasn't been measured in Cubase, and an error would be at most 96 samples.
- Modes with sharp corners still alias at extreme settings: Hard clip at 14 kHz with 24 dB of drive has aliases at about -26 dB.
- Starting a drag on a very long loop with the post saturator on can freeze the panel for a few seconds, because the export runs on the UI thread.
- The vendor details in `src/PluginConfig.h` are placeholders.
