# Studio Room Return — mixer-only presentation (Studio 0.2)

This focused change is based on **`integration/studio-0.2-integrated-alpha`**. It does not alter the Stage DSP, plugin chain, return bus, media rendering, recordings, MIDI or project data.

## Visible behavior

- Studio's internally managed `Studio Room Return` remains a genuine Tracktion `AudioTrack` with its `AuxReturnPlugin`, `studioWetRoomV1` and existing routing, solo isolation and output.
- The **arrangement/timeline does not create a row** for the managed return by default. Detection is by the canonical `spatial::isRoomReturn` role, *not the track name*; legitimate user tracks named "Studio Room Return" are not hidden.
- **Mixer:** it remains a dedicated track strip, styled with the wet-return accent and a visible **Stage Reverb** label and **WET FX RETURN** description. The wet gain/pan, mute, solo, output and metering still use the real audio track. No dead Stage send or Record Arm button; the internal reverb insert chain cannot be removed from that protected strip.
- **View or Mix → Show Stage Reverb Return in Arrangement** optionally reveals the underlying track for diagnostics. The setting is an **application UI preference** (`studio.showRoomReturnTrack`), and does not write to the project. Default: hidden. Disabling it also deselects a currently selected hidden return.
- The Tracktion example's generated `EditComponent` filters the row, and detects the native room role being assigned after creation. The code is generated via guarded, explicit CMake substitutions; no vendor source changes or global hide/mute are applied.

## Acceptance and safety

1. A Stage send creates one shared return. Open Mixer: **Stage Reverb** has real wet mix gain and audio meter. Arrangement shows only music tracks.
2. Reveal return using View/Mix menu, then hide it. Mixer channel, send bus and room plugin remain exactly the same.
3. Save/reopen an older project with active room routing. The return survives; the default arrangement stays clean. Closing/opening the Mixer must never remove the return.
4. Turn Stage sends/audio on and off; no additional return is created, no audible changes from merely hiding/revealing its row. Check solo and wet gain.
5. Confirm user-created track with the same text name but **without** Studio's internal role remains in arrangement.
6. Run `StudioUnifiedStage` (expanded with mixer/show/hide/reload assertions), `StudioSpatialRouting`, `StudioInterfaceReview`, mixer, sampler/synth and film scoring tests on the current integration branch tip. Intel macOS UI build and physical ARIA hearing verification are required before claiming fully validated.

No branches should be merged into `main` without explicit approval.
