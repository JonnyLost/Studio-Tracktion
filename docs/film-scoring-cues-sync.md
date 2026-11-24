# Studio 0.2 — Film Scoring M3: cue management and picture-sync decisions

This milestone is stacked on [PR #35](https://github.com/JonnyLost/Studio-Tracktion/pull/35), which itself requires [PR #34](https://github.com/JonnyLost/Studio-Tracktion/pull/34). It is a draft feature, **not** a released or merged Studio build. Cinematic Atelier and other UI branches are untouched.

## Composer workflow

Open a Studio project, choose **Picture** in the rail, and import a reference movie. A Picture project may now contain multiple named cues as well as spotting hit points. Cues are separate objects, with one unique identifier (for example `1M1`), title, notes, frame-anchored start, and optionally an end.

1. Seek to the desired source-picture frame. Choose **+ Cue**. Studio assigns the first free `1M#` identifier; you can change it in the cue fields.
2. Select the cue in the cue picker. Choose **Go Cue** to jump to its start. Seek beyond the cue's start and select **Mark End** to set its outgoing frame. An un-ended cue remains explicitly open.
3. Edit the cue ID, title or notes and select **Apply**. The ID must be unique. Delete a cue with **Delete Cue**; this does not delete MIDI or audio clips.
4. Select **Export CSV** to write a sorted source-picture cue sheet with start/end SMPTE, source frame indices, calculated duration in seconds and notes. There is no automatic audio rendering, stem export or upload. The native file chooser asks before overwriting.
5. Save the Studio project normally. `STUDIO_FILM_SCORING` ValueTree persists cues alongside prior picture settings and hit points, and Undo/Redo swaps are refreshed in the Film Scoring workspace.

`FilmScoringCueSheet.h` generates CSV independently of JUCE or playback and quotes fields, handles embedded quotes/newlines, and neutralizes formula-like user text for spreadsheet programs. CSV uses **picture source timecode**, not an assumed reel start timecode; a separate incoming source SMPTE offset remains future work.

## Picture sync decision engine

`FilmPictureSync.h` is a standalone, deterministic, allocation-free controller. It receives observations from the *existing Tracktion audio transport* and the native video player and returns UI-thread stop/seek/play commands. It detects large timeline discontinuities, including loop wraps, and forces re-alignment for new media, playback transitions and seeking. Small drift corrections are throttled; hidden players pause; unsupported out-of-range frame requests do not seek.

No video decoding, media seeking, blocking operations or file I/O runs in the audio callback. VideoComponent operations remain on the message thread. There is **no sample-accurate or frame-perfect presentation guarantee**; native decoder latency, codec keyframes and display scheduling must be validated on macOS. The controller does not generate additional audio clocks.

## Compatibility / persistence

- Existing project metadata schema version 1 is retained. A project without CUE nodes decodes with no cues.
- New CUE child records store ID, title, notes, start source frame and optional end source frame.
- Duplicate IDs, negative start positions and endpoints at or before the start are rejected.
- Cue positions are source-picture frames, so changing MIDI tempo does not rewrite cue endpoints.
- The selected cue and old edit pointer are discarded when changing projects. The workspace detects externally applied project Undo/Redo transactions before subsequent edits.
- Existing hit point structures, ARIA restoration, track routing and Cinematic Atelier are unchanged.

## Verification required

Source changes include three focused suites:

- `StudioFilmScoringCore`: SMPTE conversion, general timing, cue persistence and XML reload.
- `StudioFilmCueSheet`: uniqueness and endpoints, source-time sorting, CSV safety and output.
- `StudioFilmPictureSync`: transport start/stop, drift throttle, seek, loop wrap, project offset, hidden and out-of-range video.
- `StudioFilmScoringWorkspace`: app-level project save/reload, frame anchors, track isolation, and Undo/Redo reconciliation.

The existing **PR #34** Film Scoring Core test passed on the owner's Intel Mac. **None of the new PR #36 tests or the application build are verified yet**. In particular, no macOS CI or VideoComponent smoke test has been run on this branch. Treat this as implementation ready for acceptance testing, not a validated finished feature.

### Intel Mac build (in isolated test checkout)

Use your already-created `~/Developer/Studio-FilmScoring-M1` checkout and its existing Tracktion dependency checkout. Do **not** switch an unrelated Cinematic Atelier checkout:

```bash
cd "$HOME/Developer/Studio-FilmScoring-M1" &&
git fetch origin &&
git switch --track -c feature/studio-film-scoring-cues-sync-m3 origin/feature/studio-film-scoring-cues-sync-m3 &&
/Applications/CMake.app/Contents/bin/cmake -S . -B build -G Xcode \
  -DTRACKTION_ROOT="$PWD/build/deps/tracktion-engine" \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 &&
/Applications/CMake.app/Contents/bin/cmake --build build --config Debug \
  --target Studio StudioFilmScoringTests StudioFilmCueSheetTests StudioFilmPictureSyncTests --parallel 4 &&
/Applications/CMake.app/Contents/bin/ctest --test-dir build -C Debug \
  -R 'StudioFilm(ScoringCore|ScoringWorkspace|CueSheet|PictureSync)' --output-on-failure &&
open "$PWD/build/Studio_artefacts/Debug/Studio.app"
```

Acceptance on the Mac should exercise a short MOV/MP4, two cue ranges, timecode offsets, stopping, seeking, looping, floating/docking, CSV export, saving/reopening, and project Undo/Redo. Full delivery still needs frame-presentation profiling, cue-to-export MIDI/audio/stem routing and later integration with Studio's latest consolidated branch.
