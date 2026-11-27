# O10 — MIDI Print

Based on O9. O9 standalone CI passed; full native validation is still required. O10 adds the existing ostinato branch family to native CI triggers.

## Workflow

1. Open Navigate > Ostinato Engine, load or author a pattern, and assign all active rows to instrument tracks.
2. Choose the repeat count, starting harmony, and intensity and density slider values. Stop the main transport and audition.
3. Move the project playhead to an empty range on all destination tracks. The range spans the complete finite master-cycle render.
4. Click Print MIDI. Each destination track/channel receives one normal editable MIDI clip with a name containing the pattern name and channel. Close the ostinato window and open the region in the Piano Roll. Save the project as usual. Undo removes the whole print as one transaction; Redo restores it.
5. To print another variation or chord, edit the starting harmony/variation sequence and choose another empty range.

Printing does not create tracks, load instruments, change project tempo or overwrite existing regions. It preserves absolute musical beat positions through the tempo map, independent row timing, ties, ratchets, probability, humanization, A–D variations, fills, and authored density evolution. Intensity and density are sampled once at print time. Starting harmony is used for the entire render; this is not a recording of live keyboard chords, CC movements or live audition. No background renderer is left running.

## Articulations and safety

Exact project-local articulation mappings are looked up on the current destination instrument. Supported keyswitches become editable MIDI notes held across the corresponding group; supported controller mappings become editable MIDI CC events. A 1/16-quarter-note lead is inserted before mapped attacks to avoid simultaneous-event ordering ambiguity. If a mapped attack is at the start, the region can begin slightly before the playhead without shifting musical notes. At project zero, move the playhead forward to allow this lead. If a technique change has insufficient space after previous notes, print refuses instead of truncating or silently changing notes. Unmapped steps emit no new articulation command. The target patch's interpretation of held keyswitches and CCs must be tested; no vendor mapping is inferred.

The operation rejects missing destinations, unassigned active rows, empty renders, frozen/disabled or still-loading instruments, missing/unsafe mappings, keyswitch pitch conflicts, out-of-range edit positions and any overlap with existing destination regions (including muted regions). Multiple MIDI channels on a destination are separate clips; all inserted clips must survive or the transaction rolls back. No change to the saved ostinato schema.

## Verification

Standalone tests cover multi-route/channel grouping, exact beat timing, density/intensity snapshot, controller values, keyswitch lead and duration, missing mappings, collision, insufficient lead, unassigned rows and empty output. Native instrument review exercises two real destinations, overlap refusal, note timing, and undo/redo. Native build and MacBook/ARIA playback acceptance must be confirmed separately; the new tests do not themselves establish audible playback fidelity.

## Remaining work

Full acceptance: audition and print a multi-instrument orchestral pattern on the Intel MacBook, compare Piano Roll notes, verify playback with actual instruments, save/reopen, test undo and redo, and check CPU/memory. Live conducting capture and transport-synchronized ostinato playback are separate future features, not part of MIDI Print.
