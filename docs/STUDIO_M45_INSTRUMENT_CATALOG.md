# M45 — Universal instrument and articulation catalog

## Scope and baseline

Built on M44 commit `99425f157956ee681f0ffd3e785b912a61077cce`. No merge to M44, main or development. This is discovery data and its native read-only reader; no new UI, MIDI emission, patch switching or library modification.

- 559 musical instrument/ensemble/production entries across 16 categories.
- 433 named musical techniques/gestures (including family-specific distinctions), 61 control concepts.
- 168 exact existing VSL seating reference IDs and 242 icon enum tokens retained independently.
- 6 GPO 5 documented patch hints, 44 printed keyswitch hints, none enabled or auditioned. No claim that the master catalog enumerates all installed patches.

| Category | Entries |
|---|---:|
| Bowed strings | 47 |
| Flutes and edge-blown winds | 42 |
| Double reeds | 29 |
| Single reeds | 30 |
| Brass and lip-reed winds | 50 |
| Free reeds and pipes | 23 |
| Harps and lyres | 14 |
| Guitars and fretted strings | 59 |
| Zithers and dulcimers | 23 |
| Drums and membranophones | 59 |
| Metal and resonant percussion | 44 |
| Wooden and shaker percussion | 48 |
| Keyboards and organs | 25 |
| Voices and choirs | 23 |
| Electronic and production instruments | 26 |
| Multi-instrument ensembles | 17 |

## Data and native API

- `assets/instrument-designer/catalog-v1.json`: versioned, original curated discovery vocabulary. Every entry has an ID, family, alias list and ranked candidate groups. Family suggestions are intentionally broad and require instrument-specific review; they must never be treated as supported techniques.
- `UniversalInstrumentCatalog.h`: transactional JSON load; unique-ID, foreign-key and discovery-hint validation; case-insensitive search and candidate lookup. Load once off the audio thread. The CMake binary data target packages the source for the future designer and the native test.
- `tests/UniversalInstrumentCatalogTests.cpp`: embedded resource, counts, alias, prioritized flute suggestion, unknown ID, malformed schema and duplicate-ID checks. Run with CTest target StudioUniversalInstrumentCatalog; compilation and execution require the normal JUCE/Tracktion checkout.

IDs are durable; labels and aliases can evolve. Keep instrument identity separate from seating ID, icon token, library patch ID and plug-in instance. Do not rename existing IDs or auto-match ambiguous aliases. A candidate list is not a physical feasibility claim: e.g. harps differ in pedal mechanics, drums differ by stroke, reed families differ in extended techniques. Future edits should refine each instrument with independently checked candidate exclusions and additions.

## Sources and evidence

Public sources establish terminology and representative cases, not exhaustive individual verification. The catalog uses original short labels rather than copying manuals. Source links are in the JSON and include Garritan GPO 5 directory and controls, Garritan manual index, VSL Special Edition documentation, the Orchestra User's Manual, Timbre and Orchestration Resource, MusicBrainz instrument list and the MIDI Association orchestral articulation profile. VSL sources cover presets and named articulation groups; the exact 168 seating references come from the repository. Other Garritan manuals are coverage targets, not claimed fully reviewed or transcribed.

