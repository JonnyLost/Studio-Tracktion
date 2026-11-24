# Studio 0.2 — Film Scoring M5: Picture alignment and orchestral-family WAVs

**Scope:** Draft M5 branch stacked on M4. Do not merge M1–M5 or modify the separate Cinematic Atelier checkout. M4's three-column fixed Picture dock and independently floating monitor stay intact.

## Composer workflow

1. Open **Picture**, import a MOV/MP4, set the source frame rate and picture offset. Audio comes from Studio; the movie's audio remains muted.
2. Make a cue with **+ Cue** and **Mark End** while stopped. Its boundaries stay anchored to **source picture frames**, unaffected by tempo changes.
3. Under **ORCHESTRAL STEMS**, choose a Studio instrument/audio track, then choose **Strings**, **Woodwinds**, **Brass**, **Percussion**, **Keyboards & Harps**, **Choir & Voices**, **Synths & Hybrid**, or **Other**. The assignment is saved in the current Studio project and supports undo/redo. **Unassigned** removes that track from the family exports.
4. Repeat for the tracks needed in the cue, stop Studio's transport, select a closed cue, and click **Render Family Stems**. Studio creates a *new unique folder* and renders one WAV for each nonempty assigned family. The normal **Render Cue WAV** and **Render Track WAVs** commands are unchanged.
5. Review each audio file and **Delivery-Report.csv**. Check the reverb, routing, and sample alignment before external delivery.

There is no automatic name-based grouping. A named-but-unassigned track is deliberately excluded, and deleted track IDs are ignored. You can change an assignment without renaming the audio track.

## Audio output and routing

All family stems share the selected cue's picture-frame-derived timeline window: **48 kHz, stereo, 24-bit PCM WAV**, offline, no normalisation, no trimming, no appended tail. Rendering is performed serially using the pinned Tracktion Engine's `RenderSpecification` and `RenderQueue`. The master plugin chain is excluded from family stems so family outputs remain separately mixable; instrument and track effects remain on. Tracktion's `includeSourceTracks` attempts to retain sidechain, shared rack and aux dependencies without adding their direct audio to the resulting stem.

**Not yet guaranteed:** Perfect stem sum against the stereo master, inclusion of master-chain reverb, every aux/return topology, broadcast-WAV timestamps, sample-accurate video sync, or correct output from offline-incompatible Audio Units. Compare the rendered stems against the full mix on the Mac and report unexpected differences. Reverbs at a cue's exact end can be cut off.

## Picture synchronization

Picture sync continues to use native macOS video decoding driven from the Studio transport on the message thread. M5 reports *signed picture drift in frames* and makes the seek tolerance relative to source FPS. Press **Resync** to request immediate realignment after a transport/video disturbance. SMPTE calculations, negative picture leader, playback stop, seek jumps, and loop wraps are covered by portable tests. This is a practical preview alignment model, **not** an audio-clock-locked video engine. The video may still display decoding latency/jitter under load.

## Build & verify (Intel MacBook)

Use only the established film-scoring checkout, not the Cinematic Atelier checkout. The code is in the new M5 branch, not in M4; both remain unmerged.

```bash
cd "$HOME/Developer/Studio-FilmScoring-M1" &&
git fetch origin &&
git switch --track origin/feature/studio-film-scoring-sync-stems-m5 &&
/Applications/CMake.app/Contents/bin/cmake -S . -B build -G Xcode \
  -DTRACKTION_ROOT="$PWD/build/deps/tracktion-engine" \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 &&
/Applications/CMake.app/Contents/bin/cmake --build build --config Debug \
  --target Studio StudioFilmScoringTests StudioFilmCueSheetTests StudioFilmPictureSyncTests StudioFilmCueDeliveryTests StudioFilmStemDeliveryTests --parallel 4 &&
/Applications/CMake.app/Contents/bin/ctest --test-dir build -C Debug \
  -R 'StudioFilm(ScoringCore|CueSheet|PictureSync|CueDelivery|StemDelivery)' --output-on-failure &&
open "$PWD/build/Studio_artefacts/Debug/Studio.app"
```

For subsequent builds use `git switch feature/studio-film-scoring-sync-stems-m5 && git pull --ff-only` instead of `git switch --track`.

**Mac acceptance:** (1) Studio compiles and opens, (2) picture stays in fixed dock and Float/Dock preserves video, (3) source SMPTE remains accurate across seek/loop/tempo changes, (4) a two-family cue renders correctly named same-length WAVs with correct instruments, (5) unassigned tracks are omitted, (6) assignment survives saving/reopening and undo/redo, (7) rendering/cancelling does not leave project mute/solo state changed. No claims of Mac acceptance are made until checked.
