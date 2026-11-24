# Studio 0.2 — Integrated Alpha (Codex consolidation brief)

**Product-owner authorization (2026-11-23):** Consolidate the individually tested Studio 0.2 workflows into one launchable Mac application before any additional feature edits. **Do not destroy, force-push, delete or silently overwrite existing feature branches.** This document is the integration contract; it does **not** certify that branches are merged or the app is built.

## Authoritative sources and branch plan

Repository: `JonnyLost/Studio-Tracktion`. This integration branch starts at the owner's Mac-tested `feature/studio-inspector-performance-m5` commit `2bbde74a48087673c7d4a556d094f703012f3698`. That branch **already contains** the Studio 0.2 core DAW sequence through M14 and all sequential Cinematic Atelier milestones M1–M5 (open stacked PRs #33, #38, #39, #40, #42, #43). Never independently re-merge those ancestors.

The separate Film Scoring stack starts from the M7 baseline and culminates in `feature/studio-film-scoring-sync-stems-m5` commit `6f7fab6513526b16949f3c673d9485862672a7d2` (PRs #34, #35, #36, #37, #41). Its portable CI passes. **Integrate the film tip once** into the integration branch via a traceable merge, resolving overlapping `Studio.cpp`, `CMakeLists.txt`, transport, timeline, export, project-save and Inspector UI changes *semantically*. Keep working functionality of both stacks; do not choose ours/theirs wholesale.

The repository default `main` is behind these draft stacks. Do not make it the integration work branch and do not mass-merge each draft PR into `main`. Once combined, fully verified, and owner-approved, open a single clearly labeled final PR to the agreed release target. Preserve all prior commits/branches.

## Requested release experience

- A **self-contained, normal Finder-launchable `Studio.app`** for the owner's Intel MacBook (macOS 14+, x86_64). User should be able to double-click the app from Applications or a chosen folder; no Terminal commands or Xcode needed for ordinary launching. App must retain the `Studio` name and stable bundle identifier/project data paths.
- Use the **approved Studio app icon** from the earlier design task, not a newly invented brand or “JWC Studio.” Search approved project assets / earlier supplied icon before building; **icon source is not currently present in this repository tree**. If it cannot be recovered, explicitly report the missing 1024×1024 master asset; do not silently substitute unrelated artwork. Build native `.icns` / CMake resources from the approved asset, ensure Finder, Dock and Cmd-Tab show it.
- Produce a zipped ready-to-launch Debug or Release `Studio.app` as a named **Intel macOS CI artifact**; deliver repeatable CMake/build/package steps. Sign ad hoc only if appropriate for private local use; do not imply Apple notarization. Include an ordinary “copy to Applications; double-click” guide. Preserve instrument AU discovery, Garritan library paths, save protection and existing app data.
- Maintain the user-approved **orchestral Atelier Inspector** on the right as a real reserved column; it may scroll. Keep art, verified articulations, CC controls, true track Volume/Pan, native Stage room send, and Full Instrument floating editor in sync. Lower workspace remains available for Piano Roll, full Mixer, Stage, film tools, and full Studio Synth/Sampler editors. No Inspector overlap.
- Film Scoring includes frame-aware picture monitoring, SMPTE, cue/hit-point editing, sync, cue-range WAV and family stem export, while preserving transport and existing output modes.

## Must-pass gates (no exceptions for local-only project)

1. Fresh checkout and clean Intel/macOS CMake configure + Debug Studio build and full `ctest` on the combined tip; **no known failing tests**, no linker errors, no mis-linked new CPP sources. Linux portable tests, sampler/synth/Stage and film-core tests also pass.
2. **Known CI debt:** latest inspected Linux run on the M5 Inspector tip built but failed **`StudioInterfaceReview`** with `split arrangement and Mixer`. Reconcile test expectations against the **intentional reserved Inspector column**, then verify true Mixer behavior; never just disable, delete or skip the check.
3. GUI screenshot review at 1536×960 and 1280×800 at normal interface scale: arrangement, ruler, clips, Inspector and lower workspace do not overlap; Inspector close restores width; Piano Roll and Mixer remain usable; both full floating Atelier and Stage are accessible. Film picture viewport must use the same available layout.
4. Confirm UI consistency on track switches between hosted ARIA AUs, native Studio Synth, Studio Sampler and Audio. Native editors should not migrate to the right Inspector.
5. Playback, start/stop/seek/loop, recording, ARIA state-recovery save guard, project save/reopen and crash-free reopening; no accidental duplicated instrument instances or changed MIDI channels.
6. Room routing: Stage audio remains **explicitly opt-in**. The virtual microphone mixer is not yet an audible capture system; do not present it as functional capture. Do not confuse MIDI CC91 with native room sends, or MIDI CC7 with Tracktion's fader. On/off, bypass and room return must remain correct.
7. Verify Film Scoring video import/monitor, SMPTE timing, hit points, cue edits, picture resync, exports and family stems; validate no cue metadata loss in undo/recovery saves and no inadvertent changes to regular MIDI/WAV exports.
8. Produce `Studio.app` with embedded `Info.plist`, bundled icon resource, runnable `Contents/MacOS/Studio` of `x86_64` architecture, no missing runtime library references, and downloadable ZIP. Verify a Finder double-click smoke test on actual macOS. Artifact should be easy for a non-programmer to use.
9. Review licensing/asset provenance. User's personal Garritan/ARIA files remain external assets; **do not bundle proprietary libraries** into the app or public CI artifacts.

## Exit criteria and deliverables

One integration branch with history, clear conflict-resolution commits, a verified draft integration PR, all relevant CI evidence/screenshots, a release manifest listing included milestones and intentionally deferred items, an Intel Mac application ZIP with app icon, and a short launch/install/readme. Report any missing files and blockers rather than claiming success prematurely.

## Explicitly **not** part of this consolidation

Do not begin new Ostinato Engine work, rewrite the sampler/synth, implement microphone DSP, import Garritan library files, expand instrument factory sound sets, redesign unrelated UI, or merge the old Swift `Garritan-studio` repository. Those are next-phase work after stable consolidation.
