# Studio 0.2 — Arrangement and MIDI Piano Roll

Standalone JUCE/Tracktion Engine DAW for Intel macOS, based on the Mac-verified Studio 0.2 arrangement and automatic ARIA restoration milestone.

## Arrangement window

- The top bar shows project controls, transport, time position, and tempo. Space controls playback from the arrangement and Command-S saves.
- Drag the left divider to resize the sidebar. Select a track to inspect its recording status and adjust its volume and pan in the Mixer.
- Click the ruler to seek. Change the time range with the sidebar zoom menu, including **Fit project**. Track lanes show MIDI notes and audio waveforms. Use the track's **I** and **A** controls for input and recording, and the **+** in Inserts to load an instrument or effect.
- **Mixer**, **MIDI Monitor**, and **Piano Roll** share the bottom workspace. Drag its upper divider to change its height, or click **Hide** to reclaim timeline space. The monitor can still open in a separate window and dock again.

## MIDI Piano Roll

1. Click a recorded MIDI clip in the arrangement. The editor opens automatically. Alternatively, select an instrument track, position the playhead, and click **+ MIDI Clip** to create a four-bar MIDI clip and open the editor.
2. Choose **Draw** to place notes. Drag during drawing to extend their duration. Choose **Select** to click a note and drag it horizontally or vertically. Drag either edge to resize; a horizontal resize cursor indicates each trim handle. Delete or Backspace removes the selected note.
3. Drag a note's stem in the **Velocity** lane to edit its velocity. Note and velocity-bar colors follow the same continuous color wheel from soft blue to hard red. The **Snap** menu includes straight values and **1/8 T**, **1/16 T**, and **1/32 T** triplets. **Quantize** snaps the selected note, or all notes if none is selected. **Undo** and **Redo** operate on Tracktion's project undo history. Command-Z and Command-Shift-Z work when the piano roll has keyboard focus.
4. Use the scrollbars to navigate beats and pitches. Shift-scroll moves horizontally. Command-scroll or **H+ / H-** adjusts horizontal zoom. **V+ / V-** adjusts piano-key height (vertical zoom) without jumping to a different pitch range. Click **Save**, quit, and reopen to verify edits persist.
5. Click a note or a piano key at the editor's left edge to briefly audition it through an **existing MIDI input assigned only to that clip's track**. Studio does not change input routing, channel data, arming, or project state to preview a note. If the track does not have an enabled, exclusively assigned MIDI input, previews are silent until one is assigned with **I**. Preview uses the clip's MIDI channel (MIDI channel 1 if unspecified), not a forced channel 16; confirm the ARIA slot listens on that channel.

When reopening a project containing ARIA, piano editing remains available. An **ARIA CHECK** reminder and **Retry ARIA / Confirm ARIA** controls appear near the top of the left sidebar. Studio still blocks **Save** until you have verified the restored ARIA slot sounds correct and clicked **Confirm ARIA**; this protects the original AU state and backup. Editing a clip is not the same as confirming safe ARIA restoration.

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

1. Record a short MIDI take with the Oxygen 25. Stop, click its clip, and draw, move, resize from **both edges**, delete, and change note velocity. Quantize a triplet note using **1/16 T**. Undo and redo. Save, quit, and reopen the project. Verify MIDI editing stays enabled, exact notes and playback survive, and neither channel nor track routing changes.
2. Create a new MIDI clip with **+ MIDI Clip** and draw four notes. Check the sound through ARIA and that edited note channels remain the clip's original channel.
3. Import an audio clip and test playback, mixer volume/pan, monitor float/dock, and recording take retention.
4. Select a note to audition it and click the piano keyboard. If sound is absent, assign an enabled MIDI input with **I**, confirm the clip MIDI channel matches the ARIA slot, and retest. Check **H+/H-** and **V+/V-** on a small MacBook screen.
5. Open a **copy** of the bundled Intimate D recovery project. Look for the visible **ARIA CHECK** sidebar reminder. Allow automatic restoration to run, verify the instrument appears and plays, confirm piano editing works **before** confirmation, then click **Confirm ARIA** to allow saving. See [RESTORATION_TEST.md](RESTORATION_TEST.md).

### Verification boundary

The feature is checked against Tracktion/JUCE C++ headers in a Linux environment. The macOS Xcode build, Audio Unit execution, physical Oxygen 25 input, and on-disk save/reload behavior must be confirmed on the Intel MacBook before merging. The included GitHub Actions workflow is an on-demand Intel build check.

The Tracktion checkout is read during configuration. Studio writes the adapted example editor into its own build directory. Project files use the existing `.tracktionedit` format and Studio retains the previous audio-device and plugin settings location.

GPL-3.0-or-later. See LICENSE.
