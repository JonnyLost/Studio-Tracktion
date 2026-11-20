# Studio 0.2 interface refinement

Studio uses a compact project header and transport, a left tool rail, the arrangement, and a resizable lower workspace. The dark surface colors and instrument-browser hierarchy follow the approved Garritan Studio reference. The earlier Swift Stage treatment informed the conductor position and curved rows. Studio draws the interface with the existing JUCE graphics API.

- The transport separates Return to Start, Stop, Play, and Record. Play and Record have active states. Space retains the play/stop shortcut, including retaining a take when stopped. Bars and beats, tempo, and time signature stay visible on MacBook layouts. Elapsed time and the direct MIDI Monitor button use spare width. The View menu always provides MIDI Monitor and Resource Inspector.
- The arrangement keeps its musical grid, scroll, zoom, track input and arm controls, and region editing. Blue MIDI regions and teal audio regions carry their names and previews. Selection has its own outline.
- Lower workspaces support Compact, Split, Expanded, and Hide. Studio remembers vertical scroll when switching tools within the same project. The More menu contains working tools. Studio Synth and Studio Sampler are identified as planned in the instrument catalog.
- Mixer strips show track identity, input, instrument, effects, room-send access, balance, peaks, and channel level. The Room button opens the selected Stage seat. The right-hand Master stays visible while track strips scroll. The shared room return uses a distinct purple accent. Channel exposes both input types and opens hosted ARIA directly when assigned.
- The Stage shows traditional curved Concert seating, a narrower Chamber stage, or shallower Scoring rows. Choose a layout and click Auto Arrange to apply it to the chosen scope. Drag to move a seat, right-click overlapping seats to choose one, or use arrow keys to nudge. Shift-drag and Shift-arrow move the selected section. Arrangement and mixer selection follow the seat. The diagram preserves the stored pan and depth coordinates. Stage Audio and the shared send still control the audible placement.
- Rooms labels the active room and calls out unapplied changes beside the draft controls. Pre-delay, Room Check, return level and bypass use the 4R implementation. Virtual Microphones saves placement and mixer configuration. Individual microphone capture is unavailable.

## MacBook visual acceptance

1. At 1024 × 640, 1280 × 800, and a larger desktop size, try all five interface scales with Standard, Large, and Extra Large text. Confirm the transport stays reachable, the arrangement remains usable, and excess controls scroll or appear in a menu. Switch tools and return to a scrolled inspector. Try the divider, Compact, Split, Expanded, and Hide. Float and redock MIDI Monitor and Articulations.
2. With a project copy, open a hosted ARIA patch from Channel, arm an assigned input in Mixer, and record a MIDI or audio take. Stop from the transport and from the keyboard. Drag and trim regions, edit notes and expression lanes, and verify undo and redo.
3. Compare Concert, Chamber, and Scoring with a few orchestral instruments. Drag and nudge a seat, move a section, select overlapping seats, and undo. On a dry ARIA piano, verify front-to-rear ambience, Room Check, pre-delay, preset Apply, bypass, and return metering. Confirm the Room Return fader and the fixed Master during horizontal scroll.
4. Save As and reopen the copy. Check notes, ARIA state, applied room, seat coordinates, and microphone configuration. Confirm that the current appearance settings survive reopening Studio.

The Intel build runs the native interface review alongside the acoustic, hosted routing, MIDI, and spatial state tests. The interface review exercises the actual layout at 45 size and appearance combinations, checks selection, docking and Stage move/undo, and saves/reopens a temporary project. It can render the representative screens as PNGs. A MacBook with ARIA and audio and MIDI hardware is needed for the hands-on checks.

To regenerate the screenshots with a Debug build from this branch:

```bash
STUDIO_INTERFACE_SCREENSHOTS="$HOME/Desktop/Studio-M45-Review" \
  ./build/Studio_artefacts/Debug/Studio.app/Contents/MacOS/Studio --studio-interface-review
```
