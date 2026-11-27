# O9 — Pattern library and preset management

Based on O8; no merge to the consolidated Studio branch. The O8 duplicate regression-test variable found by CI is corrected on this branch.

## Library workflow
Open Navigate > Ostinato Engine > Library. Factory patterns are grouped by instrument family or musical use. Select a preset to read its description and confirm replacement. The new pattern is committed through the existing project undo transaction; no arrangement tracks, plugins, or MIDI clips are created. Studio Undo restores the previous matrix. Save the Studio project to retain the new working pattern.

Choose Save portable pattern as to save a named .studioostinato file; the default destination is Studio/Ostinato Patterns under the OS user application data directory (on macOS typically ~/Library/Application Support/Studio/Ostinato Patterns). Existing file overwrite requires the native chooser's confirmation. Files use the existing versioned pattern format and are replaced using a temporary file. Open user pattern / import browses that folder or another location, so patterns can be exchanged or backed up. Show user library folder opens Finder (or the platform file manager), where files can be searched, organized in folders, renamed, copied or deleted. The factory catalog is read-only. There is no new dependency, online account, sample asset or commercial library.

Portable files keep row names, MIDI channel, pitch, rhythm, independent cycles, step dynamics, probability, gate, ties, ratchets, harmony, variation sequence, fills, humanization and density. Project-local track IDs and articulation mapping IDs are intentionally stripped both on export and import, even if a matching ID exists in the receiving project. Unassigned roles are visibly named. Rename selected instrument role is undoable; default row names can inherit the source track display name as a portable hint. Assign each row to the intended instrument track and reselect any desired articulation from that track's own mappings. Percussion note numbers are example pitches and must be checked against your actual patch. The original working pattern retains its routing and articulation assignments on export. Import accepts earlier pattern schemas, rejects malformed UTF-8 and files over 4 MiB, and leaves the current matrix unchanged on error. Loading is disabled while auditioning or the project is locked. No automatic instrument loading, routing, or guessed keyswitches.

## Factory catalog (16 original patterns)
| Family | Presets |
|---|---|
| Strings | String motor; Pizzicato clock; Triplet pursuit; Tension weave |
| Brass | Heroic calls; Syncopated stabs |
| Woodwinds | Woodwind flutter; Pastoral six-eight |
| Percussion | Cinematic march; Heavy drum dialogue; Three against four |
| Harp & keys | Harp shimmer; Prepared pulse |
| Odd meters | Five-four pursuit; Seven-eight engine |
| Ensemble | Orchestral escalation |

Factory patterns are original musical templates, not transcriptions of film scores. Audition remains the O7/O8 finite stopped-transport system; live switching and arrangement playback are not introduced. O10 is MIDI Print.

## Verification
Dependency-free tests check all 16 presets for valid and unique metadata, schema roundtrip, deterministic eight-cycle render after explicit synthetic routing, silence without routing, rejection of malformed and oversized imports, legacy migration and preservation of the source pattern. Native editor checks cover an undo-transaction-compatible preset replacement and disk roundtrip using an isolated temporary file, never the user's actual library. Full native CI and physical MacBook acceptance must be verified separately. The user library browser uses the native file chooser and file manager rather than an in-app searchable tag database; the factory browser is grouped by category.
