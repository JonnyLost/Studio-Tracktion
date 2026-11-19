# Studio 0.1

A standalone macOS DAW prototype using the existing Tracktion Engine and JUCE checkout.
Studio is a separate CMake target and git repository. Its editor adapts Tracktion's
example timeline at configure time. Tested source baseline for generating the editor
is Tracktion commit `c6293127cdc0a8488aea2ae0907f4709ee89c228`.
The generator accepts both upstream code and the local Oxygen 25 patch.

## Build on the Intel Mac

Extract the archive into Downloads, then paste this block into Terminal.
The CMake application and existing Tracktion checkout must be in the paths shown.

```bash
studio_src="$HOME/Downloads/Studio-Tracktion"
studio_cmake="/Applications/CMake.app/Contents/bin/cmake"
"$studio_cmake" -S "$studio_src" -B "$studio_src/build" -G Xcode \
  -DTRACKTION_ROOT="$HOME/Developer/Tracktion-Studio" \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 &&
"$studio_cmake" --build "$studio_src/build" --config Debug --target Studio --parallel 4 &&
open "$studio_src/build/Studio_artefacts/Debug/Studio.app"
```

## First session

1. Click **New Project** and pick a new filename. Studio saves a Tracktion edit
   and starts with a track. Add tracks with **+ Instrument** or **+ Audio**.
2. Open **Plugin Manager** and scan for Audio Units. Click **+** at the right
   end of the instrument track and choose ARIA Player. Open ARIA from the
   new plug-in button and load an instrument in slot 1.
3. With the Oxygen 25 connected before opening the project, use **I** on that
   track to select it. Selecting a MIDI input arms recording automatically.
   **A** toggles recording arm. Play a few notes on MIDI channel 1, then use
   **Record** and **Stop Recording** to capture a take. Return to start and play
   it back. The timeline should show a MIDI clip containing your notes.
4. Click a track header to adjust volume and pan. Track headers also provide
   mute and solo. Import a WAV with **Import Audio** to place it on a new
   audio track at the playhead. Studio references imported audio in place.
   To try audio recording, choose an audio input under **I** on the audio
   track and arm it explicitly with **A**. For a microphone, use headphones
   and grant macOS microphone permission if prompted.
5. Click **Save**, then close and reopen the project. Check placement, mixer
   settings, MIDI notes, and ARIA patch recall.

The timeline has a seekable seconds ruler, zoom presets and vertical
scrolling, audio waveforms and MIDI note previews. This prototype uses
Tracktion's shared audio/MIDI track model. The track buttons set the track's
initial name and intended workflow. Detailed note editing, clip move and trim,
dedicated channel strips, MIDI channel selectors, and picture scoring are
outside this first build.

The build directory owns the generated editor. The Tracktion source checkout,
DemoRunner target, and original Swift/Alpha 1 work are independent of Studio's
build. This prototype source is GPL-3.0-or-later. Tracktion and JUCE are
separately licensed dependencies.

## Verification status

The CMake editor generation was checked against the upstream editor and a
fixture with the local Oxygen 25 patch applied. Both generated identical
Studio editor source. A full configure in the Linux workspace reached JUCE
and stopped in its build helper because Linux GUI development headers were
unavailable. The Intel app build and the hardware workflow above need
verification on macOS.
