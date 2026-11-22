# Selected-track MIDI audition (Studio 0.2)

Selecting an instrument track in the arrangement (or its MIDI clip) focuses an available **physical MIDI keyboard** for live audition, without arming the track for recording. This is intended for one-keyboard composing workflows such as an Oxygen 25 and one instrument per track.

- **View → MIDI Keyboard Follows Selected Track** is enabled by default and can be disabled. This preference is stored for Studio, not in the song.
- **Track select** chooses the live audition destination. **I** still accesses manual input routing, and **A** still arms the selected track explicitly for recording. Selection does not engage Replace recording or record arm.
- Studio uses Tracktion Engine's **input destination** plus live monitoring, rather than bypassing the DAW/plugin graph. It preserves MIDI channel filtering, Audio Unit/native instrument hosting, track effects, and normal output routing. It does not directly call instrument audio code.
- The configured default physical MIDI input is preferred. If there is no default, auto-focus only runs when **exactly one** enabled physical MIDI input is available. Virtual MIDI and multi-keyboard projects can continue using manual routing.
- To protect existing projects, Studio does not redirect a MIDI input with **multiple destinations**, any **record-armed destination**, or a currently recording input. In that case, disarm the old target or choose manual routing. When selection changes, previous live-note destinations are retained until all physical keys and observed sustain pedals have been released.
- Auto-focus backs up the input's original single destination and monitor mode and restores them when disabled or the project is unloaded, provided no manual routing/arming has superseded the temporary destination.
- The selected-track indication reports the MIDI follow status; detailed status appears as a tooltip.
- The in-memory focus is not a separately recorded take. Recording still uses Tracktion's established MIDI input, clip and transport APIs.

## Tests and acceptance

Automated policy tests verify selection-to-audition, record separation, armed-input and multi-route preservation, held-note/pedal deferral, and the absence of a playable track. All existing Studio MIDI/transport, hosted Audio Unit, native sampler/synth, save/restore and editor tests should remain green.

A physical Intel MacBook is needed to confirm the final hardware behavior:

1. Load two instruments (e.g. Studio Synth and Studio Sampler) on different tracks; ensure the **A** record-arm buttons are off. Click the Synth track and play a few notes from the MIDI keyboard. Click Sampler and play, without opening Input or arming a track.
2. Hold a note while selecting the other instrument, then release it. The old instrument should receive the note-off, and subsequent notes should go only to the newly selected track. Repeat with a sustain pedal.
3. Arm a track to record, select another track and confirm its armed input is not silently stolen. Disarm to restore automatic focus. Record a normal MIDI take, stop, replay, and verify nothing was overwritten merely by selecting a track.
4. Save and reopen the project, test native instruments and an available Audio Unit, and verify manual multi-input routing is preserved. Disable **MIDI Keyboard Follows Selected Track** and check the original routing is restored.
5. Unplug/replug the keyboard, switch to a different MIDI input, and exercise audio device/transport changes. Listen for stuck notes or audio interruptions.

**Known limits:** Single default/unique physical input only; armed inputs intentionally block focus changes; project state can temporarily show Studio's audition destination until focus is released. MIDI follow is not a replacement for full multitrack input routing or MPE control.
