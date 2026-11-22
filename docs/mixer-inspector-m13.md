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

## M13 finishing pass — composer review (November 22, 2026)

The real Intel Mac screenshots highlighted several problems that are resolved on this branch:

1. **Mixer-focused workspace.** Choosing Mixer removes the instrument Performance panel and the articulation strip from the visible lower workspace, but does not unload instruments, change MIDI focus, stop audio, or touch routing. The first switch to Mixer requests a taller split console, keeping a smaller arrangement visible above. MIDI clip selection while the Mixer is active does not unexpectedly force-open Piano Roll. Compact and Expanded remain explicit choices.
2. **Narrow, functional console channels.** The main strips are approximately 96–105 logical points wide at 100% UI scale (scaling with accessibility options), rather than 120–134. FX and Stage buttons are stacked, and the pan slider becomes a rotary knob with double-click-to-center. Faders and stereo meters keep their own vertical space.
3. **Selected-track channel strip in Inspector.** The right drawer shows a persistent working fader, stereo meter, pan, track name, color and icon category controls, and visible MIDI input/output information. Selecting a different track follows that track; it does not record-arm or reroute it.
4. **Track appearance and routing.** The Color menu selects an actual Tracktion track color, and the Icon menu stores an instrument-category label in the track's ValueTree with Undo support. Existing engine/project state loading is preserved. This is an initial category selector, *not* the final per-instrument artwork/icons collection. The expanded **Track Settings** section contains existing input, instrument, effects, sends/Stage, output and ARIA/MIDI details.
5. **Less clutter.** The Inspector's track settings are collapsed until opened and scroll when necessary. The old Channel workspace remains available under View as a fallback. The Mixer does not display a second instrument panel or a redundant set of channel input selectors.

### Updated acceptance requirements

- Verify the actual native **mixer-focused-1280x800.png** screenshot shows fully visible console faders with only arrangement above and no Performance/Articulation panel below.
- Verify **right-inspector-1280x800.png** shows the selected channel strip, and **right-inspector-track-settings-1280x800.png** shows the expanded settings. Check **right-inspector-1024x640.png** as well.
- Verify the MIDI input and clip output labels are accurate for an assigned physical keyboard and a selected MIDI clip; don't infer the plug-in's receive channel from the clip.
- Change track color and icon; save, reopen and Undo on a disposable project. Make sure the visible name and selected track follow changes. This must not affect MIDI data or audio.
- In Split mode, select a MIDI clip while viewing Mixer. Mixer must stay visible. Switch to Piano Roll or Instrument and confirm the appropriate panel and its Performance controls return normally.
- Confirm 1280×800 and 1024×640 layouts, fader movement, centered pan, stereo metering, FX/Stage routing, Master pinning, ARIA save protection, keyboard audition and the metronome/transport.
- Run Linux and Intel macOS CI. Physical MacBook acceptance is still required.

**Scope boundary:** This completes the specified Milestone 13 Mixer/Inspector pass. More elaborate Logic-like dual channel strips, custom individual instrument graphics, full per-track channel/filter inspector controls, and customization of the *transport bar* are separate milestones; do not imply they are fully implemented here. Retrospective MIDI recording is also separate.

### Split height regression fix (November 22)

Linux CI exposed a failure in `StudioInterfaceReview`: switching from Expanded to Split while Mixer was already selected could reuse the previous short dock height, clipping the console. `setDockMode` now reapplies the Mixer minimum height on that transition, using the same minimum imposed when first entering Mixer. This changes only GUI layout policy; no DSP, routing or project file changes. Commit `cd9c752`. **Linux and Intel CI must rerun on this commit before automated acceptance is claimed.**

## M13.1 refinement — MacBook feedback (November 22)

- The right Inspector now starts at 270 logical points and supports dragging its **left edge** to resize, with a constrained 205–510 point range and stored width preference. Its backing is painted as opaque to prevent the timeline from showing through.
- The channel strip paint has a solid background and the Inspector uses an explicit left-edge grip and horizontal-resize cursor.
- Mixer, Channel and right Inspector pan/volume edits now select the **same last gain/pan processor they display**, rather than reading the last processor but writing an earlier processor when there are multiple. This addresses a plausible source of the reported inert pan; confirm actual stereo pan acoustically on hardware before declaring it resolved.
- Right-click an arrangement track header → **Track Header Controls** to independently enable compact Volume, Pan and Audio activity mini-controls. They read and edit Tracktion track state (with Undo) and follow the same gain/pan processor as the main Mixer. The mini meter reads the existing level-meter plug-in without a new audio callback. Per-track options are persisted within the project; inactive headers do not poll.
- Header mini-controls are designed to fit under the existing I/A/M/S/FX buttons inside current arrangement track height. Review at 100% and larger UI/text scales for target size and clipping; further sizing may need refinement from native screenshots.

**Validation gate:** The code changes are committed to this *unmerged* draft branch, but the latest Linux/Intel macOS CI and real MacBook test are required before claiming acceptance. Specifically test an ARIA track with both headphones / stereo output, pan hard left and right, and verify changing pan in the mixer and header updates the Inspector value, audio position and Undo. Confirm the background is opaque, resizing persists, and enabling mini controls on one track does not change another track's header.

## M13.1 follow-up — real MacBook issues (November 22)

Reported on physical Intel MacBook: Studio audio comes from the **left headphone only** even though the headphones play normal stereo music correctly; the timeline guide grid also remains visible across the Inspector.

- **Confirmed UI root cause:** `StudioView::paintOverChildren` draws arrangement time-grid guides *after child components*, so painting an opaque Inspector alone does not stop visible lines. Exclude the Inspector's rectangle from that grid's clip region (`d63d463`).
- **Stereo device configuration guard:** Studio's JUCE `AudioDeviceSelectorComponent` allowed a one-channel output (`minOutputChannels=1`). Require at least two output channels (`minOutputChannels=2`) for the selectable device configuration (`1bc4f1d`). **This does not prove the saved Mac audio device was mono or repair a previously saved configuration automatically.** Do not silently reset the user's saved audio routing.
- **Hardware acceptance:** Open Project → Audio Settings and verify the intended headphones/USB device is selected and both left/right channels are enabled. Test an ARIA or native synth track panned hard left and right; then center. Check the same test with the Stage reverb temporarily bypassed; verify main output and actual stereo meters. The root cause of left-only audio remains unconfirmed until stereo routing and audible playback succeed on the user's MacBook.
- **Inspector acceptance:** With the Inspector open over the timeline at both 1280×800 and 1024×640, no guide-grid lines may draw over the Inspector, including during playback, scroll, and resize.

No repository merges. New CI and screenshot checks required.
