# AGENTS.md

Guidance for coding agents working in this repository. Read [Rules that protect users' work](#rules-that-protect-users-work) before changing anything.

LoopTexturer is a VST® 3 insert plug-in for sound design in Cubase on Windows: an effects chain plus a tape-style looper whose audio can be dragged onto the timeline. It uses the Steinberg VST3 SDK and VSTGUI directly. JUCE and ARA were considered and deliberately rejected, so don't reintroduce them.

`docs/signal-path.md` is the behaviour specification. `README.md` covers building and usage.

Code shared with the other Hadronyche plug-ins is in the `HadronycheCore` submodule, at `external/hadronychecore`: the parameters' curves and saved format, the frame that the Rust side fits into on both sides of the boundary, the module entry, `PanelEditor`, `Smoother`, the state-variable filter kernel, the test host, the build setup and CI. Its `AGENTS.md` has its own rules.

The signal chain, the looper, its worker thread, the loop's saved state and the parameter table are Rust, in `rust/`. The crate implements HadronycheCore's `Processor` trait, and depends on its `rust/hadronyche-plugin` and `rust/hadronyche-dsp` through the submodule. `hadronyche_add_rust` in `CMakeLists.txt` builds it into the plug-in, and the C++ in `src/` is the VST3 classes around it, on `RustProcessor` and `RustController`, with the panel, the export and the LV2's link reading the looper through the C functions in `rust/src/ffi.rs`, which `src/LooperApi.h` is generated from. Rust from [rustup](https://rustup.rs) is a build requirement on every system.

## Commands

Windows x64 with Visual Studio 2022 and CMake 3.25 or newer is where this is developed. macOS is built and tested by CI, on Intel and Apple Silicon, so see [macOS](#macos).

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

There's no linter or formatter for the C++. In `rust/`, `cargo fmt` formats, and `cargo clippy --all-targets -- -D warnings` and `cargo test` have to pass. Three of those tests check that `src/ParamIds.h`, `src/LooperIds.h` and `src/LooperApi.h` are what the Rust side generates, and `HADRONYCHE_UPDATE_HEADERS=1 cargo test` rewrites them.

Follow the existing style, which comes from the SDK: a space before call parentheses (`foo (x)`), `_member` for private members, `kName` for constants, and comments that explain why rather than what.

## Rules that protect users' work

People use this plug-in in real projects. Breaking these rules damages saved projects or audio, and usually nothing fails at build time.

### Saved projects

- **Never change the Audio Unit codes,** `aufx` `LpTx` here and the manufacturer `Hdrn` in HadronycheCore. Hosts use them to find the plug-in in saved projects.
- **Never change the class IDs in `src/PluginConfig.h`.** Cubase uses them to find the plug-in in saved projects.
- **Never reorder or remove an entry in `Param`** (`rust/src/lib.rs`). Their positions are saved in projects and automation, and `src/ParamIds.h` is generated from them. Add a new parameter at the end, with its spec on the same line.
- **Don't change the range or curve of an existing parameter.** Hosts store values normalized to 0..1, so this silently changes what saved projects sound like. If it's unavoidable, migrate old values and bump the state version.
- **The label lists in `rust/src/lib.rs` must match the enums they name** (`Wave`, `SvfMode`, `saturator::Mode`, `LooperBars`), whose `from_index` functions take a parameter's value. Only ever add to the end of these lists.
- **Changing the saved format requires bumping `STATE_VERSION` and still reading every older version.** The format is:
    1. HadronycheCore's `ParamState.h`: `int32` version, `int32` count, then `int32` ID and `double` normalized value pairs.
    2. From version 2, the loop (`write_loop_state` in `rust/src/lib.rs`, through the frame's `save_state`): `int32` has-loop flag, and if set, `int32` mode, `int64` frames, 4 `double`s (quarters, anchor, frames per quarter, write speed), then all left samples and all right samples as `float`.
    3. Version 3 has the same layout. It extended the looper's length from 10 s to 30 s, so `migrate_param` converts that parameter's value when reading older versions. Automation saved by hosts can't be converted.
- **The Rust port is bit for bit what the C++ was,** which HadronycheCore's `tools/nulltest/` showed against the last C++ build, so a change in the sound is a change, not rounding. Keep the arithmetic in the order it's written, `f32` and `f64` where they were, the gains through `f32` as `db_to_gain_f32` does, and a power of 2 through `hadronyche_dsp::pow2_f32`.
- **Loading must tolerate missing or damaged data.** An older state has no loop section, which reads as having no loop.

### The audio thread

`LoopTexturer::process` in `rust/src/lib.rs` and everything it calls runs on the audio thread: `Looper::begin_block`, `process_sample`, `end_block` and the rest of the chain, apart from what's listed below.

- **No allocation, locks, file access, system calls or anything else that can block.** This includes a `Vec` growing, a `Mutex`, and cloning an `Arc`. It's the reason for much of the design below, and the failure is audio dropouts that may not show up in testing, though `lv2-matches-vst3` fails if a block allocates.
- **Work that takes too long for one block has to be spread out or moved elsewhere.** `Looper::erase_some_of_free_tape` clears a little per block, and large tapes are allocated and freed by the worker thread.
- `Tape::new`, `Looper::prepare`, the bridge's tape functions, the loop's state and `render_loop` are not for the audio thread.
- **A panic is a bug, even though HadronycheCore's boundary catches it.** After one, that instance of the plug-in stays silent for good, which is what a user would notice.

### Sound and timing

- **The reported latency must always be true.** It's 96 samples: 48 from each saturator (`saturator::LATENCY`), at every sample rate and whether or not they're on, so that toggling them doesn't shift audio. Anything that changes it must update `latency`, the dry delay in `process`, the `input_latency` given to `Looper`, and the samples skipped in `render_loop`.
- **Broken samples at the input become silence, before anything else.** A NaN or infinite sample would otherwise stay in the filters and the loop for ever, and `broken-input` checks this.
- **`saturator::TAPS` must stay of the form 4n + 1.** That's what makes its latency a whole number of samples, so that the dry signal stays perfectly aligned.
- **The looper reads and writes with one head at one position.** Separate heads, or any delay in the feedback path, would misalign overdubs and shift the loop on every pass.
- **At 100% feedback, the loop must stay bit-exact.** Feedback works directly on stored cells, and `write_span` skips writing when nothing changes. Don't route feedback through an interpolated read.
- **In tempo sync, the head's position is calculated from the host's musical position.** Don't accumulate it instead, or the loop will drift off the grid. This is also what makes a tempo change repitch the loop.
- **The signal path is in single precision, and has to stay that way.** Use `Smoother::step_f32` on it, and `db_to_gain_f32`. Going through `f64` instead still compiles, and changes the sound of saved projects.
- **`Smoother` must stay double precision.** In single precision it stalls about 0.01% short of its target, which makes free running loops the wrong length.

### Decisions made by the project owner

These were chosen deliberately, so don't "fix" them without the owner's agreement:

- The post saturator processes only the loop's output, before dry/wet. Feedback doesn't pass through it, so it can never change the recorded loop.
- Feedback goes up to 200%, and it runs on every pass, not only while overdubbing.
- After its first pass, Rec moves on to overdubbing in both modes.
- Switching looper mode clears the loop, after confirmation in the UI. The mode parameter isn't automatable, so automation can't wipe a loop.
- The looper's buttons are VST3 messages rather than parameters.
- There's no dragging on Linux, because VSTGUI can't start a drag there. People render or record the output instead, and the waveform only draws.
- The dragged WAV is the loop as heard: at its current speed and through the post saturator, without the dry signal or Volume.
- Exported WAVs are never deleted, because Cubase may keep referencing them.
- Saturators add constant latency, because this is a sound design tool rather than a performance tool.

In `docs/signal-path.md`, **Requirement** marks the owner's decisions and **Proposed** marks suggestions that were accepted by default. Update it when behaviour changes, and don't turn a Proposed item into a Requirement yourself.

### The repository

- **Don't edit anything in `external/vst3sdk`.** It's a submodule pinned to `v3.8.1_build_84`. `git submodule status` shows a misleading `v3.7.3...` label for that same commit, so check with `git -C external/vst3sdk describe --tags`.
- **Don't edit `external/hadronychecore` from here.** Change it in its own repository, and move the submodule here afterwards, once the new version has been compared with the old as its `AGENTS.md` describes.
- **The Rust side builds from the submodule, even with `HADRONYCHE_CORE_DIR` set,** because Cargo can't redirect a path dependency. To work on both, point the submodule at the commit in the other checkout, and the configure stops when the two differ.
- Don't change git configuration. Commit or push only when asked.

## Architecture

### Signal path

`LoopTexturer::process` in `rust/src/lib.rs` runs this for every sample:

```
In → Gain → Pan (LFO) → Filter (LFO) → Saturator ─┬─ 48-sample delay ──────────────┬→ Dry/wet → Volume → Out
                                                  └→ Looper → Post saturator ──────┘
```

### Threads and ownership

Understanding this is necessary before changing the looper:

| Thread | Runs | Talks to the others through |
| --- | --- | --- |
| Audio | `RustProcessor::process`, then `LoopTexturer::process` and the `Looper` in Rust | Atomics only |
| UI | `Editor`, `WaveformView`, `exportLoop` | Reads the bridge through `src/LooperApi.h`, sends VST3 messages |
| Host | `setState`, `getState`, `setActive` | Atomics, and handing over a `LoadedLoop` through the bridge |
| Worker (`Worker::run` in `rust/src/lib.rs`) | Allocates and frees tapes | The bridge's `requested_sync_frames`, `requested_free_frames`, the offered tapes in its mailbox, and its `tape_change` lock |
| LV2 messages, in the LV2 only | The wrapper's message thread: the buttons' messages to `Processor::notify`, and `src/lv2/LooperLink.cpp`'s copies for the panel | Reads the bridge as the UI does, and lock-free queues to the audio thread |

How the pieces connect:

- **Parameters:** the host sends changes through `process`, and they land in `RustProcessor` as atomics, which hands the Rust side a snapshot with each block. The last value per block is used, followed by smoothing. `PARAMS` in `rust/src/lib.rs` is the one definition of every parameter's range and text: the Rust side reads it directly, and `Controller`, `Editor` and `src/lv2/PluginInfo.cpp` read it through `Hadronyche::RustPlugin::getSpec`.
- **Buttons:** `Editor` → `Controller::sendLooperCommand` → VST3 message → `Processor::notify` → `looptexturer_bridge_send_command`, an atomic bitmask in the bridge's mailbox, read at the start of the next block.
- **Status and audio for the UI:** the `Bridge` in `rust/src/bridge.rs` holds status atomics written once per block, and the tapes. `Tape` stores samples as relaxed atomics so that the UI can read while the audio thread writes without a data race. The C++ `LooperBridge` is a view of it, over the C functions.
- **Finding the bridge:** the processor sends only the bridge's ID in a message, which `RustProcessor::shared ()` gives it, and the controller looks it up in the Rust side's registry of weak pointers. This avoids dangling pointers, whatever order the host destroys things in. It relies on the plug-in not being distributable (the `0` flag in `src/PluginEntry.cpp`), so that both sides share one process. The LV2 can't rely on that, so its panel gets a copy instead, as [Linux](#linux) describes.
- **Tape lifetime:** the bridge owns tapes through `Arc`, and the audio thread holds plain pointers. The worker releases only tapes outside the mailbox's `tapes_in_use`. Hold the bridge's `tape_change` lock while adding a tape until the looper knows about it, or the worker could release it.
- **Restoring a loop:** `RustProcessor::setState` restores the parameters first and then calls `load_state`, which hands over the `LoadedLoop`. If the order were reversed, the audio thread could see the old mode, treat it as a mode switch and clear the new loop.

### The looper

`rust/src/looper.rs` is a tape with one head. The modes only differ in what moves it:

- **Free running:** a 30 s tape, where the length knob sets the head's speed. Changing the speed repitches the loop. Stop leaves the tape standing still. A restored loop keeps the tape size it was saved with, so `_freeTape`'s capacity can differ until that loop is cleared, when the worker provides a full tape.
- **Tempo sync:** a tape sized for 64 bars at the current tempo plus 25%, allocated by the worker when you enter this mode. `_syncState` goes from `Empty` to `Recording` to `Loop`.

`write_span` fills every cell the head passed, ramping between input samples, so audio recorded at any speed plays back cleanly at that speed. `Tape::read` averages cells when playing faster than the audio was recorded, to limit aliasing. Punching in and out uses short envelopes to avoid clicks, and at the end of a recording these also crossfade the loop's seam.

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

`.github/workflows/ci.yml` builds Debug and Release and runs all of this on every push and pull request, through HadronycheCore's `plugin-ci.yml`, whose commit it pins. Don't weaken or skip it to get a change through. When the submodule moves, move the commit in `ci.yml` with it.

`tests/` builds one program, `LoopTexturerTests`, which loads the built plug-in and drives it through the VST3 interfaces, the way Cubase does. It doesn't link the plug-in's code, so tests can only see what a host can.

| File | Covers |
| --- | --- |
| `CompatibilityTests.cpp` | Class IDs, parameter IDs, ranges and list order, and the saved format |
| `ChainTests.cpp` | Latency, levels, pan, LFOs, filter, saturators and aliasing |
| `LooperTests.cpp` | Both looper modes, feedback, dry/wet and saved loops |
| `PanelTest.cpp` | The real editor, driven with mouse messages, including the drag export |
| `lv2/LooperLv2Test.cpp` | On Linux, the LV2 and its panel, with the messages between them carried as a host does |

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

CI builds the installer on every push, installs it on its own machine, runs the validator and all the tests on the installed copy, and uninstalls it. HadronycheCore's `tools/windows/test-installer.ps1` writes to Program Files, so don't run it on a machine you care about.

Rules for releases:

- **Don't change `AppId` in `installer/LoopTexturer.iss`.** It's how an upgrade finds the previous version.
- **Keep `SMTG_USE_STATIC_CRT` on,** which HadronycheCore's `cmake/HadronychePlugin.cmake` does. Without it, the plug-in fails to load on PCs without Microsoft's runtime, and neither the tests nor CI would notice, because that runtime is on those machines. If you configured a build before this option existed, its cached value overrides the default.
- **The installer must keep shipping the licence files.** The VST 3 SDK's, VSTGUI's and HadronycheCore's licences require their notices to accompany binaries.
- **Follow Steinberg's usage guidelines** (`external/vst3sdk/VST3_Usage_Guidelines.pdf`) wherever "VST" appears in product-related material: use ® on first mention, include the sentence "VST is a registered trademark of Steinberg Media Technologies GmbH.", show the logo from `docs/images/` unaltered, and don't put "VST" in product or company names.

## macOS

- **Build with the Xcode generator,** which the SDK's Audio Unit wrapper needs: `cmake -S . -B build -G Xcode`, then `cmake --build build --config Release --target LoopTexturerTests LoopTexturer LoopTexturer-au`.
- **`CMAKE_OSX_DEPLOYMENT_TARGET` has to stay before `project ()`,** or the plug-in only loads on macOS as new as the build machine, and only an older Mac would notice. `test-installer.sh` checks for this, because a host on the build machine wouldn't.
- **The SDK's `-ffast-math` stays off,** as HadronycheCore's AGENTS.md explains.
- **The installer is built and tested by HadronycheCore's `tools/macos/`.** `test-installer.sh` writes to `/Library`, so it's meant for CI machines.
- **The panel test only exists on Windows,** because it uses Windows' mouse messages. Nothing automated opens the panel on macOS, so its look needs checking by eye.

## Linux

- **Use one build folder per configuration, with Ninja:** `cmake -S . -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release`, then `cmake --build build/Release --target LoopTexturerTests LoopTexturer`. The SDK's Linux output paths break with a multi-configuration generator.
- **`external/hadronychecore/tools/linux/install-build-packages.sh` installs the build's packages.** gtkmm and SQLite are only needed to configure, and the plug-in doesn't link them.
- **`panel-opens` needs an X server,** so run `xvfb-run -a ctest ...` where there's no display. It opens and closes the real editor, and nothing more: `panel`, which drives it, only exists on Windows, so the panel's look and its controls need checking by eye.
- **The release is built in an `ubuntu:22.04` container on CI, not on the runner,** because a binary only loads on a glibc as new as the one it's built against. HadronycheCore's `tools/linux/test-tarball.sh` checks this, and CI runs the tarball on other distributions too.
- **The LV2 is HadronycheCore's wrapper around the VST3's own classes.** Its `AGENTS.md` has the rules. It has audio, control ports, state with the loop, latency, transport and the VST3's panel. `lv2-matches-vst3` compares it with the VST3, bit for bit.
- **Never change the URI, the panel's URI, the symbols in `src/lv2/PluginInfo.cpp` and the ports `ui_in` and `ui_out`.** They've been released, and hosts use them to find the plug-in and its values in saved projects. A new parameter adds a symbol at the end.
- **The LV2's panel never shares memory with its processor,** because a host can run it in another process. `src/lv2/LooperLink.cpp`, which only the LV2 builds, keeps the processor's bridge ID on the audio side, and sends the panel copies of the bridge's status and of the tape's overview buckets instead, about 30 times a second and only the parts that changed. The panel side builds a mirror bridge of its own from them, with `LooperBridge::createMirror`, and introduces it with the processor's own message, so `Controller` and `Editor` are unchanged. The buttons' messages go back unchanged. Keep it that way: what the LV2 needs differently goes in `src/lv2/`, around the VST3 classes, not in them.
- **The copied waveform uses the tape's own overview buckets,** so it matches the VST3's, except that a column's edge can fall one bucket differently, which moves a few columns by a few pixels. A coarser copy showed visibly different peaks.
- **`lv2-looper` runs the LV2 and its panel as a host does,** carrying the messages itself, and clicks the real buttons. It checks the sound, what the panel draws, a panel opened later, a restored loop, damaged messages both ways, a host that doesn't pass messages back, and one without a URID map. `tests/lv2/LooperLv2Test.cpp` uses the same positions as `PanelTest.cpp`. Set `LOOPTEXTURER_SCREENSHOTS` to a folder to save the panel as PPM files.
- **There's no dragging,** which is the owner's decision. `kCanDrag` in `Editor.cpp` turns it off, and `panel` doesn't exist here to test that.

## Known limits

- It has only been used on Windows, and Cubase is the only host it's been used in.
- Sample-exact grid alignment assumes that Cubase's reported position refers to the audio entering the plug-in. This hasn't been measured in Cubase, and an error would be at most 96 samples.
- Modes with sharp corners still alias at extreme settings: Hard clip at 14 kHz with 24 dB of drive has aliases at about -26 dB.
- The installer isn't code-signed, so Windows warns users when they run it. The macOS one isn't signed by Apple or notarized, so macOS refuses it until the user allows it.
- The Linux VST 3 and the LV2 have only been tried briefly, in REAPER on Debian 13, by the owner.
- The test that opens the panel used to crash rarely on CI, as the test program unloaded the plug-in. Pango's font thread was still running in it. Plug-ins now link with `-z nodelete`, as HadronycheCore's `AGENTS.md` explains, which removes that cause. The crash was too rare for reruns to prove it gone.
- The Mac version hasn't been used in a real host on a real Mac. The tests measure its output, and Apple's `auval` validates it, on CI only.
- Starting a drag on a very long loop with the post saturator on can freeze the panel for a few seconds, because the export runs on the UI thread.
- Length automation written before version 3 plays back at different lengths, because hosts store it normalized and the range has since changed.
