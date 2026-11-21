# Studio 0.2 — Unified integration of Milestones 4.6, 5 and 6

This isolated review branch combines the **native synth**, **native sampler**, and **Studio Environments** welcome screen. It is based on native-synth Milestone 6 head `d6e621282f1643d4f16a893b2321989f21a00181` (which includes native sampler Milestone 5), with the welcome-screen implementation carried over from `feature/studio-environments-m46` at `5d27eb8aa0fbc3407a4f46c845ed95740a999011`.

No milestone branch or `development` was merged or rewritten; this integration is a new, independent branch.

## Build on an Intel MacBook

In a fresh clone, or after switching an existing checkout to this branch, use the **same pinned Tracktion Engine** dependency as both milestone branches (commit `c6293127cdc0a8488aea2ae0907f4709ee89c228`). Configure **after** switching branches, so CMake includes the new binary artwork and UI source files:

```bash
/Applications/CMake.app/Contents/bin/cmake -S . -B build -G Xcode \
  -DTRACKTION_ROOT="$PWD/build/deps/tracktion-engine" \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0
/Applications/CMake.app/Contents/bin/cmake \
  --build build --config Debug --target Studio --parallel 4
open "$PWD/build/Studio_artefacts/Debug/Studio.app"
```

If building in an existing Studio checkout, ensure its `build/deps/tracktion-engine` exists before configuring, or set `TRACKTION_ROOT` to the existing dependency path. The separate original `M4.6`, `M5`, and `M6` branches remain intact.

## Integration acceptance

- Verify the rotating welcome images, all three project actions, preference persistence, and reopening from View.
- Create sampler and synth tracks in a single project; audition, record MIDI, save and reopen, then render an audio bounce.
- Verify all seven synth families and a sampler with live notes; ensure mixer, stage send, room return, and no audible regressions.
- Confirm ARIA AU hosting, restoration and save protection using a **copy** of an existing project.
- Verify the M4.5 draggable lower-workspace divider and compact/split/expanded/hide modes.
- Confirm the 1024 × 640 large-text Welcome Screen view and all native editor screenshots.
- Run the complete x86_64 CTest suite and acoustic comparison.

## Next milestone

A separate Milestone 6.5 should create a docked-by-default, optionally floating instrument editor host for Studio Sampler and Studio Synth; this branch preserves their current working floating editors and existing DSP. Do not conflate the UI redesign with integration validation.
