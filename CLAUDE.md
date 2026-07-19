# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Dynamite is a JUCE-based digital recreation of the Valley People "Dyna-mite" vintage hardware compressor. It provides compression, gating, de-essing, and limiting as a VST3/AU plugin with 64-bit double precision processing.

## Behavioral Guidelines

- When given a concrete task, execute it directly. Do not start codebase exploration or ask clarifying questions before beginning the requested work. Only ask if you literally cannot proceed, and ask everything in one message — never in a loop.
- When a fix doesn't visibly resolve the user-reported symptom, re-investigate root cause rather than declaring success. Verify the symptom is gone, not just that the build compiled.
- When given a multi-step task, do all steps. Do not stop after one and ask "should I continue?"

## Workflow Rules

- After any code change, build **Release** (see Build Commands below) unless Debug is explicitly requested. If the build fails, fix the errors and rebuild — do not stop until it succeeds.
- Always use existing build scripts before creating new ones. Check CMakeLists.txt and `NewProject/build_test.sh` first — do not create new build scripts or wrappers unless explicitly asked.
- **Use specialized subagents for deep C++ work.** For tasks requiring deep knowledge of template metaprogramming, ABI-sensitive systems programming, lock-free/atomics design, or other advanced C++ domains, deploy the `cpp-dsp-specialist` agent via the Agent tool rather than attempting the work inline.
- **Verification loops for logic-heavy code.** For any non-trivial DSP or concurrency fix, state explicit verification criteria before coding and execute them before reporting done. Don't accept "fix a bug" as the full task — translate it into a runnable check. Example: "fix the threshold bypass" → run the threshold unit test (`NewProject/build_test.sh`) and confirm it passes.
- **Static analysis after C++ edits.** After editing any `.cpp`/`.h` file under `NewProject/Source/`, run `clang-tidy` on the changed files and fix any new warnings before building. If `compile_commands.json` is missing, generate it (see Tool Dependencies below).
- **clang-format:** format only files you otherwise modified; prefer `git clang-format` (changed hunks only) over whole-file `clang-format -i` — whole-file runs cause style-migration churn that buries the real diff.

## Debugging Guidelines

**Before debugging anything that takes more than 30 minutes:** Write three sentences first — (1) what you expected, (2) what happened, (3) what you've already ruled out. Don't open a file until those are written.

**Audio/DSP bugs:** Trace the full signal chain from input to output *before* proposing a fix. Do not guess incrementally. For this plugin, that chain is: Input → InputGain → LookAhead(64) → EnvelopeDetection → GainReduction → DryWetMix → OutputGain → Output. Common pitfalls: sample rate mismatches, unprepared DSP processors (missing `prepareToPlay`/`releaseResources`), look-ahead buffer alignment/latency errors, envelope time-constant errors at non-44.1k sample rates, dB↔linear conversion mistakes, float/double precision path divergence (`processBlock` vs `processBlockDouble`), and the threshold >= 0 dB absolute-bypass path.

**UI rendering bugs:** Consider component bounds, LookAndFeel state, and the 50 ms meter timer as likely root causes before investigating drawing logic. Meter misbehavior is usually a stale/unsynchronized gain-reduction value crossing from the audio thread, not a paint bug.

**Create a test from any bug you fix.**

## Build Commands (CMake)

```bash
# Configure (one-time)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build all formats (VST3, AU, Standalone)
cmake --build build --config Release -j$(sysctl -n hw.ncpu)

# Debug build
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug --config Debug -j$(sysctl -n hw.ncpu)

# Run threshold unit test
cd NewProject && bash build_test.sh
```

Plugins are automatically installed to `~/Library/Audio/Plug-Ins/` (VST3 and AU) after build via `COPY_PLUGIN_AFTER_BUILD`.

Build artifacts are in `build/Dynamite_artefacts/Release/` (VST3/, AU/, Standalone/).

## Legacy Projucer Build

