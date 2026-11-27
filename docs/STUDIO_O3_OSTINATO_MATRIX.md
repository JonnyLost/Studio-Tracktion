# O3 — Native Ostinato Pattern Matrix

Base: O2 46b11bd33da71ca86847731202101c5f44b2508e. O1 and O2 standalone Linux/Intel macOS CI workflows passed. O3 is still separate from the M45–M51 Universal Instrument series; no merge.

## Open and edit

1. Open a project and choose Navigate > Ostinato Engine, or the Studio View options popup. A resizable native floating window opens with a single blank row by default.
2. Add or remove rows using + Row / − Row. Removing a pattern row does not remove an arrangement track. Click a row or cell to select it. The row editor at the bottom exposes an existing instrument-track selector, MIDI pitch (0–127, display C4=60), channel (1–16), mute and solo. Choose Apply row to commit.
3. Follow grid inherits the master grid. Shared span gives that row its own number of divisions. Independent cycle adds numerator, denominator and bars. Apply row validates the settings and refuses shortening that would discard active cells. Set grid updates the shared pattern name, master step count and steps per quarter.
4. Click a matrix cell to toggle it; right-click selects it without changing it. Left/right and Space work when the matrix has focus. The step editor shows active, accent, velocity and gate (per thousand). Apply step commits the selected/numbered cell. The field edits are staged until Apply; selecting a different cell/row reloads its stored values.
5. The display uses the O2 rowBoundary/rowCycle geometry and shows one editable cycle per row against a shared time axis. A vertical line marks the master span; short independent cycles are not visually repeated yet. Very dense or very different-length cycles may require scrolling or selecting the step by number. Rendering is a single painted component, not thousands of individual buttons.
6. Check pattern runs the bounded core compiler and reports generated notes, quarter-note duration, unassigned rows or a missing route error. It does not send MIDI or start transport.
7. Mixer and Stage open Studio's existing workspaces. Effects opens the existing track inspector, not an additional processing engine. Engine and Print MIDI are visible but disabled with explanatory tooltips until live routing and printing are implemented. CC1/CC11 performance curves, row mixer, notation and pattern library are intentionally not fake/inert editable controls.

## Project persistence

One version-2 pattern is stored in the edit root property studioOstinatoPattern. Opening the editor does not create or mutate the property. Each validated change uses the existing project undo manager, in its own transaction. Save/Open retains the data, and undo/redo refresh the visible editor. A malformed existing payload disables editing rather than overwriting it. The window is destroyed when the project switches; callbacks validate the generation and source pattern and reject writes while recording, playing, locked or busy. Destination identities are stored as stable track IDs, not displayed names. Only existing tracks with instruments are offered; a missing previously saved destination is retained for explicit repair. No live track MIDI routing is implemented in O3.

The panel uses Studio's theme colours, interface-scale metrics and look-and-feel, so Deep Black and Graphite remain governed by their established themes. This is a floating editor, not a new docked workspace. User-facing appearance and small-window fit still require MacBook acceptance testing.

## Verification

Added production component review probes for row creation, route/pitch selection, shared-span resize, step properties, refusal to truncate active cells, matrix geometry, read-only protection and row removal. The Studio interface review exercises the real project-bound editor, undo/redo and project reopen persistence. Full Linux and Intel macOS workflows include O3; Intel builds the core and ARIA catalogue test targets. The native O3 app build and GUI tests have not been confirmed as passing. No live sound or installed library verification was possible in this development session.

O4 connects actual arrangement instrument routing and melodic modes; subsequent milestones implement transport synchronization, controller conducting, articulation integration and printing. Keep the window on the isolated branch until integration and acceptance.
