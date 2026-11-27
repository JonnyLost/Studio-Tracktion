# O8 — Variations, fills, humanization and evolution

Based on O7. No merge into the consolidated development branch.

## Workflow
Open Navigate > Ostinato Engine. Click Variations to author a sequence of up to 64 letters A–D; each letter lasts exactly one master grid cycle, not necessarily a bar. The sequence repeats for the chosen finite repeat count. For example AABBACAD produces eight cycles before repeating. To audition one variation, enter its single letter.

Right-click a cell or open Step details. Membership ABCD means the enabled cell plays in all four variations; B means only B; BD means B or D. F is a fill-only cell. A combination such as BF plays in B or on a fill cycle, never twice. Fill every N cycles adds F cells on cycles N, 2N, etc.; zero disables fills. Step membership does not enable a disabled step. All variations share the row routing, timing, pitch mode and individual cell parameters. This first version does not store four independent full copies of the matrix.

Humanization ranges: timing ±0–20% of a cell, velocity ±0–30 MIDI velocity units. The deterministic seed controls two independent integer jitter streams; each cell's ratchets share its jitter. Timing jitter cannot push a cell's first attack across its master-cycle boundary or its independent row-cycle boundary. Existing authored offsets determine the membership cycle before jitter. Sustains and ratchets may continue across master boundaries without forcibly releasing held notes. Independent row cycles retain their absolute phase. Standard collision checks still apply. The density ceiling starts and ends at authored values 0–127 across finite master repeats, allowing optional steps to gradually enter or leave; live CC density additionally filters those notes, rather than overriding the authored ceiling. A single-repeat render uses the starting ceiling.

Check pattern now validates the selected number of repeats, including variation transitions. Audition is finite and uses the O7 stopped-transport route and harmony adapter. These are preplanned transitions, not live A–D triggering; no audio-thread transport scheduling or MIDI print is introduced. Repeating with the same project, seed and repeat count reproduces the same compiled events. To retain project edits, save as usual. Schema 8 adds settings and cell membership; schemas 1–7 default to ABCD membership, sequence A, no fill, no jitter, and full density. Old binaries may not read schema 8: keep the original project or a backup when testing.

## Verification
Standalone regression tests cover A–D, additive fills, density progression, exact master boundaries, independent row phase, deterministic seeded jitter and bounds, invalid settings and schema 7 migration. Existing render size and audition limits remain; compile work is additionally bounded to four million candidate visits. Native UI, real instruments and physical controller acceptance remain to be verified.
