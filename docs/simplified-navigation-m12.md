# Studio 0.2 — Composer-first navigation cleanup (M12)

## Purpose

Studio's left rail is reserved for **distinct composing workspaces**. Do not require the composer to search a redundant More menu for tools that already belong to another part of the DAW. This keeps the interface usable at 1024×640 and 1280×800 on an Intel MacBook.

### Left rail

The six active workspaces now appear in this order:

1. **Instruments** — instrument browser; the selected instrument's Detailed Editor continues to open from its instrument/performance panel.
2. **Piano Roll** — note and MIDI editing.
3. **Articulations** — track-following orchestral articulation mapping.
4. **Stage** — orchestral placement and stage audio.
5. **Mixer** — mix channels and master.
6. **Channel** — the current selected-track channel controls, temporarily retained until the separate right-side Inspector redesign is implemented.

There is no **More** button. The remaining vertical space is deliberately empty. It can hold real future composing workspaces such as **Ostinato** and **Picture Scoring** after their engines, project integration and screens exist. There are no inert/placeholder icons.

### Where the removed More items went

| Former More item | Current location |
|---|---|
| Rooms | Open **Stage**, then **Rooms** in the workspace header |
| Virtual Microphones | Open **Stage**, then **Microphones** in the workspace header |
| Arrangement Reference | Open **Stage**, then **Arrangement Reference** in its toolbar |
| MIDI Monitor | **View → Diagnostics → MIDI Monitor** |
| Resource Inspector | **View → Diagnostics → Resource Inspector** |
| Instrument Editor | Select an instrument, then **Detailed Editor**; floating/docking remains supported |

The **Project** menu no longer repeats MIDI Monitor. No underlying audio, MIDI, Stage, sampler, synth, plug-in or project data has been removed. Rooms, Microphones, and Arrangement Reference continue to highlight **Stage** in the rail; the instrument editor highlights **Instruments**. The original Channel workspace is still available until the right-side Inspector is delivered.

### Compact-screen behavior

Stage's contextual Rooms and Microphones controls remain in the workspace header when space permits, and in the existing dock header menu at narrow widths. Selecting Stage from a subview returns to the Stage itself. The rail remains scrollable at extreme interface scale/text settings. Neither rail blanks nor screen-space filler should steal width from the arrangement.

### Automated and MacBook acceptance

The native interface review now tests:
- Exactly six rail destinations (no duplicate More entry).
- Stage to Rooms / Microphones navigation and active Stage highlighting.
- Availability of the macOS Arrangement Reference from Stage.
- Instrument Editor highlighting Instruments.
- Accessibility of diagnostics and correct workspace titles.
- Existing 45 UI scale/size combinations, dock layouts and arrangement review images.

Run full Linux and Intel macOS CI, including native seating tests, composer MIDI and transport, sampler, synth, Stage routing and appearance checks. Capture genuine Studio screenshots at **1280×800** and **1024×640**, plus a narrow-width Stage dock menu and View → Diagnostics if possible. Verify with the physical MacBook before merging.

**Scope boundaries:** Does not implement the Ostinato Engine or Picture Scoring; they are future functional work, not simple icons. Does not redesign the Mixer, Channel workspace, transport or underlying Tracktion engine. No merges without the product owner's approval.
