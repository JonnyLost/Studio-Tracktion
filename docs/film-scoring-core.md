# Studio 0.2 — Film Scoring Core audit and integration contract

## Baseline audit (2026-11-23)

- Verified repository: `JonnyLost/Studio-Tracktion` (JUCE/Tracktion Engine, C++20).
- Chosen base: `integration/studio-0.2-m7-baseline` at `b81101f`. This is the named integration baseline; later M8–M14 and Cinematic Atelier work live on separate feature branches and are intentionally untouched.
- `Studio.cpp` uses Tracktion's shared `edit->getTransport()` and `edit->tempoSequence`. The arrangement and piano roll already render musical ticks from the tempo sequence.
- No video import, picture decoder/player, timecode clock, spotting markers, picture editor, or scoring-specific metadata model exists on the verified integration baseline. The `SpatialMap::Marker` type is for stage microphones and instruments, not picture scoring.
- The selected baseline does not include a macOS video bridge. A frame-accurate display claim cannot be made until AVFoundation decoding, video output timing, and real-device tests are in place.

## What this change implements

`FilmScoringCore.h` provides pure, non-allocating time conversion functions for 24000/1001, 24, 25, 30000/1001 non-drop/drop, and 30 frames per second; validated SMPTE labels and 24-hour wrap; timeline/picture offsets; frame-anchored named hit points and cue IDs; piecewise-constant tempo maps; bar/beat positions (including meter changes); and non-destructive tempo suggestions between two picture hits. The tempo calculator never changes existing musical clips.

`FilmScoringState.h` stores a versioned picture-scoring tree under the Tracktion Edit root `ValueTree`; the caller must explicitly call `store(edit.state, project, &edit.getUndoManager())` when scoring metadata is edited, and `decode(edit.state)` when loading or displaying it. No serialized clip timing is mutated. Metadata is presently an opt-in integration API: Studio's workspace does **not** yet expose editing actions that call it. File paths are references, not embedded video.

`StudioFilmScoringCore` tests cover fractional/drop-frame conversions, invalid timecode, negative frames, seek/offset calculations, marker ID uniqueness, beat/tempo calculations, serialization and undo/redo. The independent time math can compile without JUCE; persistence tests require the Tracktion/JUCE build.

## Video transport integration boundary

When the picture monitor is implemented, sample its intended timeline time from the *single authoritative audio transport* (`edit->getTransport().getPosition()`), use `project.pictureFrameAt(timelineSeconds)` to identify the desired source frame, and request that frame from a seekable AVFoundation reader on a non-audio thread. On stop, seek, and loop transitions, invalidate pending frame requests using a generation token. Never seek or decode video from the audio callback. Render frames on the UI thread, and never equate the desired frame index with a guarantee of exact presentation time. The `pictureStartFrame` property is the timeline position of picture frame zero, including reel leader offset.

To build a tempo map from Tracktion, enumerate `edit->tempoSequence` tempo and time-signature events and convert their absolute positions to seconds; do not assume a fixed global BPM. The supplied calculator supports piecewise-constant tempo. Smooth ramps are not supported until an exact integral is implemented. Any time-signature change starts a new bar by defined convention; align Tracktion's bar numbering before exposing an editor. Frame-based spots must not be rewritten when tempo changes.

## Not yet implemented

- Native video import, decode, floating/docked monitor, real-time playback lock, hardware video frame-presentation verification.
- UI controls for adding, changing, navigating or showing spotting markers; actual calls from Studio project lifecycle into the new state adapter.
- Exact Tracktion tempo-sequence bridge and tempo-ramp semantics, project media relinking, and end-to-end Mac CI acceptance.

These require follow-up integration rather than advertising a completed film-scoring workspace. Preserve Cinematic Atelier and later interface branches until deliberate integration review.
