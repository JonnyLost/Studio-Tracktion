# Studio M26 — sampler performance and stabilization

## Capacity and real-time dispatch
Native mappings now accept up to 1024 regions (previously 128), and
polyphony is selectable up to 128 voices (previously 64). Default
polyphony remains 32. The 16 articulation and 16 round-robin group
limits are unchanged. Native preset XML size guard is 8 MiB, and
existing version 4 preset structure is retained.

Each published Bank builds a key-to-region index on the writer thread.
Audio note dispatch walks candidate regions for the key, evaluates
eligibility once and retains ordering for round-robin choices. Group
lookup and solo status are prepared per render bank. No allocation,
locks or I/O were added to live note dispatch/render. Render traversal
is bounded by the instrument's selected voice limit. Existing
release-first/oldest voice stealing remains.

## Sample memory
A process-wide weak-reference sample registry keys by full path,
source byte length and modification time. Separate sampler instances
can share immutable decoded audio and the corresponding bounded
streaming cache. Concurrent sample creation is serialized on control/
import threads, not the callback. Expired entries are pruned on access.
The tracked PCM/cache ceiling is 768 MiB across the registry, with
the existing 256 MiB per-instrument budget retained. This is not a
total process memory cap; decoder overhead, temporary storage, plugin
instances, graph buffers, UI, and transient worker-held caches are
additional. Same contents at different paths are not deduplicated.
Per-track resource totals still describe each track's referenced
samples, so summing those totals double-counts shared data.

Streaming page storage is sized to the actual number of remaining
source pages up to the existing six-page maximum. The worker and
page-pinning protocol remain unchanged. This reduces waste for short
streams; it does not dynamically retune the cache during playback.
Shared long recordings with divergent playback positions can compete
for cache pages; actual SSD workload benchmarks are still needed.

## Regression
StudioSamplerPerformance tests concurrent duplicate acquisition,
budget refusal, source version separation, weak ownership, cache
capacity, indexed vs unindexed output parity for 1024 mapped regions
and 128 active voices, and a synthetic 32-engine rendering workload.
Timing is informational, not a real MacBook performance benchmark.
StudioSamplerIntegration additionally tests independent proposals
referencing the same real streamed WAV share the sample/cache object.
Linux ASan/UBSan and Intel macOS checks are configured. Test execution
and full application build not yet verified at this checkpoint.

M25 Linux build failed at two ambiguous 64-bit XmlElement::setAttribute
calls in StudioLibrary.h. M26 writes these values as decimal strings
matching the existing read path. No earlier branch modified.
No branches merged.

## Acceptance
Build and run full CTest, then load real streamed SFZ/EXS instruments,
compare note/velocity/RR/release behavior against prior versions,
test saved/reopened projects, export offline, and test progressively
larger orchestral sessions at the user's actual device sample rate/
buffer size. Record callback load, underruns, resident memory and
external SSD behavior. Test repeated samples shared by several tracks
and unrelated recordings under memory pressure. Prioritize correctness
and stable playback over further format support. M18R–M26 remain
unmerged pending acceptance. No claim of verified 32-track DAW playback,
unlimited sample memory, or complete format support.
