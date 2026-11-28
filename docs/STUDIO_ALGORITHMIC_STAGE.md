# Studio algorithmic Stage

Verified base: `35776eb401e1459106f04622c1784a9a95ae4b7e`, the published tip of `feature/studio-native-spatial-p4`.

## Baseline audit and implementation checklist

- [x] Verify P4 history and inspect the Stage workspace, spatial state, hosted routing, seating parser and P1–P4 audio processors.
- [ ] Authoritative physical Stage with explicit, undoable adoption. The legacy Stage currently writes normalized track seats and hosts a stereo balance insert followed by an owned aux send. The standalone Studio Spatial insert already owns smoothed P1/P2 positioning and a P4 first-order image-source renderer with a local room.
- [ ] Share an improved algorithmic late field across managed sources and expose the same core in Add Effect. The current wet Room Return owns an eight-line FDN, room-level decorrelated taps, tracked aux allocation, input/output metering and foreign routing checks.
- [ ] Drive audio from the project microphone bank, currently a persisted placement and mixing interface with no capture graph.
- [ ] Connect explicit Stage placement to the validated Atlas catalog. The existing reference designer and its charts remain a separate workflow. Existing drag/drop, auto arrange and docked/floating views provide the Stage entry points.
- [ ] Run synthetic acoustic tests, hosted regressions, performance and interface checks, then record results here and open a draft pull request against P4.
