# Studio 0.2 M44 consolidation

M44 starts at `186f067` on `feature/studio-theme-isolation-m43`. The draft PR uses M43 as its review base so the diff shows the consolidation work. Main and development are outside the integration target.

- [Draft pull request #93](https://github.com/JonnyLost/Studio-Tracktion/pull/93)
- [M44 integration branch](https://github.com/JonnyLost/Studio-Tracktion/tree/integration/studio-0.2-m44-consolidated)
- [Build and test runs](https://github.com/JonnyLost/Studio-Tracktion/actions?query=branch%3Aintegration%2Fstudio-0.2-m44-consolidated)

## What was integrated

- M43 already contains M39A–M39E and M40–M43, including the seven palettes, scaling, welcome and Environment templates, the cream arrangement ruler, the Deep Black hardware controls and the Graphite Red standard controls. The 242 approved SVG files under `assets/instrument-icons` were verified by count. The arrangement, Piano Roll, instrument editors, Inspector, mixer, stage, seating and picture workspace use their M43 implementation.
- The common foundation also includes the M20–M24 importer branches and their M25–M30 successors. The universal importer merge brings in the earlier Garritan-native discovery work and exposes its library conversion and relink workflow alongside the newer inline Sampler importers. The merge preserves version 4 SFZ mappings and streaming in the existing Sampler.
- The ARIA catalog merge adds user-initiated discovery and registration, with recall through the installed Audio Unit. Existing Apple DLS and General MIDI handling retain their M43 paths. ARIA discovery runs when requested from the browser and the catalog is read from Studio's preferences location.
- The isolated Film Scoring M2 tip has a namespace fix covered by `caffd2c` and subsequent M5 integration. M46 diverges before the M36 Environment templates. Its welcome assets and selection logic are already identical in M43, which adds the musical template picker. M44 retains that picker.
- ARIA recall requires the licensed plug-in, the matching plug-in identifier and version, and the referenced samples. The importers leave library sources in place. See the [universal importer compatibility matrix](universal-instrument-import.md#format-compatibility) and [ARIA workflow](garritan-aria-catalog.md). Compatibility varies by format and revision. The central importer inventories opaque Kontakt, Giga and Akai containers, while native conversion is limited to readable supported mappings and accessible samples. An inventory entry gives no playback assurance. The central reader's embedded mono SoundFont path uses RAM. M43's native streamed sampler and separate inline bank conversion remain available. Validate any protected library in its licensed player.

## Intel Mac installation and updates

The installer requires macOS 14 or later, full Xcode with the Swift toolchain, CMake 3.22 or later, Git, and the pinned Tracktion Engine checkout `c6293127cdc0a8488aea2ae0907f4709ee89c228` with its JUCE submodule initialized at `7ff644f65d7daa1428c34b9738ebb5c449dc9172`.

The following commands create a separate checkout for M44. Choose an unused worktree path if that directory already exists. Set `TRACKTION_ROOT` to your pinned Tracktion checkout. For an update, run `git pull --ff-only` in the M44 worktree, review the new commit, then rerun the installer with that commit.

```bash
cd "$HOME/Developer/Studio-FilmScoring-M1"
git fetch origin
git worktree add --track -b integration/studio-0.2-m44-consolidated \
  "$HOME/Developer/Studio-M44" origin/integration/studio-0.2-m44-consolidated
cd "$HOME/Developer/Studio-M44"
git log -1 --oneline
export TRACKTION_ROOT="$HOME/Developer/tracktion_engine"
scripts/install-studio-macos.sh "$(git rev-parse HEAD)"
```

If the integration branch already exists locally, use its existing checkout. If your Tracktion checkout is on another revision or has local work, use a separate checkout at the pinned revision.

The installer builds Release with two jobs by default. It stages the app, signs it locally, checks the bundle identifier, executable, deployment target, architecture and signature, then moves any previous app into a hidden backup directory next to the installation. It installs in Applications or your per-user Applications folder and opens the installed bundle through Launch Services. The bundle retains `org.studioproject.tracktion`. The script prints the installed and backup paths. If a system copy is unwritable and a per-user copy is installed, use the per-user folder explicitly in Finder.

Once installed, open **Finder → Applications → Studio**. For a per-user installation, press **Shift–Command–G** in Finder and enter `~/Applications`. Double-click **Studio**. You can search for **Studio** in Spotlight, open it from Launchpad where available, or drag the installed app to the Dock. When Studio is running, its Dock menu also offers **Options → Keep in Dock**.

### Install the verified preview from Finder

Download the GitHub Actions artifact and open the outer ZIP, then open the enclosed `Studio-M44-Intel-preview.zip`. In Finder, move `Studio.app` to Applications or to your own `~/Applications` folder. If Finder finds an existing Studio.app, first move the existing bundle to a separate folder so it can be restored. Double-click the installed Studio. If macOS requests approval for this locally signed build, use **System Settings → Privacy & Security → Open Anyway**. [Intel preview artifact](https://github.com/JonnyLost/Studio-Tracktion/actions/runs/45941426971/artifacts/15173577847)

### Restore a previous version

Quit Studio first. In Finder, open the installation directory printed by the installer and move the current Studio.app to a separate folder for safekeeping. Use **Shift–Command–G** to open the exact hidden backup directory printed by the installer, for example `/Applications/.studio-backup.A1B2C3D4`. Move its Studio.app back into the original Applications folder and open it there. A bundle rollback restores the application executable and resources. Projects, preferences, and sample folders stay in their usual locations. Use a copy of any project saved by the newer version when checking older-version compatibility.

### Development build and launch

Run from the M44 worktree with `TRACKTION_ROOT` set. Xcode keeps configurations in separate output directories. Use a new build directory to reproduce a clean Release build.

```bash
cmake -S . -B build/m44-dev -G Xcode \
  -DTRACKTION_ROOT="$TRACKTION_ROOT" \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0
cmake --build build/m44-dev --config Release --parallel 2
ctest --test-dir build/m44-dev -C Release --output-on-failure --no-tests=error
open build/m44-dev/Studio_artefacts/Release/Studio.app
cmake --build build/m44-dev --config Debug --target Studio --parallel 2
open build/m44-dev/Studio_artefacts/Debug/Studio.app
```

## Icon and package status

The approved `Studio_Icon_MinimalPro_Refined.png`, `Studio_Icon_MinimalPro_Refined.icns` and `Studio_Icon_MinimalPro_Refined_Assets.zip` were absent from the accessible attachments, working tree and fetched repository history. The Intel Mac checkout, Desktop and Downloads were inaccessible from the Linux workspace. No substitute artwork was generated. When the approved files are available, preserve the PNG as `assets/Studio.png` and the supplied ICNS as `assets/Studio.icns`, if available. CMake's existing `STUDIO_ICON_FILE` option also accepts the path to the approved 1024-square PNG. JUCE generates `AppIcon.icns` from the PNG. The bundle verifier checks the plist icon field and resource when the PNG is supplied. Confirm its appearance in Finder, Dock, Spotlight, Launchpad where available, Command–Tab and the About window on the Mac.

## Automated and manual verification

Build and test details are tracked in the [M44 Actions runs](https://github.com/JonnyLost/Studio-Tracktion/actions?query=branch%3Aintegration%2Fstudio-0.2-m44-consolidated). The Intel Release job exercises the executable through the repository's CTest suite. It also verifies and launches a copy extracted outside the checkout before uploading the ZIP. A package without the approved icon is a preview. This workspace runs Linux and cannot install to the Intel Mac.

### Linux Release results

- Commit `2589191` passed all 43 CTest cases locally under Xvfb and on GitHub's Linux push and draft PR runs. The revised hosted recording fixture also passed five consecutive local repetitions. Results include the SFZ, SoundFont/DLS and EXS fixtures, synthetic Garritan and universal conversion cases, ARIA catalog fixtures, sampler and synth audio integration, cue and stem delivery, and window-size and theme checks. [Linux push run](https://github.com/JonnyLost/Studio-Tracktion/actions/runs/45941426992)
- The M43 baseline adaptive test expected an earlier fixed 425-point Atelier dock. The updated fixture exercises the approved default Split height, divider lock, reachable viewport content and floating editor. Appearance checks also compile on the Intel SDK after qualifying the theme's `signal` role.
- A copy of the Linux executable started from `/tmp` in Xvfb. In one smoke run, its first window appeared in 118 ms, with 45.8 MiB RSS and 22% of one core averaged over the next three seconds. The local sampler benchmark had zero misses across six configurations. An isolated synth matrix run recorded 11 individual block deadline misses across 60 configurations. These measurements describe the shared Linux environment. Startup and sustained performance on the Intel Mac remain manual checks.

### Intel macOS results

The Intel Release for `2589191` passed all 45 CTest cases and the native Swift seating check. The Apple DLS test rendered different patches and checked independent tuning in two AU instances. The updated transport fixture retained the synthetic MIDI take after Stop. The package step checked the x86_64 Mach-O, macOS 14 deployment target, bundle identifier, executable, microphone usage string and signature. Launch Services opened the extracted app outside the source tree. [Intel Release run](https://github.com/JonnyLost/Studio-Tracktion/actions/runs/45941426971)

The draft PR's Intel Debug build compiled successfully and passed 44 of 45 CTest cases. That runner still failed the hosted MIDI take retention assertion after Stop. Physical MIDI and audio recording, personal project and library acceptance, and installation on the owner's Mac remain manual checks. [Intel Debug run](https://github.com/JonnyLost/Studio-Tracktion/actions/runs/45941431792)

### Manual Mac acceptance

For manual acceptance, use both 1280×800 and 1024×640, including the largest global text size.

- [ ] Launch the installed app from Finder, Spotlight and the Dock. Check the icon and startup time. Open an extracted copy from outside the source tree.
- [ ] Verify Welcome, New Project, Open, recent projects, Continue, Environment selection, restoration, and recovery. Open a copy of an existing project.
- [ ] Use the physical audio input and headphones to record audio. Arm a physical MIDI input, record and play back, edit and trim clips, split regions, and edit the Piano Roll and controller lanes.
- [ ] Load multiple instruments, and verify independent tuning, pitch bend, stereo panning, factory and user synth presets, GM patch playback, streamed sampler playback, and articulation transitions.
- [ ] Test supported library conversions on copies or read-only source folders. Audition a locally installed Garritan ARIA patch, register and recall it, save the project and reopen it. Remove and reload an Audio Unit.
- [ ] Check the mixer, effects, Room Return, Stage, seating presets/custom seats and resource meters. Observe startup and idle CPU and memory, then play and bounce a representative scoring project to look for dropouts or memory pressure.
- [ ] Check seven themes, Deep Black controls, Graphite Red controls, cream ruler contrast, instrument vectors, Inspector and docked/floating tools at both sizes.
- [ ] Verify docked/floating picture, SMPTE offsets, hits, cue sheet, tempo map and synchronized stems against representative picture and audio.

## Branch and pull request inventory

The table records the remote heads at inspection and whether each tip is in M43 or was integrated during M44. PR numbers match the open PR head names at inspection. Duplicate references resolve to the same source tip. See the notes above for the two divergent tips.

| Remote branch | Tip | Open PR | Disposition |
| --- | --- | --- | --- |
| `development` | `217b19b` | — | In M43 |
| `feature/midi-piano-roll-m2` | `3f469be` | [1](https://github.com/JonnyLost/Studio-Tracktion/pull/1) | In M43 |
| `feature/midi-region-editing-m3` | `d797a9b` | [2](https://github.com/JonnyLost/Studio-Tracktion/pull/2) | In M43 |
| `feature/region-drag-trim-m3b` | `6a006bd` | [3](https://github.com/JonnyLost/Studio-Tracktion/pull/3) | In M43 |
| `feature/studio-acoustic-engine-m4r` | `3d5ec3a` | [9](https://github.com/JonnyLost/Studio-Tracktion/pull/9) | In M43 |
| `feature/studio-adaptive-workspace-m9` | `b90626c` | [20](https://github.com/JonnyLost/Studio-Tracktion/pull/20) | In M43 |
| `feature/studio-apple-sounds-effects-browser-m17` | `1764914` | [62](https://github.com/JonnyLost/Studio-Tracktion/pull/62) | In M43 |
| `feature/studio-aria-catalog-60` | `72fa6bb` | [61](https://github.com/JonnyLost/Studio-Tracktion/pull/61) | Merged into M44 |
| `feature/studio-atelier-compact-header-artwork` | `cb36dca` | [39](https://github.com/JonnyLost/Studio-Tracktion/pull/39) | In M43 |
| `feature/studio-atelier-fixed-dock-sizing` | `7b2c4da` | [38](https://github.com/JonnyLost/Studio-Tracktion/pull/38) | In M43 |
| `feature/studio-atelier-full-experience-m3` | `d1071e7` | [40](https://github.com/JonnyLost/Studio-Tracktion/pull/40) | In M43 |
| `feature/studio-atelier-keyboard-docking-m12` | `559bd72` | [55](https://github.com/JonnyLost/Studio-Tracktion/pull/55) | In M43 |
| `feature/studio-atelier-midi-cc-knobs-m4` | `a187a7c` | [42](https://github.com/JonnyLost/Studio-Tracktion/pull/42) | In M43 |
| `feature/studio-atelier-piano-case-m14` | `ef886d3` | [57](https://github.com/JonnyLost/Studio-Tracktion/pull/57) | In M43 |
| `feature/studio-atelier-slate-inspector-m16` | `fa34451` | [59](https://github.com/JonnyLost/Studio-Tracktion/pull/59) | In M43 |
| `feature/studio-cinematic-atelier-m1` | `77d3818` | [33](https://github.com/JonnyLost/Studio-Tracktion/pull/33) | In M43 |
| `feature/studio-cinematic-atelier-m35` | `864f2b8` | [80](https://github.com/JonnyLost/Studio-Tracktion/pull/80) | In M43 |
| `feature/studio-cinematic-keyboard-console-m11` | `3efff80` | [54](https://github.com/JonnyLost/Studio-Tracktion/pull/54) | In M43 |
| `feature/studio-composer-acceptance-m39e` | `977d751` | [88](https://github.com/JonnyLost/Studio-Tracktion/pull/88) | In M43 |
| `feature/studio-composer-refinement-m39a` | `d9ddb8a` | [84](https://github.com/JonnyLost/Studio-Tracktion/pull/84) | In M43 |
| `feature/studio-composer-ui-m12-quickwins` | `1c9aa4f` | [24](https://github.com/JonnyLost/Studio-Tracktion/pull/24) | In M43 |
| `feature/studio-composer-workflow-m8` | `e7300ff` | [19](https://github.com/JonnyLost/Studio-Tracktion/pull/19) | In M43 |
| `feature/studio-composition-workflow-m8` | `0a5aec5` | [51](https://github.com/JonnyLost/Studio-Tracktion/pull/51) | In M43 |
| `feature/studio-contextual-help-m31` | `2218014` | [76](https://github.com/JonnyLost/Studio-Tracktion/pull/76) | In M43 |
| `feature/studio-cream-ruler-m42` | `337aa15` | [91](https://github.com/JonnyLost/Studio-Tracktion/pull/91) | In M43 |
| `feature/studio-decent-kontakt-importer-m24` | `a8d9a2a` | [69](https://github.com/JonnyLost/Studio-Tracktion/pull/69) | In M43 |
| `feature/studio-deep-black-m41` | `d3b464c` | [90](https://github.com/JonnyLost/Studio-Tracktion/pull/90) | In M43 |
| `feature/studio-design-system-m33` | `50ba92a` | [78](https://github.com/JonnyLost/Studio-Tracktion/pull/78) | In M43 |
| `feature/studio-dls-general-midi-library-m18` | `afbbf4d` | [63](https://github.com/JonnyLost/Studio-Tracktion/pull/63) | In M43 |
| `feature/studio-environments-m36` | `136588f` | [81](https://github.com/JonnyLost/Studio-Tracktion/pull/81) | In M43 |
| `feature/studio-environments-m46` | `5d27eb8` | [12](https://github.com/JonnyLost/Studio-Tracktion/pull/12) | M43 welcome and templates retained |
| `feature/studio-exs24-importer-m22` | `80f517b` | [67](https://github.com/JonnyLost/Studio-Tracktion/pull/67) | In M43 |
| `feature/studio-film-scoring-core-m1` | `6920a70` | [34](https://github.com/JonnyLost/Studio-Tracktion/pull/34) | In M43 |
| `feature/studio-film-scoring-cue-exports-m4` | `fdac810` | [37](https://github.com/JonnyLost/Studio-Tracktion/pull/37) | In M43 |
| `feature/studio-film-scoring-cues-sync-m3` | `89a158f` | [36](https://github.com/JonnyLost/Studio-Tracktion/pull/36) | In M43 |
| `feature/studio-film-scoring-sync-stems-m5` | `6f7fab6` | [41](https://github.com/JonnyLost/Studio-Tracktion/pull/41) | In M43 |
| `feature/studio-film-scoring-workspace-m2` | `a294391` | [35](https://github.com/JonnyLost/Studio-Tracktion/pull/35) | Namespace fix covered by M5 |
| `feature/studio-garritan-library-import` | `67b2b20` | [48](https://github.com/JonnyLost/Studio-Tracktion/pull/48) | Merged into M44 |
| `feature/studio-global-ui-scaling` | `16b241f` | [6](https://github.com/JonnyLost/Studio-Tracktion/pull/6) | In M43 |
| `feature/studio-guided-instrument-builder-m30` | `a8dd6db` | [75](https://github.com/JonnyLost/Studio-Tracktion/pull/75) | In M43 |
| `feature/studio-hardware-console-m40` | `5c7df44` | [89](https://github.com/JonnyLost/Studio-Tracktion/pull/89) | In M43 |
| `feature/studio-inspector-controls-m39c` | `21f3636` | [86](https://github.com/JonnyLost/Studio-Tracktion/pull/86) | In M43 |
| `feature/studio-inspector-performance-m5` | `2bbde74` | [43](https://github.com/JonnyLost/Studio-Tracktion/pull/43) | In M43 |
| `feature/studio-instrument-channel-mixer-m2` | `ebbfab5` | [5](https://github.com/JonnyLost/Studio-Tracktion/pull/5) | In M43 |
| `feature/studio-instrument-library-m39b` | `294f4eb` | [85](https://github.com/JonnyLost/Studio-Tracktion/pull/85) | In M43 |
| `feature/studio-interface-shell-m1` | `4757dbe` | [4](https://github.com/JonnyLost/Studio-Tracktion/pull/4) | In M43 |
| `feature/studio-keyboard-dock-streamline-m13` | `08eb206` | [56](https://github.com/JonnyLost/Studio-Tracktion/pull/56) | In M43 |
| `feature/studio-keyboard-live-playback-feedback-m10` | `abf8b64` | [53](https://github.com/JonnyLost/Studio-Tracktion/pull/53) | In M43 |
| `feature/studio-keyboard-note-feedback-m9` | `1843b1f` | [52](https://github.com/JonnyLost/Studio-Tracktion/pull/52) | In M43 |
| `feature/studio-keyboard-slate-theme-m15` | `a92bcbe` | [58](https://github.com/JonnyLost/Studio-Tracktion/pull/58) | In M43 |
| `feature/studio-legacy-sampler-importer-m23` | `0bd745c` | [68](https://github.com/JonnyLost/Studio-Tracktion/pull/68) | In M43 |
| `feature/studio-midi-focus-m12` | `98de7f4` | [28](https://github.com/JonnyLost/Studio-Tracktion/pull/28) | In M43 |
| `feature/studio-mixer-inspector-m13` | `a3c836f` | [31](https://github.com/JonnyLost/Studio-Tracktion/pull/31) | In M43 |
| `feature/studio-musical-workspaces-m34` | `c6f7922` | [79](https://github.com/JonnyLost/Studio-Tracktion/pull/79) | In M43 |
| `feature/studio-native-sampler-m5` | `f60dd90` | [11](https://github.com/JonnyLost/Studio-Tracktion/pull/11) | In M43 |
| `feature/studio-native-synth-m6` | `d6e6212` | — | In M43 |
| `feature/studio-orchestral-seating-m1` | `75fe665` | [15](https://github.com/JonnyLost/Studio-Tracktion/pull/15) | In M43 |
| `feature/studio-orchestral-stage-m4` | `ca4818e` | [8](https://github.com/JonnyLost/Studio-Tracktion/pull/8) | In M43 |
| `feature/studio-postintegration-composer-workflow-m6` | `fdeb2f7` | [47](https://github.com/JonnyLost/Studio-Tracktion/pull/47) | In M43 |
| `feature/studio-professional-interface-m45` | `62603a1` | [10](https://github.com/JonnyLost/Studio-Tracktion/pull/10) | In M43 |
| `feature/studio-professional-midi-m3` | `b46a866` | [7](https://github.com/JonnyLost/Studio-Tracktion/pull/7) | In M43 |
| `feature/studio-reliability-performance-m38` | `9739bab` | [83](https://github.com/JonnyLost/Studio-Tracktion/pull/83) | In M43 |
| `feature/studio-room-return-mixer-only` | `f5808d2` | [46](https://github.com/JonnyLost/Studio-Tracktion/pull/46) | In M43 |
| `feature/studio-sampler-articulations-m11` | `e56a18e` | [22](https://github.com/JonnyLost/Studio-Tracktion/pull/22) | In M43 |
| `feature/studio-sampler-builder-m10` | `483da0b` | [21](https://github.com/JonnyLost/Studio-Tracktion/pull/21) | In M43 |
| `feature/studio-sampler-disk-streaming-m19` | `1670f38` | [64](https://github.com/JonnyLost/Studio-Tracktion/pull/64) | In M43 |
| `feature/studio-sampler-interface-m28` | `017efdb` | [73](https://github.com/JonnyLost/Studio-Tracktion/pull/73) | In M43 |
| `feature/studio-sampler-performance-m26` | `a60f39f` | [71](https://github.com/JonnyLost/Studio-Tracktion/pull/71) | In M43 |
| `feature/studio-sampler-stabilization-m27` | `d20dffa` | [72](https://github.com/JonnyLost/Studio-Tracktion/pull/72) | In M43 |
| `feature/studio-sfz-importer-m20` | `3cfecf3` | [65](https://github.com/JonnyLost/Studio-Tracktion/pull/65) | In M43 |
| `feature/studio-simplified-navigation-m12` | `10fcc3a` | [30](https://github.com/JonnyLost/Studio-Tracktion/pull/30) | In M43 |
| `feature/studio-sound-bank-importer-m21` | `3bf8d38` | [66](https://github.com/JonnyLost/Studio-Tracktion/pull/66) | In M43 |
| `feature/studio-theme-isolation-m43` | `186f067` | [92](https://github.com/JonnyLost/Studio-Tracktion/pull/92) | In M43 |
| `feature/studio-themes-icons-m39d` | `9d1edb1` | [87](https://github.com/JonnyLost/Studio-Tracktion/pull/87) | In M43 |
| `feature/studio-transport-context-workflow-m7` | `f93e3ed` | [50](https://github.com/JonnyLost/Studio-Tracktion/pull/50) | In M43 |
| `feature/studio-transport-header-m14` | `e1dd0fd` | [32](https://github.com/JonnyLost/Studio-Tracktion/pull/32) | In M43 |
| `feature/studio-unified-acoustic-stage-m7` | `b81101f` | [16](https://github.com/JonnyLost/Studio-Tracktion/pull/16) | In M43 |
| `feature/studio-unified-instruments-m65` | `8475a0c` | [14](https://github.com/JonnyLost/Studio-Tracktion/pull/14) | In M43 |
| `feature/studio-unified-integration-m6` | `0b2919f` | [13](https://github.com/JonnyLost/Studio-Tracktion/pull/13) | In M43 |
| `feature/studio-unified-track-headers-m12` | `a9abfa1` | [29](https://github.com/JonnyLost/Studio-Tracktion/pull/29) | In M43 |
| `feature/studio-universal-instrument-import` | `ef81438` | [49](https://github.com/JonnyLost/Studio-Tracktion/pull/49) | Merged into M44 |
| `feature/studio-universal-library-m25` | `303ae21` | [70](https://github.com/JonnyLost/Studio-Tracktion/pull/70) | In M43 |
| `feature/studio-usability-acceptance-m32` | `71c047d` | [77](https://github.com/JonnyLost/Studio-Tracktion/pull/77) | In M43 |
| `feature/studio-visual-library-m29` | `6e3b74c` | [74](https://github.com/JonnyLost/Studio-Tracktion/pull/74) | In M43 |
| `feature/studio-workflow-refinement-m37` | `5c9398c` | [82](https://github.com/JonnyLost/Studio-Tracktion/pull/82) | In M43 |
| `integration/studio-0.2-integrated-alpha` | `a635baf` | [45](https://github.com/JonnyLost/Studio-Tracktion/pull/45) | In M43 |
| `integration/studio-0.2-m7-baseline` | `b81101f` | [17](https://github.com/JonnyLost/Studio-Tracktion/pull/17) | In M43 |
| `main` | `f123be5` | — | In M43 |
