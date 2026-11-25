# Studio 0.2 — Composer Workflow / Post-Integration M6

**Branch:** `feature/studio-postintegration-composer-workflow-m6` based on `feature/studio-room-return-mixer-only` (which in turn starts from the verified Studio 0.2 Integrated Alpha). **Do not merge into `main` or overwrite the installed app until CI and MacBook acceptance.**

## User-visible operations

### Adjustable track height

- Drag the lower 7-point edge of an arrangement track header. Each track saves its own `studioTrackHeight` in the Tracktion project (clamped from 36 to 300 unscaled points).
- Alternatively right-click a track → **Track Height** → Compact, Normal, Tall, Extra Tall or Maximum.
- Drag edits are grouped as an Undo transaction, and the arrangement's vertical scroll content uses the sum of actual row heights. No timeline/clip/audio changes.

### Real folders and group summing

- Studio Track menu → **New Submix Folder...** creates an empty native Tracktion `FolderTrack` with a real stereo submix fader, meter and plug-in chain.
- **Group Selected Tracks...** creates a folder around currently selected top-level tracks.
- Right-click a track → **Move Track into Group** to place additional tracks; **Move Out of Group** restores a member to the top level. A real FolderTrack changes group audio routing in Tracktion as intended: check any existing outputs, special routes and effects before moving tracks.
- Right-click group header to rename or collapse/expand; click the disclosure region left of its name. Collapsing affects *arrangement visibility only*, not routing.
- In Mixer the group has independent Volume, Pan, Mute, Solo, Meter and **Group FX**. Its arrangement header also offers group FX and compact audio controls. Add native EQ, Compressor or Delay directly to the actual folder submix. Existing inserts can be opened and removed via Group FX.
- Stage's **Studio Room Return** is never eligible for grouping. Existing user instruments and room sends are preserved.

### Direct original plug-in

- Inspector, beneath existing Full Instrument controls → **Open Plug-in** opens the hosted Audio Unit's original UI in one click. It is the **same already-loaded instance**, not another plug-in.
- Full Cinematic Atelier remains available in its own floating editor. No ARIA state modification is performed merely by opening the plug-in.

### On-screen audition keyboard

- Click **Keyboard** beside the transport on wider windows, choose **Window / View → On-Screen MIDI Keyboard**, or press **Shift-Command-K**.
- Clicking notes sends live MIDI note-on/off to the selected playable instrument using the existing Tracktion `AudioTrack::injectLiveMidiMessage` path. Switch between 16 preview channels from the keyboard's selector. No hardware MIDI device, region recording, or plug-in re-creation.
- Notes are released on channel change, instrument selection change, project switch and window close.
- The existing physical keyboard MIDI-follow option and Piano Roll stay independent.

### Room Return

- Only the managed return's **arrangement widgets** are hidden by default. Real return track, native reverberation, AUX bus, Stage sends, mixer wet fader, output, and metering remain unchanged.
- Mixer label **Stage Reverb** is a protected wet FX return, not a normal instrument track.
- View/Mix → **Show Stage Reverb Return in Arrangement** temporarily reveals the managed track for diagnostics; this is a Studio preference, never an edit to project audio.

### Shortcuts

| Command | Action |
|---|---|
| Space | Play/Stop |
| Command-1 | Piano Roll |
| Command-2 | Mixer |
| Command-3 | Orchestral Stage |
| Command-4 | Film Scoring |
| Command-I | Show/Hide Inspector |
| Shift-Command-K | On-Screen MIDI Keyboard |
| Shift-Command-G | Group Selected Tracks |
| Command-S | Save Project |
| Shift-Command-S | Save As |
| Command-Z / Shift-Command-Z | Undo / Redo |
| Command-J | Join MIDI Regions |

The Help → Keyboard Shortcuts reference reflects these entries.

## Testing and release gate

- CMake source patches against Tracktion Engine pinned `c6293127` must find all anchors. Both Linux and Intel macOS compile.
- `StudioUnifiedStage` covers hidden managed return, Mixer presence, room send, folder summing, fold/unfold, row-height persistence and project save/reopen.
- `StudioInterfaceReview` verifies screen-size geometry (and compact/full mixer), `StudioSpatialRouting` verifies audio send; all film, MIDI, synth and sampler suites must remain green.
- Manual ARIA listening: direct original plug-in; open/close audition keyboard; play and release a held note, change selected instrument, and change MIDI channel. Verify no stuck notes and untouched ARIA saved patches.
- Create a Strings folder with 8 tracks, apply group EQ/compressor, check group meter/fader and M/S, save/reopen, and inspect volume/pan consistency. Ensure Stage room still works.
- Test arrangement tracks at 36/50/84/132/240 points at 100% and 125% interface scales; no timeline/ruler overlap, and resize does not destroy the row during pointer drag.

**Known boundaries:** The keyboard auditions but does not record clip MIDI; microphone audio capture remains a separate Stage milestone; groups use Tracktion's native submix rather than a custom DSP engine. Existing projects are not auto-grouped.