The Projucer project (`NewProject/NewProject.jucer`) and Xcode project (`NewProject/Builds/MacOSX/`) still exist but the Xcode build currently fails due to missing VST3 SDK headers in the JUCE installation. Use the CMake build above instead.

## Requirements

- JUCE 8.0.12 installed at `/Applications/JUCE/` (used via `add_subdirectory` in CMake)
- CMake 3.22+
- Xcode / Apple Clang with C++17 support

## Tool Dependencies

**`compile_commands.json`** is required by `clang-tidy` and IDE completion. The Xcode generator does not emit it; generate it from a parallel Ninja configure:

```bash
# One-time setup; re-run only when CMakeLists.txt changes materially
cmake -G Ninja -B build-cc -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Debug
# Then symlink or copy: build-cc/compile_commands.json → compile_commands.json
```

There is no `.clang-tidy` / `.clang-format` config checked in yet; if one is added, keep the noisiest checks disabled and format only changed hunks (see Workflow Rules).

## Architecture

All source is in `NewProject/Source/`. The plugin follows a two-layer design:

### Plugin Wrapper Layer
- **NewProjectAudioProcessor** (`PluginProcessor.h/cpp`) — JUCE AudioProcessor subclass that owns the APVTS parameter tree and a `DynamiteProcessor` instance. Handles parameter synchronization, state save/restore, and delegates audio processing to DynamiteProcessor.
- **NewProjectAudioProcessorEditor** (`PluginEditor.h/cpp`) — Thin shell that instantiates DynamiteComponent.

### Core Layer
- **DynamiteProcessor** (`DynamiteProcessor.h/cpp`) — Self-contained DSP engine. Runs four parallel envelope followers (compressor, gate, de-esser, limiter) and combines their gain reductions into a single output. Uses a 64-sample look-ahead buffer for transient detection. Entry point is `process()` which routes to `processWithLookAhead()` or `processWithoutCompression()` depending on threshold.
- **DynamiteComponent** (`DynamiteComponent.h/cpp`) — Full custom GUI (900x250px). Contains a custom `KnobStyle` LookAndFeel for metallic rotary knobs, a gain reduction meter, and hover tooltips. Timer-driven meter updates at 50ms intervals.

### DSP Signal Flow
```
Input -> InputGain -> LookAhead(64 samples) -> EnvelopeDetection -> GainReduction -> DryWetMix -> OutputGain -> Output
```

Four gain stages are multiplied together: `compGain * gateGain * deesserGain * limiterGain`.

### Key Design Details
- **Threshold >= 0 dB** triggers absolute bypass: all processing stages return unity gain, only output gain applies.
- **Gain Reduction Index** (1-8) maps to fixed dB ratios: 1, 3, 6, 10, 15, 20, 30, 40 dB.
- **Detection modes**: INT (internal sidechain), DS (de-esser with IIR bandpass), EXT (external sidechain input).
- **Processing modes**: GR (gain reduction/compression), OUT (output monitoring), LIMIT (brick-wall limiting).
- **Detector types**: GATE, PEAK, AVG — each uses different envelope characteristics.
- All audio processing supports 64-bit double precision via `processBlockDouble()`.

### Parameter System
Parameters are defined in `PluginProcessor.cpp` via APVTS. The wrapper listens for changes via `parameterChanged()` and pushes values to DynamiteProcessor through setter methods like `setThreshold()`, `setRelease()`, etc. The GUI binds to parameters via `SliderAttachment` / `ComboBoxAttachment`.

### Audio Thread Discipline (STRICT)

"The audio thread" means any code reachable from `NewProjectAudioProcessor::processBlock` / `processBlockDouble` and its transitive callees — the entire `DynamiteProcessor` DSP engine (envelope followers, look-ahead buffer, sidechain filters, gain stages) plus the parameter setters invoked from `parameterChanged()` when the host calls it on the audio thread. Code in this path is presumed audio-thread unless explicitly documented otherwise.

