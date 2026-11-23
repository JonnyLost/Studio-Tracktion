# Cinematic Atelier Instrument Editor — Milestone 1

This is the **first working implementation**, not the approved full-screen concept art rendered as a static GUI. It is a lightweight, capability-driven JUCE editor for **hosted Audio Unit instruments**. The app's existing Studio Synth and Studio Sampler editors remain unchanged.

## Interface

- Uses the **already bundled Blue-Hour Study** illustration (including the original Beethoven portrait), decoded and downscaled once per open editor; there are no new audio-thread allocations, 3D rendering, subscriptions, assets or external network calls.
- Dark walnut/amber materials, photograph confined to the portrait panel, opaque readable controls, adaptive stacked layout on narrower docks.
- Instrument title reflects the **actual selected track**, subtitle displays the **actual Audio Unit name**. No invented preset name, articulation selection, MIDI channel, or room microphone settings.
- **Volume and pan** operate on the selected track's existing final `VolumeAndPanPlugin` using the same setters as the Mixer. They reflect mixer changes while open and create undo transaction boundaries for user gestures.
- **Expression** is shown **only** if the Audio Unit exposes a real automatable parameter named Expression; its range and existing parameter gestures are respected. It is not a guessed MIDI CC11 translation.
- **Articulation Mappings** and **Open Orchestral Stage** navigate to Studio's existing editors. Articulations are not fabricated from patch names; Stage and shared-room routing continue to be owned by the current Stage workspace.
- **Open Original Instrument** invokes the *same hosted plugin instance's* original editor, with no new instrument or duplicate audio routing. Preset editing and unsupported plugin-specific functions remain there.

## Workflow

- Existing instrument action opens the native Synth/Sampler editor as before, or the **Atelier view for a hosted Audio Unit**. View -> Cinematic Atelier Instrument explicitly opens the selected instrument.
- The **same `InstrumentWorkspace` instance** docks, floats and returns; layout and floating geometry use its existing saved per-project/app state. The chosen Atelier presentation is remembered separately from the native Synth/Sampler editor.
- The Mixer's isolated focus remains unchanged. Loading the Studio Editor only changes workspace visibility.
- In a compact dock, **Open Original Instrument** remains the primary visible action; the Stage and Articulation panels remain reachable through the Studio dock.
- On project reopen, saved Atelier view is restored only when ARIA restoration is not awaiting confirmation; the ARIA state-protection path remains the authority.

## Milestone 1 non-goals / safeguards

- No synthesis, MIDI routing, timing, DSP, mic simulation, automatic patch discovery or direct AU state mutation.
- No pretend articulation buttons, fake virtual keyboard, or unbound knobs.
- No standalone illustration cropped from the approved mockup; it contains painted controls. Separate optimized instrument portraits are a later artwork pass.
- No changes to existing track presets, project schema, or Audio Unit host architecture.
- No branch merges. The feature branch is stacked on the unmerged M14 PR because it reuses the M14 instrument workspace.

## Verification required before acceptance

1. Linux and Intel macOS CI compile, full unit suites, Stage routing and ARIA protection checks.
2. On an Intel Mac, load an actual hosted AU (e.g. ARIA) with a selected instrument track; choose **View -> Cinematic Atelier Instrument**.
3. Volume and pan are bidirectional with Mixer, do not modify Audio Unit presets or duplicate instances, and work during playback.
4. **Original Instrument** opens the same AU original editor; close it and return to Atelier.
5. Articulation button opens existing mapping editor; Stage button selects the same track and opens Stage.
6. **Float**, **Dock**, **Expand**, window resizing and restoring state after relaunch retain the same instrument and uninterrupted audio.
7. Verify 1024×640 and 1280×800 layouts at 100% and 150% Studio scale; compact viewport may scroll but critical controls must remain reachable.
8. During ARIA recovery warnings, no previously saved AU state may be overwritten or autosaved without confirmation.

Potential follow-ups: add a separately authored violin portrait, capability-driven native Studio Synth/Sampler presentation, accurate MIDI-channel routing control, keyboard audition via selected live route, and optional direct mapped articulation tiles.

Milestone 1 serves as a reviewable development branch, **not** an instruction to install during the M14 acceptance test.

## Initial MacBook screenshot review — layout correction

Two actual MacBook captures of the first Atelier editor (docked and floating) confirmed the new hosted-ARIA view opens and moves between Studio's lower workspace and a separate window. They also revealed an unacceptable presentation regression: JUCE's rotary controls were stretched to fill available panel space, producing enormous knobs in the floating window and missing visible control labels.

The follow-up changes cap the physical width of volume, pan and optional Expression controls at `appearance.span(125)`, add explicit VOLUME/PAN labels, align the title and real AU name as a vertical hierarchy, and retain the same linked Tracktion fader/parameter instances. The bounds regression helper now verifies dial size and label containment. These changes do not alter plug-in loading, project state, audio graphs, Stage processing, or Float/Dock lifetimes.

**Still not visually complete:** the portrait still displays the cached Composer Studio Blue-Hour Study scene, not an instrument-specific violin illustration, and the full photographic mockup is a future design reference rather than an implemented control map. Do not claim these screenshots represent the final Cinematic Atelier look.
