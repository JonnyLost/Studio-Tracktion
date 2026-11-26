# M30 — Guided instrument builder

From the native sampler's Play page choose New Instrument. Select audio files
or a specific folder, name the instrument and choose Pitched or Drum /
percussion kit. Choose held notes, one-shot playback, or validated embedded
forward loops. Preview mapping before creating; the existing sampler is
unchanged until the explicit Create instrument action. This replaces the
current sampler instrument, not the track or its mixer. The original audio
files are referenced in place. After creation, play it and use Save to
write a native .studiosampler preset; Build remains available for detailed
mapping and articulation edits. The previous library portrait association
is cleared on replacement; artwork is not automatically inferred.

Pitched mapping recognizes complete scientific note tokens (C4 = MIDI 60)
and supported file root metadata. Conflicts fail. A single file without
recognized root uses the explicit root control; multiple unknown roots
require filenames/metadata or manual mapping. No acoustic pitch detection.
Fill keyboard ranges spreads neighboring roots at their midpoints over MIDI
0–127. Turning it off uses each original key only. Explicit v1-63 or
vel64-127 filename ranges are recognized; unrecognized velocity naming
defaults to full velocity. Overlapping velocity ranges fail, velocity gaps
are reported. Enable Cycle alternate takes to create matched-root,
matched-velocity round-robin sets, in natural filename order, up to the
existing 16 sequence groups. Drum kits assign one recording to each
successive key starting at the chosen first key (default MIDI 36);
the UI defaults to one-shot playback. No automatic choke groups,
articulation inference, velocity normalization, loop discovery or pitch
analysis. Embedded-loop mode requires an existing valid forward loop in
every recording. No synthetic sustain loops are invented.

Preparation runs on a worker, with cancel, strict all-or-nothing mapping,
source identity recheck before commit, original instrument revision guard,
and existing sample pool, streaming, memory and 1024-zone limits.
Specific folder scans are bounded to 20,000 entries; symlinks excluded.
Changing options invalidates the proposal. Preview reports each filename,
root, key and velocity range, alternate group and velocity gaps.
Native preset persistence is unchanged.

## Verification
Added integration checks for pitched note and velocity mapping,
round-robin opt-in and rejection without opt-in, exact-key mapping,
unknown pitch handling, drum assignments, native save/reopen,
missing embedded-loop rejection, cancellation. UI review checks the
New Instrument action, opens and captures the native dialog, closes
it without mutating the sampler. Linux and Intel macOS CI enabled.
These M30 tests and UI screenshots have not yet been verified.

M29 Linux and Intel macOS application builds passed, with 39/40 and
41/42 regression tests passing respectively. Both failed the existing
Unified Stage collapsed-group visibility check; M30 does not claim
to fix that unrelated issue. No dependency additions or merges.
