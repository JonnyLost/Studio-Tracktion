# Studio 0.2 — Arrangement and MIDI Piano Roll

Standalone JUCE/Tracktion Engine DAW for Intel macOS, based on the Mac-verified Studio 0.2 arrangement and automatic ARIA restoration milestone.

## Arrangement window

- The top bar shows project controls, **Save**, **Save As**, transport, time position, and tempo. Space controls playback from the arrangement; Command-S saves and Command-Shift-S opens Save As when Studio has keyboard focus. Save As creates a new `.tracktionedit` file, switches the current project to that name, and refuses to overwrite an existing file.
- Drag the left divider to resize the sidebar. Select a track to inspect its recording status and adjust its volume and pan in the Mixer.
- Click the ruler to seek. The arrangement ruler displays **measure numbers and beats**, aligned with vertical guides through the track lanes; both follow Tracktion's tempo and time signatures. The **− / + buttons beside Tracks at the top of the arrangement** zoom horizontally around the visible playhead (or viewport center when the playhead is out of view). The sidebar zoom menu still provides timed ranges and **Fit project**; it shows **Custom zoom** after using the buttons. Track lanes show MIDI notes and audio waveforms. Use the track's **I** and **A** controls for input and recording, and the **+** in Inserts to load an instrument or effect.
- **Mixer**, **MIDI Monitor**, and **Piano Roll** share the bottom workspace. Drag its upper divider to change its height, or click **Hide** to reclaim timeline space. The monitor can still open in a separate window and dock again.

## MIDI Piano Roll

1. Click a recorded MIDI clip in the arrangement. The editor opens automatically. Alternatively, select an instrument track, position the playhead, and click **+ MIDI Clip** to create a four-bar MIDI clip and open the editor.
2. Choose **Draw** to place notes. Drag during drawing to extend their duration. Choose **Select** to click a note and drag it horizontally or vertically. Drag either edge to resize; a horizontal resize cursor indicates each trim handle. Delete or Backspace removes the selected note.
3. Drag a note's stem in the **Velocity** lane to edit its velocity. The note remains in place during the drag. Note and velocity-bar colors follow the same continuous color wheel from soft blue to hard red. The **Snap** menu places each triplet directly after its straight value (**1/4, 1/4 T, 1/8, 1/8 T, 1/16, 1/16 T, 1/32, 1/32 T**). **Quantize** snaps the selected note, or all notes if none is selected. **Undo** and **Redo** operate on Tracktion's project undo history. Command-Z and Command-Shift-Z work when the piano roll has keyboard focus.
4. The piano roll's measure/beat ruler uses the same project tempo sequence as the arrangement ruler. **Measure boundaries are strongest, beats medium, and selected snap subdivisions subtle**. Subdivision lines are anchored to **musical bar boundaries**, including when the MIDI clip has a source offset; drawing and quantization snap to those same visible positions. **Click a subdivision tick on the piano-roll ruler to seek the shared transport** without editing notes. Subdivisions and densely packed beat ticks disappear at wide zoom to avoid clutter, but **the selected snap value remains unchanged**. A gold **vertical playhead** moves through the notes and velocity lane whenever transport playback lies inside the selected clip. Both views read the same transport position; drawing refreshes at the UI timer rate, not sample by sample. Use the scrollbars to navigate beats and pitches. Shift-scroll moves horizontally. Command-scroll or **H+ / H-** adjusts horizontal zoom. **V+ / V-** adjusts piano-key height (vertical zoom) without jumping to a different pitch range. Click **Save**, quit, and reopen to verify edits persist.
5. Click a note or a piano key at the editor's left edge to briefly audition it through an **existing MIDI input assigned only to that clip's track**. Drag vertically across the on-screen keyboard to hear successive pitches; dragging a selected note vertically also previews its pitch change. Studio does not change input routing, channel data, arming, or project state to preview a note. If the track does not have an enabled, exclusively assigned MIDI input, previews are silent until one is assigned with **I**. Preview uses the clip's MIDI channel (MIDI channel 1 if unspecified), not a forced channel 16; confirm the ARIA slot listens on that channel.

When reopening a project containing ARIA, piano editing remains available. An **ARIA CHECK** reminder and **Retry ARIA / Confirm ARIA** controls appear near the top of the left sidebar. Studio blocks **both Save and Save As** until you have verified the restored ARIA slot sounds correct and clicked **Confirm ARIA**; a warning now explains this when you try to save. This protects the original AU state and backup. Editing a clip is not the same as confirming safe ARIA restoration.

**Planned editor milestones:** shift-click/lasso multi-note selection, multi-region selection with simultaneous MIDI clip display, and region split/join/duplicate/copy/paste across tracks. These operations are **not implemented** in the present piano-roll milestone.

After stopping an Oxygen 25 recording, saving waits until the Tracktion transport completes its recording-stop transition, with a bounded timeout rather than writing a partially finalized take. If it cannot finish safely, Studio leaves the existing file untouched and reports the failure.

All note changes use Tracktion's undo-aware MIDI sequence APIs on the message thread. The editor updates the active take of the underlying MIDI clip. It preserves its MIDI channel, plugin state, and audio routing. Looping clips show the editable source sequence, so edits affect every repetition of that take. MIDI notes beyond the visible window can be reached by scrolling.

