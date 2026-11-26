# Apple DLSMusicDevice — General MIDI library

This feature addresses the difference between a generic Apple Audio Unit plug-in
editor and **128 directly selectable GM instruments**. The names are the GM1
standard program map. Actual playback still comes from the installed Apple
DLSMusicDevice Audio Unit and its selected sound bank.

## In Studio

1. Open **Instruments** in Studio's left rail.
2. At the top of the Instrument Browser, choose **Apple Sounds**.
3. Select the **Apple GM Sounds** family, then a type such as **Pianos**,
   **Orchestral Strings**, **Brass**, **Reeds**, **Flutes & Pipes** or **Percussion**.
4. Select an instrument, e.g. `001 Acoustic Grand Piano`,
   `041 Violin` or `057 Trumpet`.
5. Use **Load GM Sound** on an available instrument track, or **Add as New
   Track** to create a track already named after the GM sound.
6. If the selected track already has DLSMusicDevice, **Select GM Sound**
   changes that instrument directly—without inserting an additional AU.
7. Play using your MIDI controller or the on-screen keyboard. The sound runs
   through the existing DLSMusicDevice. Browser-selected instruments initialize all melodic MIDI channels (1–9, 11–16); channel 10 is left alone for drums.
8. Save, close and reopen the Studio project. Its GM program is stored as a
   separate 0-based track instrument property; Studio restores the MIDI
   program after the AU has loaded.

**Apple Sound Bank...** on an existing hosted DLSMusicDevice now navigates
straight to this browsable GM list, rather than showing the AU's often-empty
host preset list. **Open Apple AU** remains for advanced bank setup.

## Program numbering

Library rows use display numbers **1–128**. The event sent to the instrument
uses standard zero-based MIDI Program Change **0–127**. For example:

| Visible | GM1 name | MIDI value |
| --- | --- | --- |
| 001 | Acoustic Grand Piano | 0 |
| 041 | Violin | 40 |
| 057 | Trumpet | 56 |
| 074 | Flute | 73 |
| 128 | Gunshot | 127 |

The 128 melodic GM programs cover chromatic percussion and pitched/percussive
sounds. GM **drum kits on MIDI channel 10** are a separate bank/kit workflow,
not claimed as part of these 128 melodic slots.

## What changes under the hood

- The browser populates 128 named entries only when a real Apple
  DLSMusicDevice AU has been scanned and found.
- Selecting a GM entry inserts the existing Apple AU where required, then
  queues the ordered bank-select 0 and MIDI Program Change messages through
  that track's existing Tracktion/JUCE MIDI processing path, not through a
  UI-thread call to the Audio Unit. Selection is deferred until the AU and
  playback context are present and stable.
- The selected number is persisted in that plug-in's project state and
  re-queued when the project loads after Audio Unit instantiation, and when
  the observed AU instance or playback context changes. The selected value
  is an intent, not proof of audible program application.
- On program selection, the melodic channels release sustain and old notes
  and initialize pitch bend to the exact neutral value (8192). This is a
  one-time initialization; it does not suppress subsequent pitch expression.
- Native Studio track/fader automation, MIDI input, audition keyboard,
  recording and plug-in state safeguards remain in place.
- This feature does **not** convert .exs or create a new sampler, modify
  Apple's bank files, or duplicate the sounds into Studio.

## Mac acceptance checklist

- Verify `DLSMusicDevice` is discovered under **Apple Sounds**. If missing,
  rescan Apple Audio Units in **Plug-ins...**.
- Create an instrument track with `001 Acoustic Grand Piano`; play notes.
- Change the same track to `041 Violin`; confirm the timbre changes.
- Add a new track with `057 Trumpet` and compare simultaneous playback.
- Save, close and reopen; both independent sounds should remain assigned.
- Confirm the Inspector displays the GM name/program and original Apple
  instrument interface remains accessible.
- Test recording/playback and note lighting; user-authored MIDI program
  changes in clips may override the saved default while those clips play.
- Confirm the original Apple sound bank files are untouched.

Hardware selection, available sound banks and actual audio response must be
verified on a Mac. The catalog tests check numbering, completeness, family mapping, and the
ordered MIDI byte sequence for all 128 programs. The macOS-only
`StudioDlsPlayback` CTest renders actual DLS audio using Studio's hosted
Tracktion graph, checks same-instance patch switching, distinct AU instances,
and compares the piano's pitch before and after a second AU is loaded.
A skipped or unrun test does not certify live DLS playback. The manual
hardware MIDI routing and save/reopen checks remain necessary. The current
live-dispatch implementation does not independently guarantee program
initialization in separately created offline render snapshots.
