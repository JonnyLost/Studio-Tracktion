# Studio Cinematic Atelier — M3 full instrument experience

**Status:** Draft feature branch. The source artwork is the product owner's original Solo Violin Cinematic Atelier mockup, not a runtime screenshot. Source assets from that image are **not** imported into the app as fake GUI sprites.

## Product design contract

Cinematic Atelier has **one** original Tracktion-hosted Audio Unit instance and three responsive presentations.

1. **Compact Split:** 425-point total lower-workspace dock at 100% interface size, independent of the other workspaces' resizable split height. One Studio header, complete visible portrait, four Atelier pages without a mandatory scroll for standard MacBook sizes.
2. **Floating:** initial and saved geometry are preserved (default ~960×510 logical points before a user resize). The same instrument editor is moved, not recreated. **Full View** enlarges that window within the current display's user bounds.
3. **Expanded:** the same editor uses a larger layout when there is sufficient height and width. Full-size layout includes the real MIDI preview keyboard and an instrument-artwork area independent of the panel controls.

A separate walnut/bronze/amber LookAndFeel styles only Cinematic Atelier. Studio's arrangement, Browser, Piano Roll, other native Synth/Sampler editors, original ARIA plug-in windows, and Audio Unit behavior are untouched.

## Functionality

- **Main:** verified user-defined articulation mappings, track volume, track pan, exposed Audio Unit Expression when available, and live Stage pan/depth/send trim in full presentation.
- **Articulations:** up to eight verified mappings. Clicking changes the active mapping in the exact track-stored Studio Articulations XML profile keyed by Audio Unit instance ID. Undo/Redo is supported, and the full mapping palette remains available for editing/extra mappings. It does not guess preset-specific MIDI messages.
- **Mixer:** the **same** track fader and balance as Studio's Mixer; optional AU Expression exposed only when published by that loaded instrument.
- **Advanced:** open the **original** hosted AU editor, Studio Articulation Mappings and existing Orchestral Stage, plus real Stage/routing status.
- **Stage:** an existing Studio seat can be adjusted from the full Main page using pan, depth and send-trim sliders. Edits target the canonical `STUDIO_STAGE_SEAT` track ValueTree and use the project Undo manager; enabled native Stage audio is updated through `spatial::updateRoomSend`. Controls are disabled for unplaced/frozen/recording tracks. The UI never enables routing implicitly.
- **Keyboard audition:** the six-octave keyboard (C2–B7) is a GUI mouse input to `AudioTrack::injectLiveMidiMessage`, the same existing live-MIDI route Studio uses for articulation audition. It requires an enabled, loaded Audio Unit and active playback context, outside recording/frozen states. **MIDI preview channel** is user-selectable 1–16; this is only an audition setting and does not rewrite MIDI clips, track filters or the ARIA patch. The active verified articulation can be sent before the note when it targets that channel. Note-offs are sent on release, drag-away, mouse exit, focus loss, page-size changes and editor destruction. An unavailable AU displays a disabled-preview explanation.
- **Persistence:** per-instance view state v4 contains selected portrait, page and preview channel; v2/v3 view states still restore. Floating geometry is stored separately from native Synth/Sampler windows. No change to the project audio/MIDI engine.

## Performance and independence

All images are decoded/resampled on the GUI thread on selection. The scene background and keyboard are vector painted; there are no new audio callbacks, worker processes, commercial plug-ins, libraries, external network requests, or runtime artwork downloads. GUI status updates run at 6 Hz and avoid rewriting unchanged labels.

## MacBook acceptance checklist

- Open a project with ARIA on an Intel MacBook. Confirm Studio's existing ARIA patch survives opening/closing/reopening Atelier, and Verify ARIA/save gating remains intact.
- Test 1536×960 and 1280×800 app windows at 100% scale. Dock must remain 425 points when room allows; art top/bottom must fit; no clipped Float, tabs, slider text boxes, or full mappings action. Confirm the Piano Roll still resizes by dragging its divider.
- Float, resize, enter Full View, switch among all four pages, Dock, reopen. Confirm selection, artwork, page, preview channel, volume/pan and project MIDI remain the same.
- With known valid articulation mappings: select/toggle a mapping; verify the existing Articulation Palette mirrors the active value and Undo restores it. Verify unmapped ARIA programs show a truthful empty state.
- With an explicitly placed Stage seat: adjust pan, depth, room trim; confirm Stage editor/Undo reflect changes. When Stage audio is enabled, compare native room send display. When not enabled, no send is created by Atelier.
- With a real loaded Audio Unit and valid playback context: select preview channel, mouse-play C2/C#2 and higher keys; release, drag off, change page and close editor while a note sounds. Confirm every note ends and live performance never modifies clips.
- Confirm the original Audio Unit editor still opens and can access the actual Garritan patch settings.
- Check both Intel macOS and Linux CI. Capture compact and full Main, Articulations, Mixer and Advanced screenshots from `--studio-adaptive-workspace-review`.

## Deliberately deferred

The original mockup also depicts an automatic patch-name browser, eight *built-in* articulations without mapping verification, custom studio-room artwork, extra dynamics engines and deep AU routing/MIDI channel edits. Studio does **not** fabricate those features. They require instrument-specific metadata and their own implementation milestones. The original mockup remains the style target, not a claim of pixel-for-pixel delivery.
