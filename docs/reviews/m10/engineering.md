# M10 engineering and validation notes

## Preserved architecture

`SamplerEngine` remains the existing fixed-array 64-voice renderer. Key/velocity overlaps, round-robin IDs, CC expression, sustain, preview sources, stealing, release envelopes, stereo interpolation and fixed loop smoothing remain in place. The only new playback state is each voice's start position and exclusive sample end. Invalid trim bounds are rejected before allocating a voice. No new audio-thread allocation, locks, file access, parsing, enumeration or UI calls were added.

`StudioSampler` still publishes immutable banks through the existing single-reader borrowed-bank handoff; retirement and destruction remain on the message thread. Referenced decoded samples use shared ownership outside rendering. Organizational group IDs are separate from the existing round-robin ID, avoiding a change to old preset playback behavior. Version 1 presets acquire a Main organizational group and full-length sample boundaries. Version 2 writes group names, zone membership and boundaries alongside all previous fields.

The native editor still derives from `NativeInstrumentEditor`. `InstrumentWorkspace` and `InstrumentPerformance` remain responsible for Float/Dock and the fixed-height compact track-specific workspace. Existing articulation strip mappings are preserved; no third-party keyswitch assignments are invented.

## Import and limits

Import creates an unpublished proposal, enforcing the existing per-sample (64 MiB), bank (256 MiB), zone (128) and group (16) limits. The preview supports changing root, key and velocity ranges before committing one project undo transaction. Cancellation leaves the instrument unchanged. Filenames use delimited scientific note tokens, MIDI 60 = C4. Conflicting tokens are treated as ambiguous. Unknown roots use consecutive notes starting at MIDI 48. Automatic ranges divide at adjacent root midpoints, with repeated roots intentionally overlapping. There is no acoustic pitch detection.

File enumeration and decoding currently run on the message thread. This can briefly block the interface during a large import, while audio rendering continues against its previous bank. Future streaming/asynchronous decoding should replace the preparation phase, retaining the same bounded immutable-bank publication step. Source recordings are never modified; saves require the instrument extension and use a temporary target file.

## Automated coverage

- Standalone engine suite: existing note-off, pedal, expression, overlapping velocity ranges, round robin, stealing and loops; new start/end playback check.
- Standalone mapping suite: natural/sharp/flat/negative-octave filename tokens, ambiguity, absent notes, root midpoint ranges, repeated roots and trimmed-loop bounds.
- Application integration: generated WAV/AIFF decoding, proposal cancellation, accepted mapping, named groups, undo/redo, group persistence, version 1 migration, prevention of source overwrite, keyboard dispatch, preset/project reopen, missing-file recovery and hosted MIDI recording.
- Existing workspace suite: playback through opening, floating, docking, editor closure, track switching and restoration. Extended with reopened 1280 × 800 capture.

Full acceptance requires inspecting Intel macOS results and actual images. Passing standalone DSP tests alone does not establish a usable MacBook application.
