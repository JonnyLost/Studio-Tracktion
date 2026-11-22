# Studio 0.2 — unified arrangement track headers

The arrangement's per-track controls now live together on the **left side** of each track. The right-hand Tracktion example footer previously displayed **+ S V L**, where the initials were plug-in buttons (including built-in Volume and Level Meter), **not visibility or lock controls**.

## Arrangement layout

- **Track name** occupies the upper line. Click the name or empty track-header area to select the track. A selected clip continues to identify its owner track for the instrument workspace.
- **I** opens manual MIDI/audio input selection. For a single MIDI keyboard, the selected-track audition feature from M12 should work without this step.
- **A** remains the explicit record-arm control. Selection alone never arms a recording.
- **M** mutes the track and **S** solos it.
- **FX** opens the selected track's existing Studio insert-chain menu. It lists actual instruments and effects by name, offers **Add Effect**, and opens native Synth/Sampler or Audio Unit interfaces through the existing hosting and ARIA-protection checks.
- **Right-click a track header** exposes the existing Rename, Move Up/Down, Duplicate (muted), and Delete Track actions. Delete uses the established restrictions and Undo.
- A long track name truncates on compact displays; full name remains accessible through tooltip.
- The Tracktion plug-in objects remain in the song with their full order and processing. Only the right-hand footer UI is removed. The arrangement and timeline ruler reclaim the full freed width.

## CI / physical acceptance

Before accepting, verify native **Intel macOS and Linux** builds and the existing composer, Synth, Sampler, transport, UI appearance, ARIA, spatial routing and project persistence tests. Review actual screenshot captures at **1280×800** and **1024×640** in Split/Expanded/Hidden layouts.

On a disposable project:
1. Create Studio Synth, Sampler, and Audio Unit instrument tracks. Click each left header to select and audition it without record arm, checking the earlier MIDI-focus milestone.
2. Click **FX**; make sure the menu shows the real instrument, select it, then add an Equaliser effect and reopen that effect. Save/reopen and check both instrument and effect retain settings.
3. Right-click the header; rename, duplicate and delete a throwaway track, then Undo. Ensure selected-track focus follows the new selection and no MIDI hangs remain.
4. Mute, solo, arm and manually change input on separate tracks; verify nothing was re-routed during active recording.
5. Compare Piano Roll and timeline ruler alignment. The right side of the timeline must end at the viewport scrollbar, not an invisible 150-pixel insert region.
6. Repeat at 1280×800 and 1024×640 with Large/Extra Large text; no fifth-button overlap, clipping or missing FX menu.
7. Check the Stage, Mixer, and Performance panel still follow selected tracks and remain usable.

**Known limits:** This is a focused track-header change, not the proposed Mixer redesign, right Inspector or transport refactor. The left-header FX menu only adds effects; loading a new instrument still uses the existing Instrument Browser. The custom Tracktion example header is generated in the Studio build directory from the pinned upstream source; this branch does not edit Tracktion Engine itself.

Do not merge without product-owner approval.
