# M49 — Installed library discovery and mapping

Based on M48 commit 807efbda71599236b1e7700d9ea5eb141e168e81. M47 and M48 Intel macOS and Linux CI were confirmed successful before this work. No merge.

## Workflow

1. Load the intended ARIA instrument and select its track. Open Instruments > Instrument Designer.
2. Click Scan libraries to open the existing ARIA discovery menu. Choose standard locations, a custom root, or Rescan all known roots. Old catalog records are retained and can be rescanned to obtain switch evidence. Scanning uses the existing cancellable worker and checkpoint mechanism; it never modifies samples, opens another instrument or sends MIDI.
3. Search installed patches by library, filename or path. Duplicate display names remain distinct by their exact catalog ID. Choose the physical instrument in the existing catalog, then link the selected local SFZ patch. Link is explicit and will not replace another patch identity or overwrite existing elements. The linked library, patch, source ID, library version and root-file size/time stamp are saved with the profile.
4. The discovered articulation selector displays original SFZ labels, provisional numeric notes, or GPO 5 printed manual hints. Add one or Add all unassigned to create buttons without transmitting anything. Existing labels and manually assigned bindings are retained. Unique exact catalog terms/aliases receive canonical IDs; ambiguous or unknown labels retain a local semantic ID. Different labels sharing a canonical meaning are not collapsed.
5. Review literal note adds/selects a button and fills the M47 assignment editor, but leaves the actual binding unassigned. Confirm the loaded patch, channel and note, save assignment and audition before saving the profile. A previously assigned button is not overwritten by review. Printed note names, conflicts and unknown values require manual entry/MIDI Learn.
6. Save profile, then use the M48 attachment command. Reopening an older saved profile works; optional M49 metadata is backward compatible with schema 2. Existing project snapshots are not modified by scan or global profile edits. Reapply updated profiles explicitly. Linked source change warnings are shown on reattachment; existing user bindings are not silently changed or disabled.

## Evidence and safety

The existing scanner records SFZ labels from bounded readable definitions and literal includes within the selected scan root. M49 adds explicit decimal MIDI 0–127 sw_last / sw_label pairs within a single group, region, global or master block. No inheritance is computed; macro expansion, ranges, named notes, compound switches and proprietary bank formats are not interpreted. Evidence is limited to 1024 pairs and the existing 64-label cap per patch, with existing file, line, recursion and include limits. No additional samples are loaded. The scanner caches candidate source file size/time stamps and checks root and contributing definition freshness when reviewing. Size/time is not a content hash or proof that every dependency is unchanged. Conflicting note/label pairs are suppressed as numeric suggestions, even where sharing might be intentional. Label-only discoveries may include region labels that are not switches. No discovered mapping is enabled automatically. The code cannot infer that a loaded ARIA slot contains the selected patch. No decryption or unsupported file extraction.

GPO 5 documented hints from M45 appear only on matching library and display name, with printed octave names retained separately; even exact name matching cannot establish edition or Standard/Notation variant. GPO 4, Instant Orchestra, Jazz & Big Band, Concert & Marching Band, Harps, World Instruments, and Orchestral Strings can use the same generic SFZ discovery path where readable. Original-player and snapshot entries remain visible but are not treated as a single readable patch. Proprietary Kontakt/VSL formats do not gain new decoding support.

The compatibility report now gives per-library present, labelled, literal-evidence, not-yet-inspected and unavailable counts. These are inventory statistics, not audited playable articulation counts. User profile bindings are not automatically marked audition-confirmed; previous ARIA capture status is separate from articulation verification.

## Tests

Expanded StudioAriaCatalog fixtures cover explicit note pairs in includes, comments, named notes and ranges, same-label and same-note conflicts, checkpoint persistence, stale include evidence, exact semantic aliases and independent printed manual notes, stable patch identity and profile XML round trip, plus preserving a manual override. Existing source-preservation and original-player tests remain. CI branch filters updated. Native M49 compilation and test execution were not yet confirmed at the time of writing; local Garritan inventory is not accessible in the GitHub development session.

## Remaining work

Actual per-library coverage will be known only after scanning the user's Mac. No complete vendor patch database, automatic AU slot introspection, controller opcode mapping, full SFZ interpreter, verified per-binding audition registry, compound sequences, or cross-library translation. M50 adds articulation sequencing/chase and automation; M51 handles semantic translation.
