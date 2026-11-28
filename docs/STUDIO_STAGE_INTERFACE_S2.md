# S2 — Stage Professional Interface

Base: verified integrated algorithmic Stage, 14ea6b6160f8fe028a0d5c902834a4f761a8e553 (PR 121). Composer confirmed first successful listening on an Intel MacBook. This branch is interface-only. No acoustic algorithms, factory reverb parameters, routing, project state schema or file resources are changed.

- Physical Stage retains its rectangular acoustic coordinate domain and existing screen/inverse mapping. Concentric arcs are visual seating guides, not a nonlinear acoustic projection or a restrictive curved boundary. Larger outline instrument icons adapt to marker density. Microphone capsules, direction/polar diagrams and listener are visible in the physical Stage. The microphone view remains the editing surface for microphone placement. Track dragging, atlas assignments, keyboard navigation and compact overview are preserved.
- Direct level, source width and reflections are exposed in the selected source sidebar using existing source properties and undo transactions. Remaining source controls (orientation, height, radiation family, directivity and reset) remain in Advanced source. Legacy scenes still require explicit adoption. Float control shares the main toolbar. Very narrow docks still scroll.
- Virtual microphone mixer uses existing theme-aware console faders, pan knobs, segmented meters, editable delay, mute/solo and exact numerical values. Meters are the previously implemented peak maximum of individual direct/early capsule contributions, not a summed microphone bus or late-field meter. No new DSP. Array preset, placement, spacing and angle are presented before the placement map. Existing setups retain their identities and names.
- Studio Reverb Basic has six rotary controls (decay multiplier, wet mix, predelay, wet trim, size and diffusion) and quality selector. Advanced retains all existing detailed fields. Input/output peak meters read the existing plugin atomics. Frequency-dependent decay curve displays estimated targets, not a measured impulse waveform. Window is smaller by default; advanced and narrow views retain scroll access. Graphite and Deep Black use the existing look and feel.
- Full Intel/Linux regression workflows include this branch. Prior integrated Stage DSP test results are not evidence of S2 build success; verify current commit CI and physical laptop appearance.

## Local build

From the existing Studio-FilmScoring-M1 checkout, quit Studio and check for tracked changes. Fetch and switch to feature/studio-stage-interface-s2. Run CMake configure (-S . -B build) before building the StudioStageAssets and Studio targets in Release, then open build/Studio_artefacts/Release/Studio.app. Do not delete untracked build folders.

## Acceptance

1. Open a copy of the known-good piano/flute project. Compare sound before changing controls. Verify both sources remain in their original seats and route only once.
2. Drag both sources and use atlas assignments; confirm left/right and front/back unchanged. Test direct level, width and reflections in inspector, undo and reopen. Verify legacy scene controls stay disabled until adoption.
3. Place ORTF/Decca-inspired array, select monitor microphones, move a capsule. Verify fader, pan, delay, mute/solo, numerical readouts and meter while sounding. Confirm listener mode still works.
4. Open Studio Reverb: switch presets, move knobs, change quality, verify meters, bypass, save and reopen. Compare existing preset sound unchanged before adjustment. Check Basic/Advanced scrolling and editing.
5. Review docked/expanded/floating Stage and effect editor at 1024x640 and 1280x800, normal and largest text, Graphite and Deep Black. Check control labels, no overlap, no inaccessible controls. Actual new screenshots and physical composer acceptance pending.

No convolution, laboratory, routing or acoustic changes; no merge.