- [gpo](https://usermanuals.garritan.com/GPO5/Content/directory.htm)
- [gpoControls](https://usermanuals.garritan.com/GPO5/Content/controls.htm)
- [garritan](https://www.garritan.com/user-manuals/)
- [vsl1](https://www.vsl.co.at/instruments/special-editions/special-edition-1)
- [vsl2](https://www.vsl.co.at/instruments/special-editions/special-edition-2)
- [vsl4](https://www.vsl.co.at/instruments/special-editions/special-edition-4)
- [vslPresets](https://www.vsl.co.at/tutorials/guides/sibelius-integration/special-editions)
- [hugill](https://andrewhugill.com/OrchestraManual/)
- [timbre](https://timbreandorchestration.org/learn-about-orchestration)
- [world](https://musicbrainz.org/instruments)
- [midi](https://midi.org/midi-ci-profile-for-note-on-selection-of-orchestral-articulation)
- [gpoOverview](https://usermanuals.garritan.com/GPO5/Content/instruments.htm)
- [extended](https://www.cambridge.org/core/books/abs/conducting-for-a-new-era/extended-instrumental-techniques/AA7594CB6AC67CC87CBF1347DD49E28D)
- [vsl3](https://www.vsl.co.at/products/studio/special-edition-vol-3)
- [garritanWorld](https://www.makemusic.com/press-room/brand-assets/garritan/world-instruments/)
- [garritanIO](https://www.makemusic.com/press-room/brand-assets/garritan/garritan-instant-orchestra/)

GPO 5 illustrates why profiles must be patch-specific: Flute Solo KS and Piccolo Solo KS use different printed octaves; Violin 1 Solo KS and Violin 1 KS have different articulation sets even though the physical instrument is the same. Store printed note names as hints only. Confirm the installed patch's actual MIDI number (0–127), channel and variant by audition or MIDI Learn. CC1, CC2 and CC11 can be alternative inputs to the same GPO expressive dimension; do not send competing controller streams. CC64 can mean legato, damper, damping or bow direction depending on patch. Preserve source qualifiers (e.g. muted tremolo vs unmuted) when designing the profile.

## Local library reconciliation (required before claiming complete Garritan coverage)

M44's `studio::aria::Catalog` stores patch ID, library, version, family, source, discovered labels, MIDI range, health and registered AU state. Discovery is user-initiated and local; the repository has no personal library files. M45 does not invent patch IDs. In the next integration:
1. Load existing ARIA catalog from its local directory; retain every patch ID, even if unavailable.
2. Treat discovered SFZ region/switch labels as hints, not proof of keyswitch identity. Keep unknown labels and unresolved instrument matches. Inspect only readable authorized definitions; no decryption or source modification.
3. Reconcile Standard vs Notation, solo vs ensemble, mute and player variants, preset version, plug-in identity and MIDI slot/channel. Require exact local identity before enabling a hint. Keep original state capture isolated.
4. Provide manual assignment and MIDI Learn, with numeric MIDI note and clear octave convention, ordered messages, optional note-off, latching/momentary behavior and explicit channel.
5. Preserve user overrides and project snapshots, allow export/import, report changed or missing patches. Never automatically overwrite an auditioned binding.
6. Produce per-library counts: scanned, unresolved, documented, assigned, auditioned, missing. A manual patch may be fully mapped even without readable source metadata.

## Plane sketch — interface contract for later milestones

Owner's sketch labels a Composer View with articulation buttons along the bottom and a horizontal Universal Instrument View below it. The latter places instrument artwork and name left; an assignable articulation matrix next; available controls and two drawn vertical faders next; a narrow utility-button column and collapsible channel/instrument information at right. The handwritten utility labels appear to reference mixer, room, sampler and ostinato; exact choices/order remain to confirm. Preserve the compact transport and full editor as two presentations of one profile. Use approved simple vector artwork; no painterly art in this milestone. Full view can expose controls omitted from the transport. Universal audio controls target the mixer, not MIDI CC. Layout must obey Studio's theme and text-size settings.

## Deferred work

M46 builds the actual Instrument Designer and reusable profile persistence, M47 assignment and learning, M48 synchronized rendering in transport/full editor, M49 local inventory reconciliation, M50 semantic articulation sequencing, M51 cross-library translation. Profile schema contract in JSON describes identity, message kinds, trigger modes and verification levels, but is not yet a serialized profile implementation. Future UI must keep unassigned suggestions disabled and distinguish documented, discovered, assigned and auditioned evidence.

## Validation and remaining limits

Repository data was re-read after committing and checked for unique IDs, group and family references, disabled hints, valid printed note syntax and control references. The native CTest is registered but has not been run in this session; inspect GitHub Actions and perform Intel Mac verification before integrating. No local ARIA access, no exhaustive patch-by-patch audit, no rendered audio claims. This milestone is a foundation, not a final encyclopedic instrument taxonomy.
