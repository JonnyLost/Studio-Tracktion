# O1 — Core multi-row rhythmic engine

## Base and isolation
Built on integration/studio-0.2-m44-consolidated commit d525295baa696df5414eaaa721a1108aa0888180, not the M45–M51 feature stack. No branch merged; Studio.cpp and the existing instruments, UI and audio graph are unchanged. This is an independent C++20 library/test milestone, not a playable workspace yet.

Archived Swift reference: JonnyLost/Garritan-studio feature/ostinato-engine-v1 at 691e842fde953bb37ea99bc2850331c3dd88bc42, Tests/DAWCoreTests/OstinatoPatternTests.swift. Recovered tests demonstrate beat-relative rendering, editable MIDI, explicit track IDs, mute/solo, JSON round trips and seeded variants/evolution. The original Swift implementation path was not recovered in this inspection; no Swift code was ported verbatim. Later O milestones should reuse those behavioral ideas where appropriate.

## O1 model
- Opens with one blank row, 16 steps, four steps per quarter note. Row count 1–64, common grid 1–256 steps, pitch 0–127, MIDI channel 1–16, opaque stable track identity, user name, mute/solo.
- Each step has enabled/rest, accent (+20 MIDI velocity capped at 127), base velocity 1–127 and gate 1–1000 per mille. Pitch is a row property; multiple rows can share a track for melodic patterns.
- 9,600 integer ticks per quarter; only exact divisors accepted at O1. No accumulated floating-point drift. Shared grid can be resized without silently dropping enabled steps. O2 will add independent divisions and cycle lengths, including exact rational non-divisors such as seven.
- Versioned bounded text payload, quoted names, validated data and transactional decode; round-trip does not depend on JUCE, file permissions or library paths. This is a serialization API, not yet integrated with project save or a permanent preset UI.
- Rendering takes an explicit list of existing track IDs. An active row with a missing destination fails compilation; unassigned rows remain silent and counted. No fallback to a different instrument. Muted rows do not generate events; solo restricts audible rows and mute overrides solo.
- Finite render: 1–256 repeats, maximum 65,536 candidate notes before normalization. Exact same-address simultaneous hits merge using highest velocity and longest gate; overlapping later hits shorten the earlier note before retriggering. Distinct tracks and channels remain separate. Note-off precedes note-on at equal tick.
- Immutable precompiled Render carries sorted notes/events and compact route indices. Cursor consumes contiguous half-open tick blocks [start,end); it allocates no heap memory itself. Caller owns render lifetime, immutable data, and nonallocating sink. Stop and discontinuity release only owned generated notes, with no chase into sustained notes. To consume a note-off exactly at final duration, process a block beyond duration or stop explicitly. No panic CC is sent.
- Runtime routing, audio graph integration, tempo-map to sample offset conversion, hardware CC1/CC11, step probability and articulation tags, live input, loop/seek policy, coexistence with physically played notes, and printing actual MIDI clips are intentionally deferred. The core alone cannot guarantee note ownership against external events on the same destination channel/pitch; the routing adapter must address that.

## Tests
Run the dependency-free executable without building Studio:
```sh
clang++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror tests/OstinatoCoreTests.cpp -o /tmp/studio-ostinato-core
/tmp/studio-ostinato-core
```
CTest target StudioOstinatoCoreTests / StudioOstinatoCore is registered for full builds. Dedicated GitHub Actions matrix runs optimized and ASan/UBSan tests on Linux and Intel macOS. Tests cover defaults, row editing, invalid fields, round trip and transactional failure, bounded compilation, immutable deterministic output, note-off ordering, repeated cycles, missing routes, duplicate hits, mute/solo, block partition equivalence, stop and seek release, stress caps. CI not confirmed at time of writing.

## O2
Add independent per-row rhythmic divisions and polymetric cycle lengths, aligned to the shared musical origin; use exact rational scheduling or an explicitly bounded rational-to-tick rounding policy without cumulative drift. Preserve old patterns, add migration tests and extend collision/partition tests before connecting the O3 matrix UI.
