# Studio 0.2 — Mixer and selected-track Inspector (M13)

## What changed

Studio's Mixer and Channel controls now have different, understandable jobs:

- **Mixer** (left rail) is the place to balance the whole project. Channels remain in a horizontally scrollable bank, with a **pinned Master** on the right. Each channel has its track name, instrument/return description, **FX**, **Stage** send access, pan, a tall level fader, stereo peak meter, Mute (**M**), Solo (**S**), Record Arm (**A**), and output routing. Selecting a channel also changes Studio's selected track. Audio and MIDI input selection has been removed from the narrow mixer strips because it is easier to understand in the Inspector.
- **Inspector** (top-right button or **View → Show Selected Track Inspector**) displays details for the selected arrangement/Mixer track. It uses the existing, proven Channel inspector implementation: Audio/MIDI Input, Arm/Monitor, Instrument editor, Insert FX, Sends/Stage, Output, pan/volume and MIDI channel/ARIA status. Switching tracks updates the same controls.
- The Inspector is a **collapsible overlay** on the right. At 1280×800 and 1024×640, it can scroll vertically. **Hiding it restores the full arrangement and lower workspace width**. It does not shrink piano-roll, mixer, or timeline geometry. Its open/closed state persists in Studio preferences (not in the project file).
- The former **Channel** icon is removed from the left navigation. The original Channel workspace implementation remains temporarily accessible through **View → Classic Channel Workspace (temporary)** for testing and rollback; it is not deleted.
- The **Mixer Master fader and meter** no longer reserve room for invisible track-only controls, giving them significantly more travel.
- No Tracktion playback graph, routing, sampler/synth engine, Stage reverb, AU hosting, MIDI recording or project serialization changes are introduced by this milestone.

## Your normal workflow

Click **Violin** in the arrangement to select it and audition from your MIDI keyboard. Click **Inspector** to see its instrument, inputs, FX, output and stage send. Open FX or the instrument editor as needed. Click **Viola**—the Inspector follows Viola. Close it to get all your screen space back. Click **Mixer** for all the channels together. The **A** in the Mixer arms *recording*; selecting a track does not.

If a lower Channel workspace is open while you open Inspector, Studio closes that redundant workspace. It can still be opened manually from View for acceptance testing.

## MacBook acceptance checklist

1. Add at least four tracks (Studio Synth, Studio Sampler, an Audio Unit such as ARIA if available, and Audio). Select them in the arrangement and mixer; confirm the Inspector follows the chosen track without arming recording.
2. Open FX, Instrument, Audio/MIDI Input, Stage sends and Output from Inspector. Verify correct instrument, actual routing names and working controls. Don't save a project with unverified ARIA restoration.
3. Toggle Inspector open/closed, close from within its drawer, and verify the timeline width and lower Piano Roll layout are unchanged by the overlay. Check 1024×640 and 1280×800, including Large/Extra Large text.
4. Open Mixer in **Split** and **Expanded**. Ensure faders have practical travel, meters work when audio plays, scrolling shows all channels, selected strip is highlighted, and **Master** stays visible. Check track Mute/Solo/Arm and fader changes as well as Undo.
5. Confirm the Stage return strip, room send, Master output, and standard track output remain correct. Confirm stage acoustics are unaffected.
6. Save/reopen the project and play instruments, stop, seek, record and export. Verify no audio routing changes and no stuck MIDI notes.
7. Verify View → Classic Channel Workspace (temporary) still exposes the older detailed channel panel; it can be removed only after owner acceptance.

## Automated review

The existing native interface review now captures **mixer-1280x800.png**, **mixer-split-1280x800.png**, **right-inspector-1280x800.png**, and **right-inspector-1024x640.png** from running Studio. It tests whether Inspector follows selection, does not alter ruler/timeline width, and restores arrangement width on closing. Existing 45 appearance/viewport combinations, synth, sampler, MIDI, ARIA and routing suites must remain green.

Linux + Intel macOS CI results and screenshots should be added to the PR after they actually complete. Physical MacBook/MIDI hardware acceptance remains unverified until the owner tries the build.

**Scope boundary:** This is the first complete Mixer/Inspector layout pass. A deeper mixer redesign (send lanes, user-configurable banking, integrated mixer automation and scalable metering) can follow owner screenshots and feedback. No merge without explicit product-owner approval.
