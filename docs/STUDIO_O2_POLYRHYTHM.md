# O2 — Independent subdivisions, polyrhythms and polymeter

Base: O1 e43381152450ecfeeb6a1f2a3ce0e0699dad6f4d, whose standalone Intel macOS/Linux CI workflow passed. O2 remains isolated from M45–M51, and no branch is merged.

## Three timing modes
- Follow grid: O1 behavior, all rows inherit the master number of steps and exact steps-per-quarter division.
- Shared span: a row divides the master pattern duration into its own 1–256 equal subdivisions. Four, five and seven steps across the same bar realign at every master boundary. The step count is independent of the master grid.
- Independent cycle: each row has its own meter numerator (1–32), denominator (1,2,4,8,16,32), bars per cycle (1–16) and step count (1–256). All cycles start at the same pattern origin, but repeat without restarting at master boundaries. For instance, one bar each of 5/8 and 7/8 meets after 35 eighth notes, or 17.5 quarter notes. The render duration remains the explicit master span multiplied by the requested repeat count; the last independent cycle may be partial.

Master meter convenience API converts a numerator/denominator and bar count to the existing exact master grid. It rejects a grid that exceeds the step cap, cannot represent an exact integer tick division or would truncate enabled cells. Individual row timing changes also refuse to discard enabled cells. Changing meter changes the musical positions of cells by design, not their stored values.

## Timing and MIDI
The scheduler retains 9600 ticks per quarter. For a row span S and N cells, boundary k is floor(S*k/N) ticks from the cycle origin. The cycle right edge is exactly S. Fractional tick divisions are rounded down by less than one tick at each boundary, with no accumulated phase error; this is not arbitrary-precision rational playback or sample-accurate audio synchronization. Gate length uses the actual integer cell width, and notes are clipped to the finite render end. Active step indices are precollected, and the O1 candidate note cap is enforced before normalization. Simultaneous duplicate notes on the same route/channel/pitch merge; intersecting notes retrigger safely. O1 cursor and stop/discontinuity behavior are unchanged. No audio-thread allocation is introduced.

The same rowCycle/rowBoundary methods are available to the O3 matrix, avoiding a separate visual timing calculation. Pattern data uses schema 2; schema 1 loads with follow-grid rows, preserving old timing and content. O1 readers cannot load the new payload; back up original files if reverting.

## Verification
Extended the existing standalone regression executable with an explicit O1 migration fixture, invalid schema/meter/count checks, 4:5:7 shared subdivisions, all 256 subdivision counts in 7/8, 5/8 against 7/8 independent cycles, grid resize preservation, finite partial cycles, dense output cap, overlapping notes and cursor partition equivalence. Dedicated CI already covers the branch via feature/studio-ostinato-* and runs optimized and sanitizer tests on Linux and Intel macOS. O2 results are not yet confirmed as passing.

## Deferred
No pattern matrix GUI, live transport or tempo-map adapter, live meter changes, swing, ties, MIDI input/CC conducting, project save integration, or actual MIDI clip printing. Meter is row-cycle timing, not a change to the project's time-signature map. The next milestone O3 will implement the native matrix interface from the airplane sketch using these APIs. O4+ connects existing arrangement instruments and live routing.
