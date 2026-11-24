# Cinematic Atelier M4 — live MIDI controller knobs

This is a stacked UI and controller milestone built upon `feature/studio-atelier-full-experience-m3`. **Do not merge without explicit approval.**

## Studio interface

The new **MIDI CC** instrument tab works inside the existing **fixed 425-point Split dock**, in the normal floating instrument, and in the enlarged/full editor. The approved portrait remains visible. This deliberately avoids putting eight tiny controls into the Main view or reducing the arrangement area.

A dedicated set of eight bronze rotary knobs supplies common MIDI 1.0 channel controllers:

| Control | CC | Initial display value | Semantics |
| --- | ---: | ---: | --- |
| Modulation | 1 | 0 | Mod wheel, often vibrato or orchestral dynamics |
| Breath | 2 | 0 | Breath, only if instrument responds |
| MIDI Volume | 7 | 100 | **MIDI channel volume**, not Studio's Mixer fader |
| Expression | 11 | 127 | **MIDI expression**, distinct from any AU automatable parameter |
| Sustain | 64 | 0 | Two-state pedal, 0 off / 127 on |
| Resonance | 71 | 64 | Filter resonance/timbre when implemented |
| Brightness | 74 | 64 | Filter cutoff/timbre when implemented |
| Reverb | 91 | 40 | **MIDI reverb send**, not Studio's native Stage room-send |

These are standardized **message numbers**, not a promise that a particular ARIA patch responds to each. Studio does not invent a patch mapping. Tooltip and help text communicate the difference.

## Routing and state

- Turning a knob sends a live `MidiMessage::controllerEvent(channel, cc, 0…127)` via **the same selected `AudioTrack::injectLiveMidiMessage` path** as the existing Atelier keyboard and Studio Articulation Palette.
- The same explicit **preview MIDI channel** (1–16) is shared with the keyboard, including in the small dock. This does **not** modify project MIDI clips, track channel filters, AU presets, or Studio mixer volume.
- Knobs are unavailable unless an enabled hosted Audio Unit has an active playback context and the track is not frozen/recording.
- **Send values** explicitly reapplies all eight displayed values after opening an instrument. Opening/reopening projects and switching tabs does **not** silently replay controller messages.
- The view state is v5: it remembers seven continuous CC knob values, preview channel, selected portrait, and page **per instrument instance**. v2–v4 view state continues to restore. Sustain is intentionally always restored Off.
- CC64 On is tracked by channel. The pedal is actively released on page/channel changes, editor destruction, or when preview availability becomes invalid, reducing risk of stuck notes. Actual sounding notes still receive the existing keyboard note-off lifecycle.
- Slider dragging sends live events; project preference updates are delayed until a gesture ends to avoid excessive GUI/serialization work.
- No audio engine, DSP, instrument instance, MIDI track data, commercial plug-in or new framework is introduced.

## Screenshot correction

The legacy "CHANNEL" text duplicated and visibly overlapped the painted "VOLUME & EXPRESSION"/"CHANNEL MIXER" heading. The redundant label is hidden. The painted heading remains.

## Acceptance

1. Verify **MIDI CC** tab appears alongside Main, Articulations, Mixer and Advanced.
2. Confirm all eight knobs and the shared channel selector fit in the 425-point dock, with the timeline still visible, plus enlarged floating view.
3. With a loaded, enabled ARIA instrument, turn Modulation CC1 and Expression CC11, test real sonic response on a known patch, and confirm CC7 does not directly modify Studio's audio track fader.
4. Turn Sustain CC64 On while holding notes, then change page, change channel and close editor. Verify release and no stuck notes.
5. Save/reopen; CC values and page/channel appear as last saved, but no unsolicited MIDI CC is injected. **Send values** explicitly re-applies them.
6. Verify unavailable AU/recording state disables controls; no fake audible response.
7. Run the extended adaptive GUI review and compare `20-atelier-full-midi-cc` screenshot to the compact dock. Check no overlapping headings on Main or Mixer.

**Not yet included:** hardware MIDI Learn, incoming-controller feedback, writing live CC sweeps into MIDI regions, and instrument-specific articulation dictionaries. Those need separate track input/recording integration rather than an unsafe simulation.
