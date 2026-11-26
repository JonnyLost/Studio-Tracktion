# M31 — Contextual help and usability refinement

This milestone builds on M28–M30. No new sample format, synthesis engine,
commercial dependency or project migration.

## Help at the point of use
The sampler has a Help button in its main navigation. Its local readable
help dialog describes the currently selected Play, Sound, Mapping, Groups,
Waveform or Advanced page. The visual library and guided builder have their
own Help buttons. Help is read-only, scrollable, available offline, and
does not edit the instrument. Close returns to the existing workflow.
Explanations include envelope stages, recording vs zone, velocity layers,
keyswitches, round robin, looping, memory/voice limits, prepare vs load,
source limitations, and saving/repairing native presets.

## Everyday workflow
- Play and Sound no longer show technical mapping overlap/gap diagnostics
  during ordinary use. Build retains them.
- Play provides Find missing sample if a sample is unavailable; it selects
  the first missing recording and uses the existing replacement file
  chooser. Saved boundaries and mapping validation still apply.
- Mapping root/range/keyswitch and guided builder root fields show note
  names plus MIDI numbers, accept either note names or numeric values,
  including enharmonic flats, and preserve the previous value for invalid
  text. C4 is MIDI 60.
- Envelope tooltips explain the musical effect and editing scope.
- Library empty/filter states explain the next action. Changing favorites
  or family preserves matching selected entries. Double-click and Return
  activate the actual row, not an unrelated selected row. Format/management
  controls remain behind Manage library.
- Repair sample paths is available directly for cataloged native presets,
  even if they have never gone through conversion. Repair still writes
  a separate copy, never overwrites the original, and rejects ambiguity.
- Inspector native articulation button routes to the native sampler rather
  than the hosted-AU articulation palette; native expression tooltips
  distinguish MIDI volume and track mixer volume. Hosted behavior retained.
- Library row height follows appearance metrics. Status receives extra
  space and full failure text in tooltip. Guided builder and help use
  existing scaled UI typography and layout.

## Verification
Added integration checks for note text round trips at all 128 MIDI notes,
sharp/flat input, invalid/mismatched text, each help topic, direct native
preset sample repair and original-file retention. Existing native UI
review opens/captures/closes sampler help without changing the instrument.
Linux and Intel macOS CI configured. M31 application builds, screenshot
and new regression tests are not yet verified. M30 CI was still in progress
at checkpoint. M29 compiled on both platforms and had one known Unified
Stage collapsed-group visibility failure per platform. That issue remains
for M32 rather than being hidden or bypassed here.

## Remaining usability acceptance
Check normal and largest text on Intel MacBook at 1024x640 and 1280x800,
docked/floating windows, keyboard focus and navigation, help dismissal,
missing recording recovery, library source preparation, and new instrument
creation. Review actual screenshots; do not infer visual success from code.
Test with real libraries, preserve project and original recordings.
No branches merged.
