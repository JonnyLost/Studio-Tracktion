# Unified instrument workspace

Milestone 6.5 starts at `0b2919f083d8851b0393d596a0e719cc7c6b2805` on `feature/studio-unified-integration-m6` in `JonnyLost/Studio-Tracktion`. That integration contains Studio Environments, both native instruments, the original Synth catalog and the Intel regression workflow.

The workspace uses Studio’s existing dock, viewport and arrangement divider. A message-thread host owns one native editor and holds its associated Tracktion plug-in reference. The chooser and navigation use the Tracktion plug-in ID within the current Edit. Floating transfers the same editor from the workspace to a resizable JUCE window. Dock reverses the transfer. Page changes and presentation changes stay outside the audio graph. Tracktion keeps ownership of MIDI, audio, automation and the instrument state.

Selecting another track resolves its native instrument instances and retains the previous editor’s navigation separately from its sound. The host releases the editor before releasing its plug-in reference, and resets before the Edit is destroyed. Studio’s Tracktion window adapter handles native editor requests from the arrangement footer. Hosted Audio Units continue to use Tracktion’s existing floating windows, including the ARIA restoration path.

Application preferences hold floating geometry and project-scoped editor navigation and presentation. Project and preset formats continue to carry the actual instrument settings.

## Using the workspace

- Add or open Studio Synth or Studio Sampler through **Instruments**, **Channel**, or the arrangement plug-in button. Select a native instrument track while the instrument workspace is active to follow it. If the track has several native instruments, use the selector in the workspace header.
- Drag the dotted arrangement divider to resize the dock. Use **Expand** for detailed editing, or use the existing **Layout** menu for Compact, Split, Expanded, and Hidden. **Float** transfers the current editor to a resizable window. **Dock** or the floating window’s close button returns it to the dock.
- In Synth, **Browse Presets** shows Factory and User libraries. Filter by name, synthesis family, and musical category, select a result, and press Return, double-click it, or choose **Load Sound**. The keyboard auditions the loaded sound. Studio asks before replacing unsaved edits. **Save Preset** writes a `.studiosynth` file. Presets saved to the default Synth Presets folder appear in the User library. **Load Preset** opens a compatible preset from any location.
- In Sampler, use **Basic** to load a sample, set its root note, and shape volume and the amplitude envelope. **Mapping** contains key and velocity ranges, zone gain, tuning, and round robin. **Loops** contains the markers and playback mode. **Advanced** contains instrument tuning, polyphony, and pitch bend range.

Studio remembers the last instrument, layout, dock or float state, editing page, and Synth browser visibility for a project on the current computer. Floating size and position are shared application preferences. Moving a project carries sound settings and sample references with it. Presentation preferences are stored in the Studio settings on the original computer.

## Build and automated verification

Use the [Intel MacBook build and launch commands](../README.md#build-and-launch-on-intel-macos). To run the complete suite and capture the native instrument review screenshots, launch the commands from a signed-in macOS desktop session after building Studio.

```bash
/Applications/CMake.app/Contents/bin/cmake --build build --config Debug --parallel 4 &&
STUDIO_INSTRUMENT_SCREENSHOTS="$PWD/build/instrument-screenshots" \
  /Applications/CMake.app/Contents/bin/ctest --test-dir build -C Debug --output-on-failure
```

The `StudioInstrumentWorkspace` test uses Studio’s actual controls, Tracktion’s hosted audio device, and a generated sample. It checks docked and floated editor identity, playback, preset filtering and loading, layout and appearance, project reopening, and editor teardown. The Intel workflow uploads the Synth and Sampler screenshots as `Studio-M65-instrument-workspace`.

## Physical Intel MacBook acceptance

1. At 1024 × 640 and 1280 × 800, use the dotted divider and cycle Compact, Split, Expanded, and Hidden. At 175% with Extra Large text, scroll through Synth Sound, Shape, Modulation, and Advanced and Sampler Basic, Mapping, Loops, and Advanced. Check that the preset browser, keyboard, and main controls remain reachable. Repeat Float and Dock, and verify keyboard focus and VoiceOver announcements.
2. Play a sustained live MIDI chord through each instrument at 44.1 and 48 kHz. Keep the pedal down while changing pages, dragging the divider, and floating or docking. Listen for dropouts and stuck voices. Repeat during playback and recording with the actual device and buffer size used for production.
3. Browse the factory library, change a parameter, and try loading a different preset. Verify both **Keep Editing** and **Load Sound**, then save a user preset and recall it. Build a two-zone sampler patch with a sustained loop, record a MIDI take, and confirm that the track and room remain audible through the Mixer and Master. Save, quit, and reopen the project to check both sounds and their restored editor presentation.
4. On a copy of a project with Garritan ARIA installed, open its existing floating Audio Unit window, verify that its patch restores, then alternate between ARIA and both native editors during playback. Check the Room Return and offline bounce while the native editor is floated. Audio Unit docking is deferred.
