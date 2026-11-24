# Studio 0.2 — Picture and Film Scoring workspace (draft)

This is the first user-facing picture-scoring workflow built on [Film Scoring Core](film-scoring-core.md). It is a **stacked feature**: PR #35 depends on PR #34 and is not yet on the integration branch. It requires macOS for native video playback.

## Getting started

1. Create or open a Studio project and choose **Picture** in the left rail. Use **Split** to view the arrangement alongside picture, or **Expanded** for more monitor and spotting space.
2. Choose **Import Picture** and select a supported local MOV, MP4, or M4V. The reference video plays silently to avoid duplicating any guide audio routed through Studio. Studio saves a path reference rather than embedding a large movie in the project file.
3. Select the picture frame-rate mode: 23.976, 24, 25, 29.97 non-drop, 29.97 drop-frame, or 30. These are project scoring settings; Studio does not automatically detect embedded source timecode.
4. **Set Offset** is the signed *timeline frame* where picture source frame zero begins. For example, +240 frames at 24 fps places the first picture frame 10 seconds after the project starts. Hit points stay attached to picture frames when the musical tempo changes.
5. Place the transport at the desired picture event and choose **Add Hit**. Edit its cue ID, name, and notes with **Apply Hit**. Choose a hit from the list or from the strip, use Previous/Next, and **Go To** to seek to it. Delete one with **Remove**.
6. Enter a count of quarter-note beats in the field at lower left (e.g. 16 for four bars of 4/4). Studio suggests a constant tempo from the selected hit to the next later hit and displays the beat interval in the **existing** Tracktion tempo map. Suggestions **do not change** the arrangement.
7. **Float** moves the same muted video monitor to a resizable window. Closing the floating window returns the video to its docked host; it does not unload the picture or replace the project.

The spotting strip draws frame-anchored hits and the picture playhead. Clicking a marker selects it and seeks there; clicking empty space seeks to the corresponding source picture time. The line represents *picture time*, not a beat grid, so a tempo change never slides a hit away from its source frame.

## Project isolation and persistence

Frame-rate mode, picture start offset, imported-media path, hit-frame positions, cue IDs, names, and notes live in the project's versioned `STUDIO_FILM_SCORING` ValueTree. They should survive Save/Save As and project reload. A missing video stays as a missing reference; reopen it through **Import Picture** without losing hit points. The Film Scoring workspace releases its previous edit pointer on project close or switch. Repeated marker edits participate in the Edit's undo manager.

## Transport, latency and media limitations

The **Tracktion audio transport** is authoritative. Video preview follows transport position on Studio's message-thread timer, and seeks are throttled to avoid flooding the AVPlayer backend. Source audio from the VideoComponent is muted. Video seek/display accuracy depends on native decoding, keyframe structure and hardware timing. **This is not yet guaranteed sample-accurate, frame-perfect picture presentation**. Before using a scored hit for a locked delivery, verify it with a rendered cue against the original media.

There is no independent video transport or separate audio clock. This milestone does not add burn-in, dialogue/stem extraction, video export, streamers/punches, automatic tempo rewriting, tempo-ramp fitting, or a cue-sheet exporter. AVFoundation can fail on incompatible codecs despite a matching filename suffix. Paths are absolute and may need relinking when moving a project to another Mac.

## Intel Mac development build

From the already built **Studio-FilmScoring-M1** checkout (do not overwrite Cinematic Atelier or another Studio build):

```bash
cd "$HOME/Developer/Studio-FilmScoring-M1" &&
git fetch origin &&
git switch --track -c feature/studio-film-scoring-workspace-m2 origin/feature/studio-film-scoring-workspace-m2 &&
/Applications/CMake.app/Contents/bin/cmake -S . -B build -G Xcode \
  -DTRACKTION_ROOT="$PWD/build/deps/tracktion-engine" \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 &&
/Applications/CMake.app/Contents/bin/cmake --build build --config Debug --target Studio StudioFilmScoringTests --parallel 4 &&
/Applications/CMake.app/Contents/bin/ctest --test-dir build -C Debug \
  -R 'StudioFilmScoring(Core|Workspace)' --output-on-failure &&
open "$PWD/build/Studio_artefacts/Debug/Studio.app"
```

`StudioFilmScoringCore` exercises frame conversion, tempo calculations, persistence and undo. `StudioFilmScoringWorkspace` creates a disposable Tracktion edit, opens the Picture workspace, stores frame-anchored hits, saves, reloads and verifies that track state remains undisturbed.

**Acceptance still required on the physical Mac:** open MOV and MP4 references; verify muted audio, sustained playback against track tempo; stop/seek/restart/loop, project reopen, media relink, floating/docked window behavior, undo/redo in the actual GUI, and scrolling at MacBook resolutions. The full macOS compilation and native movie checks have not been run in the environment that authored PR #35.
