# Adaptive instrument workspace

## Lower workspace

Use the presentation menu in the top-right corner.

- **Performance** shows the selected instrument’s main controls and the articulation strip. The panel uses the same height when you change tracks or presets. When a docked editor and large appearance settings compete for space, Studio keeps the editor and articulation strip usable. Closing the editor brings the performance dials back.
- **Articulations only** leaves a shallow strip. Open **Piano Roll** from the left rail, then select a region to compose with the strip visible.
- **Workspace hidden** closes the lower workspace. You can still explicitly open Piano Roll, Mixer, Stage, or a detailed editor. The performance presentation is remembered in Studio’s preferences.

Compact, Split, Expanded and the divider still control the larger editor above the performance area. **Detailed Editor** opens the selected native instrument’s existing editor. **Float** and **Dock** move that editor between windows without replacing the instrument. A loaded Audio Unit exposes the plug-in’s discoverable parameters and **Open Instrument** opens its own window.

Studio Synth offers family-specific Sound controls, an Output page, and a layer selector for hybrid patches. Native edits go through the existing instrument setting and preset APIs. Dials commit a native preset edit when released, so a drag makes one undoable change. The Synth automation parameters use their existing Tracktion bindings. Studio Sampler exposes envelope, tuning, bend range and level. Its mapping, loops and zones remain in Detailed Editor.

## Articulations

The strip and palette use the same project mappings. Choose **Mappings** to add a name and MIDI message you have verified for the receiving patch and channel. The strip offers banks of six for larger profiles. **Pin** holds one track’s mapping profile. **Follow** returns to the selected track.

- Clicking a pad selects the track’s active articulation. It changes no MIDI until you draw a note or explicitly use Insert. Studio saves the selection with the instrument’s mapping profile, and it participates in project undo.
- **Audition** sends the selected message to the enabled instrument through its existing live route. The palette’s Ctrl-Command-digit shortcuts still audition their assigned mappings.
- **Insert** writes the selected mapping at the playhead inside an editable MIDI region on the matching track and channel.
- With an active mapping, **Draw** in Piano Roll writes a needed switch alongside the new note, in the same Undo transaction. An already-effective switch is reused. A keyswitch needs a short lead-in inside the region. Leave space before the first musical note or place the switch manually with Insert.

Automatic articulation writing currently covers drawn notes in linear, unmuted MIDI clips without alternate takes, MPE, playback quantisation or groove. It skips hardware recording, paste, step input and looped clips. A patch’s MIDI map is supplied by the user, and Studio never advertises a guessed keyswitch for ARIA or another library. Changing a hosted patch may change its mapping, so verify the profile after a patch change.

## Header

Return to Start, Stop, Play, Record, Cycle and Metronome form the transport group. Select BPM or Meter while stopped to edit the event at the playhead. **Timeline** opens the remaining metronome, count-in, tempo, meter and cue options. The elapsed clock is in the central display when space permits, and is available in the Bars / Beats tooltip at wider scaling. The header uses the edit’s elapsed time.

**Project** holds New, Open, Save, Save As, Audio Settings, Plugin Manager, MIDI Monitor, import and exports. On wider layouts, New Project and Open Project also have direct buttons. CPU opens Resource Inspector, which is also reachable from View. The track toolbar keeps instrument, audio, clip and region actions close to the arrangement.

## Verification

`StudioAdaptiveWorkspace` exercises native parameter bindings, preset switching, track-local articulation state, actual Piano Roll Draw gestures and MIDI insertion, undo/redo, save/reopen, Float/Dock playback and multiple sizes and scales. Existing Composer, Stage, sampler, synth, and editor suites continue to run in CI.

The [phone-friendly gallery](reviews/m9/README.md) uses image captures from the Intel macOS test application. The orchestral mappings and unavailable Audio Unit are labeled fixtures. Installed ARIA patch restoration, a live Core Audio and MIDI session, and physical MacBook accessibility remain hardware acceptance.

[Build Studio on an Intel MacBook](composer-workflow-m8.md#build-on-an-intel-macbook)
