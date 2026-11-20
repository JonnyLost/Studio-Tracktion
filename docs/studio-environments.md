# Studio 0.2 — Studio Environments

Studio Environments introduces an optional welcome screen using five original
composer and recording-studio illustrations supplied by the Studio project owner.
The images are prepared as compact JPEG resources (about 1.7 MiB combined).
The original high-resolution artwork remains untouched outside the source tree.

## Using the welcome screen

- **New Project** invokes Studio's existing safe New Project workflow.
- **Open Project** invokes Studio's existing project-open workflow.
- **Continue to Studio** closes the welcome screen without modifying a project.
- Choose any of the five image tiles to view it immediately; use Previous/Next
  or the Left/Right keys to browse. Escape or Return closes the screen.
- **Favorite** includes the current illustration in automatic rotation. At
  least one scene must remain selected. Initially all five are included.
- **Rotation: Shuffle** selects an eligible different scene at random.
  **In Order** visits eligible scenes sequentially. **Keep Selected** keeps
  the selected illustration instead of automatically changing it.
- **Cycle while open** changes the image about every 12 seconds. Rotation
  stops immediately when the screen closes. Nothing runs in the background
  while composing.
- **Show at startup** is enabled by default. Turn it off to skip the screen on
  future launches. The gallery remains available from **View... → Studio
  Environments...** at any time.

Preferences are stored with Studio's application settings, independently of
Tracktion project files. Image decoding and preference writes are performed
on the message thread; no additional processing is performed by an audio
callback. Only one full-resolution illustration and small previews need to be
decoded at once.

## Architecture

- `StudioEnvironments.{h,cpp}` contains the JUCE welcome component and its
  visible controls. `Studio.cpp` wires the New and Open actions to the existing
  project dialogs without touching audio routing or save safeguards.
- `EnvironmentSelection.h` contains deterministic, dependency-free rotation
  rules. `tests/EnvironmentSelectionTests.cpp` covers favourites, fixed
  selection, sequential wraparound, and non-repeating shuffle.
- `assets/environments/` stores lightweight embedded versions of the five
  source illustrations. The `StudioEnvironmentsAssets` JUCE resource target
  packages them in the Studio app; users do not need external artwork files.
- The native interface review also captures the welcome screen at 1280 × 800
  and 1024 × 640 at the largest UI/text settings.

## MacBook acceptance

1. Start Studio: the welcome screen should display an image and the three
   project actions. Verify Continue reaches the normal arrangement.
2. Open **View... → Studio Environments...**, select all five images, and
   confirm each preview matches its tile. Try Previous/Next and keyboard
   navigation.
3. Choose Shuffle and In Order; verify the image changes while the welcome
   screen stays open. Confirm Keep Selected stops automatic changes.
4. Mark and unmark favourites; only favourites should appear in automatic
   rotation. One image must always remain eligible.
5. Uncheck **Show at startup**, quit, and relaunch. Studio should open without
   the welcome screen. Reopen the gallery from View and re-enable the option.
6. Check **New Project** and **Open Project** with disposable projects. Verify
   ARIA restoration, recording, audio playback, save protection, and dock
   resizing still behave exactly as before.
7. Check the layout at 1024 × 640 and 1280 × 800 and at larger interface and
   text sizes. The project actions must remain readable and reachable.

No branch merges are part of this feature. Studio Sampler's separate native
engine milestone can proceed independently.