## Build and launch on Intel macOS

Quit any running Studio instance. On the MacBook, switch to this feature branch after it is published and configure once:

```bash
cd "$HOME/Developer/Studio-Tracktion" &&
git fetch origin &&
git switch --track origin/feature/midi-piano-roll-m2 &&
/Applications/CMake.app/Contents/bin/cmake -S . -B build -G Xcode \
  -DTRACKTION_ROOT="$HOME/Developer/Tracktion-Studio" \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 &&
/Applications/CMake.app/Contents/bin/cmake --build build --config Debug --target Studio --parallel 4 &&
open "$PWD/build/Studio_artefacts/Debug/Studio.app"
```

If you already built the branch, update it without reconfiguring:

```bash
cd "$HOME/Developer/Studio-Tracktion" &&
git switch feature/midi-piano-roll-m2 &&
git pull --ff-only &&
/Applications/CMake.app/Contents/bin/cmake --build build --config Debug --target Studio --parallel 4 &&
open "$PWD/build/Studio_artefacts/Debug/Studio.app"
```

## Mac verification

1. Record a short MIDI take with the Oxygen 25. Stop, verify a recorded clip remains in the arrangement, click its clip, and draw, move, resize from **both edges**, delete, and change note velocity. Quantize a triplet note using **1/16 T**. Undo and redo. Save, quit, and reopen the project. Verify MIDI editing stays enabled, exact notes and playback survive, and neither channel nor track routing changes. If the clip vanishes immediately at Stop, this is a recording/arming failure (before project saving), so note that distinction.
2. Create a new MIDI clip with **+ MIDI Clip** and draw four notes. Check the sound through ARIA and that edited note channels remain the clip's original channel.
3. Import an audio clip and test playback, mixer volume/pan, monitor float/dock, and recording take retention.
4. Select a note to audition it, drag a note vertically to transpose it, click the piano keyboard, then drag up and down on the keys to hear a chromatic preview. If sound is absent, assign an enabled MIDI input with **I**, confirm the clip MIDI channel matches the ARIA slot, and retest. Check **H+/H-** and **V+/V-** on a small MacBook screen.
5. Click **Save As**, choose a new filename, and verify it creates a separate `.tracktionedit` without changing the original. Add a note, click **Save**, then quit and reopen that new copy to verify subsequent saves target it. Existing filenames are deliberately rejected by Save As.
6. Open a **copy** of the bundled Intimate D recovery project. Look for the visible **ARIA CHECK** sidebar reminder. Allow automatic restoration to run, verify the instrument appears and plays, confirm piano editing works **before** confirmation, then click **Confirm ARIA** to allow saving. See [RESTORATION_TEST.md](RESTORATION_TEST.md).
7. Set the piano snap to **1/4 T** and draw/quantize quarter-note triplets. Confirm a velocity drag changes only velocity and colour, never the note's on-screen location, and that undo/redo restores velocity changes.
8. During playback, show a selected MIDI clip's piano roll. Compare the gold piano-roll playhead to the arrangement playhead at the start, in the middle, and near the end of the clip; seek via the arrangement ruler, pause, and resume. Both should indicate the same musical position. A clip that does not cover the transport position deliberately hides the piano playhead. Repeat with the piano roll scrolled and zoomed.
9. Compare measure numbers and beat markers in both views, including a **3/4** or **6/8** test project and an optional tempo change. Verify marker positions agree with the arrangement at the same transport time. Looping clips show the source-sequence position and need Mac verification across loop boundaries.
10. Check the Snap dropdown order (**1/4** immediately followed by **1/4 T**, and likewise for each smaller value). At **H+** zoom, verify the snap subdivisions are visible but lighter than beats; at **H-** zoom, verify dense subdivisions hide while bar lines remain clear. Draw a note at a fine subdivision when zoomed out, then zoom in to confirm its position and timing have not changed.
11. With a **1/16** grid, compare each subdivision across consecutive measures: the line spacing should remain even from one bar line to the next. Click a small tick in the piano-roll ruler; the playhead should jump to that exact subdivision and agree with the arrangement. Draw/quantize a note at the clicked grid location. Test again with a shifted MIDI clip and with **1/4 T**. Confirm neither existing notes nor playback channels change merely because the grid is displayed or the ruler is clicked.
12. Find the **− / + buttons next to Tracks at the top of the arrangement**, including when the sidebar's zoom menu is offscreen. Repeatedly zoom in/out around the transport playhead; the MIDI clip and arrangement bars should scale together without affecting the tempo or the piano-roll zoom. Confirm the preset/fit menu still works afterward.

### Verification boundary

The feature is checked against Tracktion/JUCE C++ headers in a Linux environment. The macOS Xcode build, Audio Unit execution, physical Oxygen 25 input, and on-disk save/reload behavior must be confirmed on the Intel MacBook before merging. The included GitHub Actions workflow is an on-demand Intel build check.

The Tracktion checkout is read during configuration. Studio writes the adapted example editor into its own build directory. Project files use the existing `.tracktionedit` format and Studio retains the previous audio-device and plugin settings location.

GPL-3.0-or-later. See LICENSE.
