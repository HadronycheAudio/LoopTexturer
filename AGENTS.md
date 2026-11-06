# AGENTS.md

Guidance for coding agents working in this repository. Read [Rules that protect users' work](#rules-that-protect-users-work) before changing anything.

LoopTexturer is a VST® 3 insert plug-in for sound design in Cubase on Windows: an effects chain plus a tape-style looper whose audio can be dragged onto the timeline. It uses the Steinberg VST3 SDK and VSTGUI directly. JUCE and ARA were considered and deliberately rejected, so don't reintroduce them.

`docs/signal-path.md` is the behaviour specification. `README.md` covers building and usage.

Code shared with the other Hadronyche plug-ins is in the `HadronycheCore` submodule, at `external/hadronychecore`: the parameters' curves and saved format, `PanelEditor`, `Smoother`, the test host and the build setup. Its `AGENTS.md` has its own rules.

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

# Build and run all the tests, which takes about 10 seconds in Release
cmake --build build --config Release --target LoopTexturerTests
ctest --test-dir build -C Release --output-on-failure -j 4

# Run a single test, and list them
ctest --test-dir build -C Release --output-on-failure -R "^sync-grid$"
ctest --test-dir build -C Release -N

# Install for testing in Cubase. Close Cubase first, and use an elevated shell.
Copy-Item -Recurse -Force build/VST3/Release/LoopTexturer.vst3 "C:\Program Files\Common Files\VST3\"
```

There's no linter or formatter.

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
    3. Version 3 has the same layout. It extended the looper's length from 10 s to 30 s, so `readParamState` converts that parameter's value when reading older versions. Automation saved by hosts can't be converted.
- **Loading must tolerate missing or damaged data.** An older state has no loop section, which reads as having no loop.

### The audio thread

`Processor::process` and everything it calls runs on the audio thread: `Looper::beginBlock`, `processSample`, `endBlock` and all of `src/dsp/`, apart from what's listed below.

- **No allocation, locks, file access, system calls or anything else that can block.** This includes `std::vector` growth, `std::string`, `std::function` copies and `shared_ptr` copies. It's the reason for much of the design below, and the failure is audio dropouts that may not show up in testing.
- **Work that takes too long for one block has to be spread out or moved elsewhere.** `Looper::eraseSomeOfFreeTape` clears a little per block, and large tapes are allocated and freed by the processor's worker thread.
- `Tape`'s constructor, `Looper::prepare`, `LooperBridge`'s tape functions and `exportLoop` are not safe on the audio thread.

### Sound and timing

- **The reported latency must always be true.** It's 96 samples: 48 from each saturator (`Oversampler::kLatency`), at every sample rate and whether or not they're on, so that toggling them doesn't shift audio. Anything that changes it must update `Processor::getLatencySamples`, the dry delay in `Processor::process`, the `inputLatency` given to `Looper`, and the samples skipped in `exportLoop`.
- **Broken samples at the input become silence, before anything else.** A NaN or infinite sample would otherwise stay in the filters and the loop for ever, and `broken-input` checks this.
- **`Oversampler::kTaps` must stay of the form 4n + 1.** That's what makes its latency a whole number of samples, so that the dry signal stays perfectly aligned.
- **The looper reads and writes with one head at one position.** Separate heads, or any delay in the feedback path, would misalign overdubs and shift the loop on every pass.
- **At 100% feedback, the loop must stay bit-exact.** Feedback works directly on stored cells, and `writeSpan` skips writing when nothing changes. Don't route feedback through an interpolated read.
- **In tempo sync, the head's position is calculated from the host's musical position.** Don't accumulate it instead, or the loop will drift off the grid. This is also what makes a tempo change repitch the loop.
- **The signal path is in single precision, and has to stay that way.** Use `Smoother::nextFloat` on it, and `dbToGain` from `src/dsp/FloatGain.h`. Going through double instead still compiles, and changes the sound of saved projects.
- **`Hadronyche::Smoother` must stay double precision.** In single precision it stalls about 0.01% short of its target, which makes free running loops the wrong length.

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
- **Don't edit `external/hadronychecore` from here.** Change it in its own repository, and move the submodule here afterwards, once the new version has been compared with the old as its `AGENTS.md` describes.
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
| Worker (`Processor::runWorker`) | Allocates and frees tapes | `requestedSyncFrames`, `requestedFreeFrames`, `offerSyncTape`, `offerFreeTape`, `_tapeChangeMutex` |

How the pieces connect:

- **Parameters:** the host sends changes through `process`, and they land in `Processor::_params` as atomics. The last value per block is used, followed by smoothing. `getParamSpec` is the one definition of every parameter's range and text, shared by `Processor`, `Controller` and `Editor`.
- **Buttons:** `Editor` → `Controller::sendLooperCommand` → VST3 message → `Processor::notify` → `Looper::sendCommand`, an atomic bitmask read at the start of the next block.
- **Status and audio for the UI:** `LooperBridge` holds status atomics written once per block, and the tapes. `Tape` stores samples as relaxed atomics so that the UI can read while the audio thread writes without a data race.
- **Finding the bridge:** the processor sends only the bridge's ID in a message, and the controller looks it up in a registry of weak pointers. This avoids dangling pointers, whatever order the host destroys things in. It relies on the plug-in not being distributable (the `0` flag in `src/PluginEntry.cpp`), so that both sides share one process.
- **Tape lifetime:** `LooperBridge` owns tapes through `shared_ptr`, and the audio thread holds plain pointers. The worker releases only tapes outside `Looper::getTapesInUse`. Hold `_tapeChangeMutex` while adding a tape until the looper knows about it, or the worker could release it.
- **Restoring a loop:** `setState` writes parameters first and then hands over the `LoadedLoop`. If the order were reversed, the audio thread could see the old mode, treat it as a mode switch and clear the new loop.

### The looper

`src/dsp/Looper.cpp` is a tape with one head. The modes only differ in what moves it:

- **Free running:** a 30 s tape, where the length knob sets the head's speed. Changing the speed repitches the loop. Stop leaves the tape standing still. A restored loop keeps the tape size it was saved with, so `_freeTape`'s capacity can differ until that loop is cleared, when the worker provides a full tape.
- **Tempo sync:** a tape sized for 64 bars at the current tempo plus 25%, allocated by the worker when you enter this mode. `_syncState` goes from `Empty` to `Recording` to `Loop`.

`writeSpan` fills every cell the head passed, ramping between input samples, so audio recorded at any speed plays back cleanly at that speed. `Tape::read` averages cells when playing faster than the audio was recorded, to limit aliasing. Punching in and out uses short envelopes to avoid clicks, and at the end of a recording these also crossfade the loop's seam.

A locate during a recording's first pass cancels it, because its length isn't settled yet.

### The editor

`Editor.cpp` builds the panel in `buildPanel`, on `Hadronyche::PanelEditor`, with no `.uidesc` file. A 33 ms timer in `PanelEditor` pushes parameter values to the controls, and `onSync` does the same for the looper's status.

- A control's tag is its `ParamId`. Tags from 1000 upwards are buttons rather than parameters, which `Editor::valueChanged` handles, and `PanelEditor` leaves anything from `kParamCount` upwards alone.
- Dragging is an ordinary operating system file drag through `CView::doDrag`, because VST3 has no drag API. The host just sees a file being dropped on it.

## Verifying a change

Before calling a change done:

1. **Build Debug and Release, and run the tests on both.** The validator runs as part of the build, but it only proves that the plug-in follows the VST3 rules. It doesn't check sound, the looper, the panel or saved loops.
2. **Add a test for any new behaviour,** and check that it fails without your change.
3. **Test it in Cubase** if you can, or say clearly that you couldn't. The tests simulate a host, and nothing here has been measured for how it sounds. Don't report behaviour you haven't observed as working.

### The tests

`.github/workflows/ci.yml` builds Debug and Release and runs all of this on every push and pull request. Don't weaken or skip it to get a change through.

`tests/` builds one program, `LoopTexturerTests`, which loads the built plug-in and drives it through the VST3 interfaces, the way Cubase does. It doesn't link the plug-in's code, so tests can only see what a host can.

| File | Covers |
| --- | --- |
| `CompatibilityTests.cpp` | Class IDs, parameter IDs, ranges and list order, and the saved format |
| `ChainTests.cpp` | Latency, levels, pan, LFOs, filter, saturators and aliasing |
| `LooperTests.cpp` | Both looper modes, feedback, dry/wet and saved loops |
| `PanelTest.cpp` | The real editor, driven with mouse messages, including the drag export |

- **Never change an expected value in `CompatibilityTests.cpp` to make a test pass.** Those values are written out by hand on purpose, and a failure there means the change would damage saved projects. Adding new parameters to them is fine, and expected.
- **Don't loosen a limit elsewhere to get a test passing** without working out why it failed. Several of these numbers come from real bugs.
- **To add a test,** add it to `Tests.h`, `kTests` in `TestMain.cpp` and `LOOPTEXTURER_TESTS` in `tests/CMakeLists.txt`.
- **`PanelTest.cpp` clicks fixed positions,** so moving the panel's controls means updating the positions at the top of that file.
- **Exports go to a temporary folder,** because the program sets `LOCALAPPDATA` before loading the plug-in.
- **To see the panel,** set `LOOPTEXTURER_SCREENSHOTS` to a folder before running the `panel` test, which saves a BMP at each step.
- **To measure CPU use,** run `build/bin/Release/LoopTexturerTests.exe <plug-in DLL> cpu`, in a Release build. The DLL is at `build/VST3/Release/LoopTexturer.vst3/Contents/x86_64-win/LoopTexturer.vst3`.

These tests run much faster than real time, which matters when writing new ones:

- Use `useSyncMode` to enter tempo sync. It waits until the worker thread has allocated the tape, however long that takes on the machine. Pressing Rec before that makes the recording start a bar late, and a fixed wait fails on slower machines.
- Let the speed settle for about 0.8 s after changing the free running length, before recording. `recordFree` does this.
- Parameter changes are smoothed over about 20 ms, so allow for that before measuring. Otherwise, an early click can leak through the dry signal.
- `DoDragDrop` only wakes on mouse input, so a synthetic drag needs mouse messages posted to the thread. `Panel` does this.

## Releasing

Pushing a tag `v<version>` publishes a release, once everything has passed:

1. Set the version in `project()` in `CMakeLists.txt`. CI fails if the tag doesn't match it.
2. Add `docs/release-notes/v<version>.md`, which becomes the release's text.
3. Commit, push, and wait for CI to pass.
4. Tag that commit and push the tag.

CI builds the installer on every push, installs it on its own machine, runs the validator and all the tests on the installed copy, and uninstalls it. `installer/test-installer.ps1` writes to Program Files, so don't run it on a machine you care about.

Rules for releases:

- **Don't change `AppId` in `installer/LoopTexturer.iss`.** It's how an upgrade finds the previous version.
- **Keep `SMTG_USE_STATIC_CRT` on,** which HadronycheCore's `cmake/HadronychePlugin.cmake` does. Without it, the plug-in fails to load on PCs without Microsoft's runtime, and neither the tests nor CI would notice, because that runtime is on those machines. If you configured a build before this option existed, its cached value overrides the default.
- **The installer must keep shipping the licence files.** The VST 3 SDK's, VSTGUI's and HadronycheCore's licences require their notices to accompany binaries.
- **Follow Steinberg's usage guidelines** (`external/vst3sdk/VST3_Usage_Guidelines.pdf`) wherever "VST" appears in product-related material: use ® on first mention, include the sentence "VST is a registered trademark of Steinberg Media Technologies GmbH.", show the logo from `docs/images/` unaltered, and don't put "VST" in product or company names.

## Known limits

- It has only been built and used on Windows, and Cubase is the only host it's been used in.
- Sample-exact grid alignment assumes that Cubase's reported position refers to the audio entering the plug-in. This hasn't been measured in Cubase, and an error would be at most 96 samples.
- Modes with sharp corners still alias at extreme settings: Hard clip at 14 kHz with 24 dB of drive has aliases at about -26 dB.
- The installer isn't code-signed, so Windows warns users when they run it.
- Starting a drag on a very long loop with the post saturator on can freeze the panel for a few seconds, because the export runs on the UI thread.
- Length automation written before version 3 plays back at different lengths, because hosts store it normalized and the range has since changed.
