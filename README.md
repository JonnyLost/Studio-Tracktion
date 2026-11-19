# Studio 0.2 Arrangement

This is the first interface milestone for the standalone JUCE and Tracktion Engine app. It starts from the Studio 0.1 automatic ARIA restoration edition and builds as a separate app bundle in this folder.

## Arrangement window

- The top bar shows project controls, transport, time position, and tempo. Space controls playback from the arrangement and Command-S saves.
- Drag the left divider to resize the sidebar. Select a track to inspect its recording status and adjust its volume and pan in the Mixer.
- Click the ruler to seek. Change the time range with the sidebar zoom menu, including **Fit project**. Track lanes show MIDI notes and audio waveforms. Use the track's **I** and **A** controls for input and recording, and the **+** in Inserts to load an instrument or effect.
- **Mixer** and **MIDI Monitor** share the bottom workspace. Drag its upper divider to change its height, or click **Hide** to reclaim timeline space. The monitor can still open in a separate window and dock again.

## Build and launch on Intel macOS

Quit any running Studio instance. Extract the source ZIP into Downloads, then run:

```bash
cd "$HOME/Downloads/Studio-0.2-Arrangement" &&
/Applications/CMake.app/Contents/bin/cmake -S . -B build -G Xcode \
  -DTRACKTION_ROOT="$HOME/Developer/Tracktion-Studio" \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 &&
/Applications/CMake.app/Contents/bin/cmake --build build --config Debug --target Studio --parallel 4 &&
open "$PWD/build/Studio_artefacts/Debug/Studio.app"
```

## Mac verification

1. Open a new project, add or select an instrument track, and assign the Oxygen 25 using **I**. Check that the track arms and the MIDI Monitor displays the keyboard's actual channel. Record, stop, and check that the MIDI clip remains and plays after reopening.
2. Import a short audio clip and verify the waveform and playback. Open the Mixer, select the track, and test volume and pan. Resize the sidebar and the bottom workspace, then float and re-dock the monitor.
3. Open a copy of the bundled Intimate D recovery project. Allow automatic restoration to run, then open ARIA and confirm that Intimate D appears and plays. Click **Confirm ARIA** once you have verified sound. Save the copy, reopen it, and check again. See [RESTORATION_TEST.md](RESTORATION_TEST.md) for the restore log and recovery behavior.

The Tracktion checkout is read during configuration. Studio writes the adapted example editor into its own build directory. Project files use the existing `.tracktionedit` format and Studio retains the previous audio-device and plugin settings location.

GPL-3.0-or-later. See LICENSE.
