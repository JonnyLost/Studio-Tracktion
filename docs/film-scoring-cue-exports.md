# Studio 0.2 — Film Scoring M4: Cue WAV delivery (draft)

**Status:** Isolated candidate implementation in PR #37, stacked on M3 (#36), M2 (#35), and M1 (#34). Not merged. The previously passed Mac hardware build was M1 **only**. The full macOS Studio application and native WAV render in this PR have **not** been compiled or tested on the owner's Mac.

## Composer workflow

Open a Studio project and select **Picture** from the rail. Import your reference picture, set the project frame rate and picture offset, then create a cue with **+ Cue**. Position the transport at the desired end frame and choose **Mark End**. The cue's start and end are stored in **source picture frames**, so changing tempo later does not change the desired picture boundaries.

Stop playback and recording, select the cue in the cue picker, and choose one of the new delivery controls:

- **Render Cue WAV:** Produce one 24-bit / 48 kHz stereo WAV of the selected cue, with the Tracktion mix and master plugins.
- **Render Track WAVs:** Produce separate 24-bit / 48 kHz stereo WAVs for the current project's audio tracks. The Tracktion renderer tries to preserve required sidechain, rack, and aux sources as muted render-graph dependencies. These outputs are *individual-track renders*, **not yet orchestra-family or guaranteed sum-accurate stems**.
- **Cancel Render:** Cancels the active render queue. It may leave completed earlier jobs or partial files from a cancelled job in the delivery folder. The report records job statuses.

Studio opens a native folder chooser, then creates a **new unique delivery folder** each time. It does not intentionally replace previously rendered WAV files. The native renderer uses `RenderSpecification`, `validateRenderSpecification`, `createRenderJob`, and sequential background `RenderQueue` from the **pinned Tracktion Engine**. The GUI never runs the renderer in the audio callback. The batch is planned and validated before the first file is rendered.

Each completed/cancelled batch attempts to write **Delivery-Report.csv**, including cue ID/title, picture source SMPTE start/end, timeline start/end seconds, file names, success/cancellation/error state, and errors. This report is a delivery log, **not** evidence of sample-accurate picture locking. It can be compared to the existing SMPTE cue-sheet CSV.

### Default export settings

| Setting | Value |
|---|---|
| Format | WAV PCM |
| Depth | 24-bit |
| Sample rate | 48,000 Hz |
| Channels | Stereo |
| Start/end | Selected cue's exact source-frame-derived Tracktion time range |
| Normalization | Off |
| Fade, trimming, looping | None |
| Render tail | Not appended; all output ranges intended to align |
| Plugins | Track effects and instruments enabled |
| Master plugin chain | Stereo mix only; off for individual-track renders |

**Important:** This first implementation is designed for bounded cue delivery. It does not yet embed broadcast-WAV source time references, burn video timecode, create dialogue/M&E stems, group by orchestral family, or automatically produce sum-accurate reverberant stems. Some commercial/AU plug-ins may require real-time playback, so offline renders of these plug-ins must be checked by ear. Reverb/delay decay at the cue endpoint can be truncated because extending tails would make delivery lengths unequal; a separate configurable tail policy is future work.

### Project and resource safety

- A cue must have a valid, positive-length end. The requested range must exist at/after Studio time zero and is limited to one day.
- Rendering is disabled during playback/recording and a new render can't start before the previous batch finishes/cancels.
- New output folders prevent accidental replacement of an existing delivery batch.
- M4 doesn't edit MIDI, clips, tracks, effects or the cue's stored frame positions. Tracktion's render queue may temporarily mute dependency tracks where necessary and is responsible for restoring those states.
- Switching projects cancels and releases the in-flight queue before destroying the Edit.
- Serial rendering prevents an unbounded number of instruments and stems from rendering concurrently; this implementation limits individual-track batches to 128 tracks.

## MacBook build and test

Use only the already-created, **separate film-scoring checkout**. Do not change the Cinematic Atelier checkout or merge any branch.

```bash
cd "$HOME/Developer/Studio-FilmScoring-M1" &&
git fetch origin &&
git switch --track -c feature/studio-film-scoring-cue-exports-m4 origin/feature/studio-film-scoring-cue-exports-m4 &&
/Applications/CMake.app/Contents/bin/cmake -S . -B build -G Xcode \
  -DTRACKTION_ROOT="$PWD/build/deps/tracktion-engine" \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 &&
/Applications/CMake.app/Contents/bin/cmake --build build --config Debug \
  --target Studio StudioFilmScoringTests StudioFilmCueSheetTests StudioFilmPictureSyncTests StudioFilmCueDeliveryTests --parallel 4 &&
/Applications/CMake.app/Contents/bin/ctest --test-dir build -C Debug \
  -R 'StudioFilm(ScoringCore|ScoringWorkspace|CueSheet|PictureSync|CueDelivery)' --output-on-failure &&
open "$PWD/build/Studio_artefacts/Debug/Studio.app"
```

If the film-scoring M4 branch already exists locally, use `git switch feature/studio-film-scoring-cue-exports-m4` after fetch instead of the `git switch --track -c` command. Keep other working Studio windows closed before the build.

### Physical Mac acceptance checklist

1. Build the full Studio app, run the core, cue, picture-sync, delivery, and workspace test suites.
2. Confirm an Intel MacBook can open the Picture workspace with a short MOV/MP4 and preview video through the existing Studio audio transport.
3. Create a cue with an exact source start/end timecode, export a stereo WAV, verify its format, real audio content and duration.
4. Use a disposable session with several native synth/sampler/ARIA audio tracks; render individual-track WAVs and verify their start times, names, routing and expected audio.
5. Compare exports against Studio's live mix. Specifically check master processing, room/reverb sends, buses, mute/solo, sustain and AU real-time requirements.
6. Cancel a longer batch, verify Studio remains responsive, and that no stale mute state survives.
7. Switch/open a project after a completed batch; confirm its instrument states and cue metadata are intact.
8. Check the report CSV for correct frame timing, successful/cancelled job statuses, and no accidental overwrites.
9. Verify output names and folders with duplicate cue titles, invalid punctuation, and reopened projects.

**Do not merge PR #37 or claim professional delivery readiness until the Mac build and actual audio routing tests pass.** This branch is intentionally isolated from the main 0.2 integration and Cinematic Atelier work.
