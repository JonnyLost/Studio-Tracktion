# Build Your First Studio Instrument

Allow five minutes with a few short WAV or AIFF recordings. Mono and stereo files are supported. Keep your recordings in a folder beside the instrument you will save.

1. Create a Sampler track in Studio. Open its detailed editor; use Float if you want more room.
2. Drag your recordings onto the sample display, or choose **Load Sample** and select several files or a folder.
3. Choose **Map Automatically**. Names such as `Piano_C4.wav` provide the original note: C4 means MIDI note 60. Names without a reliable note receive provisional notes starting at C3. Studio does not listen to guess pitch.
4. Review the proposed notes and ranges. Choose **Import**, or **Cancel** to choose the other mapping approach. Manual mapping begins with each sample on its root note. After import, use Mapping to refine every range.
5. Click a rectangle to select a sample. Drag its left or right edge to change the range; drag the middle to move it. Shift-drag the middle to set its root. Use the readable root, key and velocity controls for precise adjustments. Command-click another rectangle to add it to the selection. Undo and Redo apply sampler edits.
6. For two velocity layers, duplicate a zone. Set the softer zone's maximum velocity to 63 and the louder zone's minimum velocity to 64. Give both the same key range. Overlapping velocity ranges deliberately play layers together. A nonzero **Round-robin group** number makes matching zones take turns instead.
7. Open **Groups** to create Piano Soft and Piano Loud. Select a zone, select a group, and choose **Move zone here**. Double-click the group name to rename it. **Copy group** copies its zones; **Delete group** moves its zones into a remaining group.
8. Click the keyboard to play the complete mapping. **Audition** plays the selected sample directly, even when the root is outside its assigned key range. Basic adjusts the instrument's envelope and volume.
9. Open **Waveform**. Gold lines mark sample boundaries; blue lines mark the loop. Drag in the top half to adjust loops and the bottom half to adjust sample boundaries. The frame controls provide precise values. Enable Loop and audition to hear it. Zoom and Position let you inspect smaller regions. The original recording is never edited.
10. Double-click the instrument name, then choose **Save**. Studio writes a `.studiosampler` instrument. Close and reopen Studio, create a Sampler track, and choose **Load** to reopen it. The compact Performance panel remains available for composing without the detailed editor.

[Native editor review and capture status](reviews/m10/README.md)

Keep the instrument and its sample subfolder together when moving them. Files below the instrument folder use relative references; files elsewhere retain absolute paths. For unavailable recordings, select the affected zone and choose **Locate Sample**. A shorter replacement may require resetting saved boundaries or disabling its loop first.

Limits: 128 zones, 16 organizational groups, 64 voices, 64 MiB decoded per recording and 256 MiB per instrument. Import reports files it cannot accept. Loading and decoding currently run on the message thread and may briefly pause the interface. Audio rendering performs neither disk access nor decoding.

Deferred: acoustic pitch detection, disk streaming, portable sample-copy export, velocity crossfades, editable loop crossfade, keyswitch groups and articulation switching. Organizational groups do not mute or switch layers. The existing fixed loop smoothing remains in the playback engine. Import review supports accepting or cancelling a proposal; detailed range edits happen after import and can be undone.
