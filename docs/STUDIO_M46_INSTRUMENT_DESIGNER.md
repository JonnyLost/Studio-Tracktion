# M46 — Instrument Designer and permanent profiles

M46 builds on M45 commit c264ab4d4b739c82d89e92c9528c328ca69d4f33. The existing playback path is unchanged. No branch is merged.

## How to use it

1. Select an instrument track (optional), then choose **Instruments > Instrument Designer...**. The View popup also offers Instrument Designer. A floating, resizable window opens. Closing hides the window and preserves its draft until Studio exits.
2. Name a profile, enter the library and exact patch/variant, MIDI channel and slot. A new draft takes the selected track name and hosted plug-in identifier/version as reference only. It does not inspect ARIA's internal slot state; confirm those fields yourself. To use another selected track, reopen the designer and choose New after saving or discarding the current draft.
3. Search and select a physical instrument from M45. Select a vector artwork silhouette independently (defaults to the instrument on first selection). No new raster files are required.
4. Choose Articulations or Performance controls; search suggested candidates or enable Show all techniques. Add to layout. Suggestions are discovery terms, not verified patch capabilities.
5. Select an added element, change its label and choose Button/Knob/Fader where applicable. Check Transport and/or Full view. Apply element, move up/down, or remove. Element edits are committed when changing selection, adding, reordering or saving. At least one view is required.
6. Review the compact strip and horizontal full-view preview below. The preview is static: it never emits MIDI. Full view follows the plane sketch: art on the left, articulation matrix, controls, proposed utility shortcuts and optional channel information. Crowded previews show overflow; every saved item remains in the ordered layout. Utility shortcuts are placeholders, not live buttons.
7. Click Save profile. Select a saved entry and click Open to reuse its design. Duplicate makes a separate profile ID and leaves the original unchanged. Save or explicitly discard before opening another design. Discard edits restores the saved version (or resets an unsaved new draft). Save before quitting; there is no quit-time draft recovery.

## Persistence and security

macOS: `~/Library/Application Support/Studio/Instrument Profiles`. One UUID-named `.studioinstrument` XML file per profile. Back up that directory to retain the collection. No sample data, plug-in state blobs, credentials, absolute sample paths, or live routing. File writes use JUCE TemporaryFile replacement. Invalid files are skipped, malformed/version-unknown data is rejected, and a failed read does not change the output profile. Profile and element IDs, ordering, custom labels, presentation, independent visibility and identity metadata are serialized. Element mappings are explicitly unassigned; a schema-1 document containing a live binding is rejected. Future versions must migrate rather than silently strip bindings. A profile name need not be unique; the UI shows the short stable ID.

The design is a global reusable collection, not yet a track association or project snapshot. No automatic patch loading, project changes, MIDI learn or keyswitch routing. The existing instrument transport and full inspector remain unchanged until M48. Mixer concepts are selectable labels in the preview, not yet connected to mixer parameters. Native artwork uses existing silhouette matching; unmapped names can fall back to generic artwork. Some M45 family suggestions require review; do not interpret them as installed articulations. Editing the same profile in two Studio processes at once is not supported.

## Validation

- Native CTest StudioInstrumentProfiles: XML/storage roundtrip, order, duplicate IDs, rejection of invalid visibility, unsafe IDs, bad channels, unsupported schema/bindings, corrupt file isolation.
- Existing StudioInterfaceReview exercises the actual DesignerPanel using a disposable directory: flute selection, technique/control selection, fader and visibility edits, reordering, saving, duplication, reopening and 660x420 layout.
- Intel workflow builds both M45 and M46 test executables explicitly; Linux and Intel workflow branch filters now include M46 and the M45 review base.
- Build and CTest execution are pending CI; no Intel Mac build or ARIA audition has been performed during this implementation session.

## Next milestones

M47 adds manual right-click bindings and MIDI Learn to the profile contract; M48 binds the transport and full instrument to the same profile and track state; M49 reconciles local ARIA inventory. Keep semantic IDs, patch identity, actual binding and layout identity distinct, and never send an unassigned suggestion.
