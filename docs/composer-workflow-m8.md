# Studio 0.2 Composer’s Core Workflow

## Build on an Intel MacBook

Run this from a checkout of `feature/studio-composer-workflow-m8` with Xcode and CMake installed. Replace `cmake` with `/Applications/CMake.app/Contents/bin/cmake` if needed. Studio targets Intel macOS 14 or later.

```bash
git init build/deps/tracktion-engine &&
git -C build/deps/tracktion-engine fetch --depth 1 https://github.com/Tracktion/tracktion_engine.git c6293127cdc0a8488aea2ae0907f4709ee89c228 &&
git -C build/deps/tracktion-engine checkout --detach FETCH_HEAD &&
git -C build/deps/tracktion-engine -c url.https://github.com/.insteadOf=git@github.com: submodule update --init --depth 1 &&
cmake -S . -B build -G Xcode -DTRACKTION_ROOT="$PWD/build/deps/tracktion-engine" -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 &&
cmake --build build --config Debug --target Studio --parallel 4 &&
open build/Studio_artefacts/Debug/Studio.app
```

Once the dependency checkout exists, rebuild and launch with:

```bash
cmake --build build --config Debug --target Studio --parallel 4 && open build/Studio_artefacts/Debug/Studio.app
```

## Transport and timeline

- Drag across the upper arrangement ruler to set and enable Cycle. Drag either edge to adjust it. Click the lower ruler to seek. Hold Option for a freehand drag or seek. Use **Music → Cycle playback** to toggle the saved range. Enabling an unset cycle uses the current bar.
- **Music → Metronome** enables the click during playback and recording using Tracktion's click output. **Metronome level** sets 20–100%, and **Accent bar starts** distinguishes downbeats. **Recording count-in** works independently of the regular click. Start a recording from Stop to hear the count-in. Starting from Play records from the current position.
- Turn off Cycle before recording. Stop or Record ends the take. Studio waits for recording finalization before another recording or a save. Check your input's merge settings when recording over existing MIDI. Studio asks before starting a take with an armed MIDI input in Replace mode.
- Stop playback to change tempo or meter. Right-click the arrangement ruler or use **Music** at the playhead. **Set tempo at beat** adds or updates a tempo event. **Set meter at bar start** adds or updates the meter at the containing bar's start. The **Tempo events** and **Meter events** lists edit or delete existing changes. The first event of each kind can be edited. Tempo and meter use project undo and are saved with the edit.
- **Add cue marker** names a position in the upper ruler. The **Cue markers** list jumps to, renames, and removes markers. Cue changes use project undo and are saved with the edit.
- **Edit Regions → Arrangement snap** selects Off, Beat, Half beat, Quarter beat or Beat triplet. Region gestures and cycle use this grid. The Piano Roll retains its own grid choice and uses the same bar-relative snap calculation.

## Compose and organize

- **Project → Import MIDI** accepts Standard MIDI File types 0 and 1 with PPQ timing. Start with **New Project** when you want a separate session. New tracks split the file by source track and MIDI channel. Selected-track placement creates separate clips on that track and preserves their channels. Choose to keep the tempo map and place at the playhead, or replace the map and place at project start. Import is one Undo transaction. Assign instruments to the imported tracks.
- **Project → Export MIDI** writes a type 1 file with the conductor track and selected MIDI regions, or the MIDI clips on selected tracks. Events retain their project positions, MIDI channel, velocity, CC, pitch bend, and key and channel pressure. Export reads the visible portion of each region's active take. It writes the stored note timing and tempo points.
- Drag on empty arrangement space to lasso clips. Hold Shift to keep the previous selection. **Edit Regions → Join selected MIDI regions** or Command-J joins linear, unmuted regions on the same track and channel. Note and controller boundaries account for source trim. Undo restores the source regions. Ghost notes in the Piano Roll are read-only.
- Select a track or one of its clips, then open **Edit Regions → Track** for Rename, Move up/down, Duplicate and Delete. Duplicate preserves the native instrument, effects, clips and Stage seat with new IDs. The copy starts muted with solo cleared. One shared Room Return serves both tracks. Delete can be undone. Managed room returns and hosted or rack state have edit restrictions.

## Deliver the cue

