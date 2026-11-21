# Unified Orchestral Stage

Open **Stage** to see the current project’s audio tracks grouped by instrument family. The roster shows which tracks are unplaced and which have Stage audio enabled. Drag a track onto the semicircle to place it, or choose it and click **Place on Stage** for a suggested position. Click a marker or roster row to select its Tracktion track, even if another track has the same name. Drag a placed marker to move it, use the arrow keys for finer changes, or hold Shift to move its placed section.

Use the inspector to set family, pan, depth, and room-send trim. **Reset seat** returns the placement to centered pan and default depth while keeping the family and trim. Stage audio is opt-in per track. Moving or arranging tracks leaves the enablement choice with the user. **Enable stage audio** uses the existing Studio stereo balance and shared Room Return. At the same trim, greater depth increases the send by up to 10 dB from front to rear. Use **Rooms** for factory or custom room settings, and **Microphones** for geometry and named setups.

**Compact** shows a Stage overview and placement counts. **Open Stage** switches to Split for editing. Drag Studio’s workspace divider or use **Expanded** to give the stage more room. **Auto Arrange** applies a placement layout to existing tracks in the chosen scope. On macOS, **Arrangement Reference** opens the original VSL catalog and Modern Symphony reference layout. Those references describe orchestration without requiring a Vienna library or creating project tracks.

## Saved projects and routing

Each audio track owns its authoritative `STUDIO_STAGE_SEAT`. The workspace resolves the track by Tracktion ID. A track’s saved pan and depth map to a chord of the semicircle, so the full pan range remains available at every depth. The drawing leaves the existing pan and depth coordinates unchanged on project load.

Older `studioPhysicalSeating` data remains available in Arrangement Reference. Reference entries have their own IDs and no Tracktion binding. Studio preserves both formats on open, avoids matching by names, and requires explicit placement of tracks that have no acoustic seat. A drop, drag, or inspector change records one project undo action along with the corresponding enabled routing parameter changes.

## Microphone and acoustic scope

Virtual microphone placement, pickup patterns, stereo-pair geometry, capsule height and aim, and the virtual mixer’s gain, pan, delay, mute, and solo are visual setup data. The microphone page does not capture audio, create individual mic outputs, or render microphone pickup or directional propagation. Its shared-room bypass controls the actual room. The audible Stage uses the existing stereo balance, depth-based send gain, and shared room engine. It does not render per-seat reflections, HRTF, or per-source distance delays.

## Verification

`StudioUnifiedStage` exercises the roster drop, duplicate-name track binding, marker movement, pan and send updates, Undo and Redo, legacy and updated project reopening, and track deletion. The Intel workflow publishes Compact, Split, and Expanded screenshots as `Studio-M7-unified-stage-macOS`. The existing hosted routing test exercises the audible wet and dry paths. The repository’s Intel build and launch instructions apply when checking out `feature/studio-unified-acoustic-stage-m7`.
