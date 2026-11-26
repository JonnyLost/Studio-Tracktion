# M28 — Musician-first native sampler editor

The native sampler now opens in Play for new editor sessions. Three main
tabs replace the flat five-tab navigation:

- Play: instrument readiness and missing-recording count, actual mapped
  articulation choices and an explicit Select articulation action,
  instrument volume, keyboard and Open/Save. No sample mapping, raw
  waveform, region audition, or memory controls. No fabricated
  articulations, no claim that selecting a menu item changes playback:
  the action uses the existing native sendArticulation queue. The
  status indicates a message was sent, not that an external MIDI event
  has not subsequently changed the active articulation.
- Sound: the existing envelope and instrument volume controls, tuning
  in place of root-key mapping, explicit scope for imported per-region
  ADSR, and a recording selector when an imported envelope is edited.
  Existing waveform and recording audition remain available here.
- Build: Mapping, Groups, Waveform and Advanced subpages. All existing
  sample creation, mapping, group, looping, tuning and memory controls
  preserved; the last Build subpage is remembered for the editor
  session. Old numeric saved page IDs remain valid; Play is ID 5.
  Opening a native preset or accepting an imported instrument returns
  to Play. Adding individual recordings still returns to Mapping.

The on-screen keyboard continues to use existing native preview
dispatch. Play does not highlight only the selected zone, which would
misrepresent the playable range of a layered instrument. Layout uses
existing appearance scaling and scrollable body for constrained docks.

This is the first actual UI milestone, not a redesign of the entire
Atelier Inspector. The existing Inspector currently recognizes hosted
Audio Units, not native sampler instances. Portraits and catalog
artwork, native sampler Inspector controls, family/favorite library
integration and guided instrument creation remain follow-up work.
No invented CC controls or sound-changing behavior added.

Existing native UI review now checks default Play, hidden Build
controls, compact 460x320 keyboard and gain layout, Build subnavigation,
non-destructive view restoration, and captures Play screenshots at
normal/compact dimensions, while preserving prior mapping, loop,
keyboard and envelope regressions. These tests and screenshots have
not yet run at this checkpoint.

M27 Linux failed compilation of its streamed-bounce test because the
JUCE writer API requires a unique_ptr<OutputStream> reference rather
than the deduced unique_ptr<FileOutputStream>. M28 corrects the
fixture's type. CI configured for M28; no successful full build or
visual verification claimed. No earlier branch modified, no merge.
