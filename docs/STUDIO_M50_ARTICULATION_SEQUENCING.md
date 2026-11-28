# M50 — Named articulation sequencing and performance write

Base: M49 ca5a5bc2c81f2afbe292d38d2907c03a5c358dbf. No branch merged.

## How to use

1. Load the instrument and attach a saved profile with working M47 assignments. Select one MIDI region on that track and open the Piano Roll. A named Articulations lane appears for compatible profiles or existing named events.
2. Click in that lane at a musical position, and choose an assigned articulation or control from the menu. The Piano Roll snap grid determines its source beat. Only the clip's actual MIDI channel is eligible. Put the switch slightly before the first affected musical note (M50 does not shift notes or invent a lead-in).
3. Click near an existing event's start to delete it through the same menu; undo and redo use the existing project undo manager. The named lane shows the stored labels and controller values. It is an event lane, not a duration or continuous-curve editor. For existing supported standard CCs, the existing controller lanes can edit the actual MIDI values.
4. To capture a performance, select exactly one compatible linear MIDI clip, disable cycle, and choose Instruments > Arm Performance Write on Selected MIDI Clip. Press Play (not Record), click articulation buttons or move assigned MIDI knobs/faders in the instrument transport or full view. Stop playback to commit the captured events to that original clip as one undo transaction. Discard Pending Performance Write cancels the staged capture. The status message reports saved/skipped events. This is an overdub of performance events into an existing region, not a new audio or MIDI note take.
5. Start playback through Studio's Play command from the middle of a piece. Studio looks for earlier matching named articulation and control events on compatible hosted tracks; it sends the latest relevant values and a short keyswitch pulse before starting where the engine provides an existing playback context. Exact-start events remain the native sequence's responsibility. Stop cancels a pending pre-play pulse. Starting playback with no eligible previous events uses the normal path.

## Architecture

Named notes and controller events use Tracktion's native MIDI sequence. Metadata stored on the event includes profile ID, element ID, semantic ID, label and kind. No GUI-timer note scheduler is used for normal clip playback. MIDI export contains the underlying note and controller messages; the proprietary names do not survive Standard MIDI File export/import. The project stores them as native ValueTree properties; linear region join preserves these tags. The ordinary Piano Roll note grid, velocity editor, lasso, select all and quantize exclude tagged keyswitch notes. No heuristic hides untagged low notes. Clip-level transposition and third-party editing are not covered by this protection.

Keyswitches use a bounded 1/16-beat note pulse, shortened at clip end. A write rejects overlapping same-pitch notes, wrong channels, unassigned or invalid messages and existing unrelated controller events at the same beat. Continuous CC/pressure use the existing Tracktion shifted 7-bit format and bend uses native 14-bit values. Repeated capture points for the same control within 1/96 beat are coalesced; capture is capped at 16,384 points. Native MIDI sequencing handles normal playback and render/export rather than using live UI injection as its timing authority.

Capture is explicit, in-memory, single-clip and single-profile. It never edits sequence data while transport is running. Track, clip and snapshot identities are revalidated on commit; incompatible or colliding points are skipped. Saving with pending points and changing projects are guarded. Normal Record is blocked while Performance Write is armed. Stop and save before quitting. Capture timestamps come from the transport position on the message thread, not the audio callback; no sample-accurate performance capture claim. Room/volume/pan mixer automation and physical hardware recording use existing facilities, not this new capture mode.

## Scope and limitations

- Only linear, unmuted clips without alternate takes, MPE, clip quantisation or groove are supported for new named writing and chase. One clip channel; cross-channel/compound switches not supported.
- Pre-play chase is best-effort, only on explicit Studio Play. It requires an initialized hosted instrument, matching project attachment, and available playback context; it is not a Tracktion graph-level chase. It does not handle in-flight seeks, cycle wrap, offline bounce starting mid-phrase, or ordinary untagged MIDI override history. No claim of sample-accurate chase. The latest named articulation per channel and latest named control per address are selected; exact-start events are not injected twice.
- ARIA patch identity still relies on user confirmation. Existing MIDI events remain fixed to their original MIDI pitches and controller values when the profile changes; no remapping is performed by M50. M51 will require explicit semantic translation. Native MIDI output remains authoritative even if a profile is removed. UI active-state indicators are not a complete playback monitor.
- Named event editing supports insertion/deletion/undo, not dedicated drag or arbitrary-value editing. Continuous gestures are captured from Studio's live controls during an armed playback pass; simultaneous MIDI note-take recording, hardware performance mapping capture, arbitrary custom controller curves, true engine automation lanes and playback feedback remain future work.

## Verification

Extended the real Studio interface-review fixture to write tagged keyswitch and CC events, inspect values and profile match, reject a same-pitch overlap and remove both events. Existing native sequencing, export and interface tests still apply. CI filters include M50. Native compilation, tests and physical ARIA validation are pending; the source was committed, not executed on the user's Mac.
