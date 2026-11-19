# Studio 0.2 — Arrangement and MIDI Piano Roll

Standalone JUCE/Tracktion Engine DAW for Intel macOS, based on the Mac-verified Studio 0.2 arrangement and automatic ARIA restoration milestone.

## Arrangement window

- The top bar shows project controls, transport, time position, and tempo. Space controls playback from the arrangement and Command-S saves.
- Drag the left divider to resize the sidebar. Select a track to inspect its recording status and adjust its volume and pan in the Mixer.
- Click the ruler to seek. Change the time range with the sidebar zoom menu, including **Fit project**. Track lanes show MIDI notes and audio waveforms. Use the track's **I** and **A** controls for input and recording, and the **+** in Inserts to load an instrument or effect.
- **Mixer**, **MIDI Monitor**, and **Piano Roll** share the bottom workspace. Drag its upper divider to change its height, or click **Hide** to reclaim timeline space. The monitor can still open in a separate window and dock again.

## MIDI Piano Roll

1. Click a recorded MIDI clip in the arrangement. The editor opens automatically. Alternatively, select an instrument track, position the playhead, and click **+ MIDI Clip** to create a four-bar MIDI clip and open the editor.
2. Choose **Draw** to place notes. Drag during drawing to extend their duration. Choose **Select** to click a note and drag it horizontally or vertically. Drag its right edge to resize it. Delete or Backspace removes the selected note.
3. Drag a note's stem in the **Velocity** lane to edit its velocity. The **Snap** menu controls the beat grid. **Quantize** snaps the selected note, or all notes if none is selected. **Undo** and **Redo** operate on Tracktion's project undo history. Command-Z and Command-Shift-Z work when the piano roll has keyboard focus.
4. Use the scrollbars to navigate beats and pitches. Shift-scroll moves horizontally. Command-scroll or the +/- buttons adjusts horizontal zoom. Click **Save**, quit, and reopen to verify edits persist.

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

## Mac verification

1. Record a short MIDI take with the Oxygen 25. Stop, click its clip, and draw, move, resize, delete, and change note velocity. Quantize a note. Undo and redo. Save, quit, and reopen the project. Verify the exact notes and playback survive.
2. Create a new MIDI clip with **+ MIDI Clip** and draw four notes. Check the sound through ARIA and that edited note channels remain the clip's original channel.
3. Import an audio clip and test playback, mixer volume/pan, monitor float/dock, and recording take retention.
4. Open a **copy** of the bundled Intimate D recovery project. Allow automatic restoration to run, verify the instrument appears and plays, then click **Confirm ARIA** to allow saving. See [RESTORATION_TEST.md](RESTORATION_TEST.md).

### Verification boundary

The feature is checked against Tracktion/JUCE C++ headers in a Linux environment. The macOS Xcode build, Audio Unit execution, physical Oxygen 25 input, and on-disk save/reload behavior must be confirmed on the Intel MacBook before merging. The included GitHub Actions workflow is an on-demand Intel build check.

The Tracktion checkout is read during configuration. Studio writes the adapted example editor into its own build directory. Project files use the existing `.tracktionedit` format and Studio retains the previous audio-device and plugin settings location.

GPL-3.0-or-later. See LICENSE.