1. Stop the transport and choose **Project → Export WAV**. Select **Full mix** or **Individual track stems**, stereo or mono, 44.1 or 48 kHz, and 16- or 24-bit PCM.
2. Select Whole project, Cycle range or Custom time in seconds. Set the tail length from 0 to 30 seconds. Source clips stop at the range end. The graph runs for the full tail window so all files have matching durations.
3. For stems, select the tracks and choose whether to include the shared **Room Return**. Direct stems include the track processing, fader and pan. The optional stereo or mono Room Return contains the wet signal from the selected audible senders. Stems are delivered before master processing. The full mix includes the master and shared room.
4. Choose a new filename for the mix or an existing folder for the stem set. Studio renders from an isolated copy of the edit and keeps temporary WAVs until the export succeeds. Cancel stops the queue and discards temporary results. Existing names are protected. Use a local volume that supports hard links, such as APFS, for safe final publication.

## Reproduce an 80-second cue

1. Create a fresh project. Make five Synth tracks, two Sampler tracks and one audio track. Choose different included Synth presets. For the sampler sources and audio, record your own short sound or generate a two-second sine sample in a local audio tool. Load the sample on both Sampler tracks and import the audio source near the start and at one later transition.
2. At 120 BPM in 4/4, create and duplicate MIDI phrases through bar 16. At bar 17, set 90 BPM and 3/4. Continue through the end of bar 40. The boundary of bar 41 is 80 seconds. Add cue markers at bars 1, 9, 17 and 33. Set a cycle around bars 17–20 and audition the transition.
3. Disable Cycle, record a short MIDI part with a count-in, then stop. Check its recorded notes and add CC1 and CC11 in Expression. Put a CC64 release after the last held note and center any pitch bend at the end of the phrase. Split and join two same-channel regions, then undo and redo the join.
4. Place the seven instruments on Stage and enable Stage Audio on at least three. Apply a room and put two sources at different depths. Save. Export the whole cue as a stereo WAV with a four-second tail. Export two dry stems and one shared return over the same range.
5. Reopen the project. Check the cue and cycle ranges, tempo and meter, MIDI expressions, sample paths, track order, Stage seats and room controls. Verify the WAV length is 84 seconds when using a custom range of 0–80 seconds and a four-second tail.

## Feature and limitation matrix

| Workflow | Supported path | Limits and remaining validation |
| --- | --- | --- |
| Cycle and click | Native Tracktion playback, adjustable cycle, click level, record count-in | Cycle recording is disabled. Bar count-ins use the opening meter's beat count in the pinned engine. Cycle endpoints are stored in seconds. Live Core Audio and physical MIDI transition checks remain. |
| Musical timeline | Editable tempo and meter events, named cues, ruler seek and shared snap calculation | Tempo point moves use delete and add. Meter additions use the containing bar start. Cycle and click toggles use project settings outside undo history. |
| Standard MIDI | Type 0/1 PPQ import, channel-separated clips, optional file conductor, deterministic type 1 export | SMPTE files, SysEx and MPE are outside this path. Export writes discrete tempo events and the active take's raw timing. File import pairs same-pitch overlaps FIFO. Export rejects nested same-key overlaps whose note lengths cannot survive that pairing. |
| Arrangement | Lasso, snap, linear MIDI join, basic track editing with undo | Audio join, looped-region join/export, group trim and lasso auto-scroll are deferred. Hosted and shared return tracks cannot be duplicated or deleted through the new menu. |
| WAV | Offline stereo/mono PCM, fixed tail, dry track stems and one selected-source wet stem | Hosted plug-in export is blocked pending AU validation. Individual stems use top-level tracks on the default output. Family stem grouping and arbitrary routed-bus stems are deferred. Large sessions need enough memory for the render copy. |
| Project safety | Native edit persistence, ARIA Save/Save As protections, staging and cancellation | Physical Intel MacBook playback, installed ARIA restoration and real-world performance still need hardware verification. |

## Automated checks

Build all configured targets and run `ctest --test-dir build -C Debug --output-on-failure` on the Intel Xcode build. On Linux run `xvfb-run -a ctest --test-dir build --output-on-failure` after the Release build. The Composer suites cover the transport and saved timeline, both MIDI file types, overlap and expression timing, join and track undo, WAV format, length, cancellation, dry/wet routing, missing media and unsupported AU, and the ARIA export gate. The Intel and Linux workflows publish the arrangement/export screenshots, test reports and synthetic WAVs. The existing Synth benchmark and acoustic comparison remain part of the verification.
