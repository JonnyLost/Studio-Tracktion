# O7 — Harmonic intelligence

Base: O6 4bb78154834fb9d4852c1d75ceaf65c1bd66b27a. No merge into Universal Instrument System.

## Workflow
Open Navigate > Ostinato Engine. Click the harmony button above the matrix to set a starting root, scale and chord; this is stored in the project pattern. Select each row's pitch mode and Apply row. The row MIDI pitch specifies register. The step interval field is interpreted by mode:
- Fixed pitch: unchanged, ideal for percussion.
- Root + intervals: unchanged O4 mode, static row root and semitone offsets.
- Follow played root: semitone offsets from the live or starting root nearest the authored row pitch.
- Scale degrees: signed zero-based degrees in the selected scale; 0 = root, 2 = third, negative degrees descend.
- Chord tones: signed zero-based degrees in the chord's pitch-class set, extending across octaves.
- Arpeggio up, down, bounce: cycle through chord tones within one octave using the cell index plus step interval as phase. Rests retain their phase; repeated strikes within a cell repeat the same pitch.

Scales: major, natural minor, harmonic minor, Dorian, Mixolydian, major pentatonic, chromatic. Starting chord choices: major, minor, diminished, augmented, sus2, sus4, dominant 7, major 7, minor 7, root only; preserve a custom saved chord mask. The starting harmony is used for finite compile/Check Pattern. Old patterns load unchanged through schema 7 migration.

## Live harmony
Use the dedicated enabled, unrouted physical MIDI input and channel configured in O5; disable automatic MIDI follow and remove its track assignment. During stopped-transport audition play a chord on that input. By default the lowest held pitch determines root, and other held pitch classes supply chord tones relative to it. The optional Recognize chord root rule matches exact pitch-class sets for common triads and seventh chords, including inversions. Unrecognized or ambiguous sets retain the lowest played pitch as root; it does not infer omitted chord tones or analyze harmonic context. The root rule is persisted with the pattern (schema 7; schema 6 keeps bass-relative behavior). A 35 ms settling period groups attacks; release-only changes leave the last latched chord intact. New key presses while previous keys are held include the remaining notes. Pedal sustain is not applied to harmony; all notes off clears the key snapshot, not the latched harmony. The selected scale remains fixed until edited. The chord display reflects the live notes; stopping restores the authored starting context. Input does not forward key notes to the underlying instruments via the conducting listener. Do not manually route the same device or simultaneously play destination notes.

Dynamic recipes are stored in the precompiled event list. Future note-ons resolve to the current root/chord; a held note keeps its original sounded pitch and note-off ownership even across changes. Actual pitch collisions or out-of-range live pitches are skipped, with a count on completion. Baseline ambiguous overlapping reference pitches are rejected rather than silently merged across distinct harmonic recipes. Saved articulation keyswitch conflicts stop audition; no unsafe note-off is sent for a rejected switch-pitch attack. Existing rhythmic, probability, density, articulation and finite tempo-map audition behavior remains. No separate instrument is instantiated. Chord changes do not trigger new notes outside the rhythm. Keyboard chord following is active during audition only; hardware selection remains session-local. No recorded chord progression, automation, transport-synchronized live playback, sample-accurate scheduling, or MIDI Print yet.

## Tests
Extended standalone tests for signed scale/chord degrees, all three arpeggio directions, schema 5 migration, static harmony, chord latch, changed-harmony note-off and live pitch collisions, exact inverted chord recognition in all 12 keys, ambiguous-chord fallback and schema 6 migration. The interval field label now reflects the row pitch mode. Full native CI enabled. Native and physical keyboard acceptance are pending unless verified separately. O8 develops variation, humanization and fills.