**Strictly prohibited on the audio thread:**

- **Heap allocation** — `new`, `delete`, `std::make_unique`, `std::vector::push_back`/`resize` (if it can grow), `std::string`/`juce::String` construction or concatenation, `std::map`/`std::unordered_map` insertion, `std::function` assignment from a lambda with captures.
- **Locks that can block** — `std::mutex::lock()`, `juce::CriticalSection::enter()`, `std::condition_variable::wait*`. Use `std::atomic<T>`, `juce::AbstractFifo`, or `juce::SpinLock::ScopedTryLockType` (try-lock with skip-on-fail) instead.
- **Blocking I/O** — file reads/writes, network calls, any logging that hits disk. `jassert`/`jassertfalse` are permitted since they compile out in Release.
- **Exceptions** — `throw` and anything that may throw across the callback boundary.
- **Virtual dispatch into code that may do any of the above** — if a base-class method is called from the audio callback, every override must also comply.

**Allowed:** pre-allocated buffers sized during `prepareToPlay` (the look-ahead buffer is sized there), `std::atomic<T>` for scalars and flags, lock-free queues, fixed-size ring buffers, trivial arithmetic and memcpy.

**GUI ↔ audio thread:** the gain reduction meter reads values written by the audio thread on a 50 ms Timer — those values must cross via `std::atomic<T>`, never via locked shared state.

**Enforcement:** any change touching a function reachable from `processBlock`/`processBlockDouble` should be verified against the threshold unit test (`NewProject/build_test.sh`) or a sanitizer build — not just "it compiled."

## C++ Style

Use modern C++ patterns (C++17). Specifically:

- **Prefer `std::unique_ptr` over raw owning pointers.** Raw pointers are for non-owning observation only. For JUCE `Component` children, use `std::unique_ptr<T>` members; for polymorphic ownership, use `std::unique_ptr<Base>`. Never `new`/`delete` manually — use `std::make_unique<T>(...)`.
- **Mark return-value-significant functions `[[nodiscard]]`.** Any function whose return value carries an error code, a `Result`, a handle, or a value that should not be silently dropped must be `[[nodiscard]]`. This includes factory functions, `try*()` accessors, and anything returning `juce::Result` or `std::optional`.
- **Adhere to const-correctness.** Member functions that do not modify observable state must be `const`. Parameters passed by reference that are not written to must be `const T&`. Local variables that are not reassigned should be `const`. Audio-thread read-only methods must be `const` — this makes thread-safety boundaries visible at the type level.
- **Header hygiene: forward-declare, don't over-include.** In public headers (`.h`), prefer forward declarations over `#include` whenever only pointers or references are used. Move full `#include` into the corresponding `.cpp`. Headers should compile as fast as possible.
- **Keep the code lean and clean.** Write the smallest change that solves the problem. Delete dead code rather than commenting it out, avoid speculative abstractions and unused parameters, and don't add a layer of indirection a caller doesn't need. Every added line is a line to be maintained — prefer removing code to adding it.
- **Reuse existing functions instead of creating new ones.** Before writing a helper, search for one that already does the job (or nearly does). Extend or call the existing function rather than duplicating its logic; if two call sites need the same behavior, factor it into one place. New near-duplicates of existing utilities are a review-stopper.

## Conventions

- Classes: PascalCase. Methods: camelCase. Constants: `kPrefixedCamelCase` or `SCREAMING_SNAKE_CASE`.
- JUCE idioms: `ChangeBroadcaster`/`ChangeListener` for events, `CriticalSection`/`SpinLock` for thread safety, `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR` on components.
- Real-time safety: no heap allocations, no locks, no blocking I/O in audio callbacks. Use lock-free structures for audio thread communication (see Audio Thread Discipline above).
- Smart pointers (`std::unique_ptr`, `std::shared_ptr`) for ownership; JUCE components may use raw pointers only for non-owning observation with clear ownership comments.
