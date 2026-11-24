# Studio 0.2 — Inspector-first Orchestral Instrument Performance (M5)

This is a product-owner-driven layout correction and performance-workflow milestone. Draft PR #43 is stacked on M4, which in turn is stacked on the current Cinematic Atelier development stack. **Do not merge without authorization.**

## Core design decision

- A hosted orchestral/third-party **Audio Unit** is represented by a permanently accessible **Cinematic Atelier performance Inspector** on the right. Its full editor is an *optional floating* window.
- **Studio Synth and Studio Sampler** continue to open their full native editors in the **lower workspace**.
- The lower workspace is otherwise reserved for Piano Roll, orchestral Stage, full Mixer, MIDI/Audio editing, and Film Scoring. No more automatic, oversized AU Atelier dock upon selecting a Garritan instrument.
- The right Inspector is now a **layout column**, not a layer drawn above arrangement and dock. Its width is reserved *before* arrangement/ruler/lower workspace bounds are computed. Closing it restores their full width.
- A user can resize the Inspector. The Orchestral panel reserves ~365 logical points when needed for 4-column controller knobs, with a scrollable vertical layout on shorter MacBooks.

## Inspector order

1. Track and Audio Unit identity, artwork selector, full aspect-preserved instrument artwork from the existing independent `~/Library/Application Support/Studio/AtelierArtwork/` library.
2. Up to eight verified existing articulation mappings (same per-instrument XML profile and Undo as the Articulation Palette). If no mappings exist, explicitly show that state and provide an edit action instead of fabricated patch-specific keyswitches.
3. Eight genuine MIDI 1.0 CC knobs: 1 Mod, 2 Breath, 7 MIDI Volume, 11 Expression, 64 Sustain, 71 Resonance, 74 Brightness, 91 MIDI Reverb; choose preview channel 1–16. Sends through existing Tracktion `AudioTrack::injectLiveMidiMessage` to the selected AU, never changes MIDI clips, never sends without a loaded usable AU, and never automatically replays settings on project open. Sustain sends CC-off on hidden Inspector, track switch, channel change, or teardown.
4. Three separate vertical strips: Track **Volume** (same Tracktion volume plug-in as Mixer), Track **Pan** (same Tracktion pan), and native Stage **Room** send trim. Room send disabled unless a real Stage seat and enabled send exist; it is not MIDI CC91.
5. Stage & Mics and Full Instrument actions. *Mic position faders are not fabricated*: native Studio Stage mics are shared and distinct from a patch-specific close/room microphone channel. Full independent mic faders should appear later only for instruments or Stage routing that actually expose them.

## State and independence

All on-screen controls act on the selected existing track and plug-in; Studio does not create another signal path, reinstantiate ARIA or add DSP work. Track artwork, CC values and audition channel are stored once in the track state for both Inspector and floating full Atelier, with v2–v5 full-editor view state still readable. Restoring a project updates knob displays but does not send MIDI until interaction or explicit Send CC. The full editor and Inspector stay coordinated when both are visible.

Existing channel-strip Inspector remains for Synth/Sampler/audio tracks. Prior Inspector Track Settings remain available in that mode, while FX and track commands stay accessible through track headers for hosted instruments.

## Verification

- With real ARIA: select its track, confirm right Inspector content, no lower Atelier, and correct instrument image when chosen.
- On 1280×800 and 1024×640: open/resize/close Inspector with Piano Roll, full Mixer, Stage and Film Scoring lower workspace. Nothing may be covered. The arrangement ruler and clips must shrink/expand together without misalignment.
- Open Full Instrument and Dock; it must return to Inspector without stealing the lower workspace. Test audio/MIDI continuity and unchanged saved ARIA state.
- Confirm real Studio Volume/Pan move the same mixer channel. CC7 moves only the instrument's MIDI channel volume, not the host audio fader.
- Verify verified articulations, project Undo, expressive CC1/CC11 responses on a supported ARIA patch; no fictitious labels for unmapped patches.
- Confirm room send slider is disabled until Stage is configured; when enabled, Stage inspector and live send reflect the same trim.
- Test sustain CC64 on, then hide Inspector/change track/change channel/close project. Verify no held pedal.
- Select Synth/Sampler: the conventional right Inspector appears and their lower full editors remain.
- Exercise expanded adaptive tests plus `StudioInterfaceReview`; capture `13-atelier-performance-inspector`.

No Film Scoring branch is modified or merged by this feature.
