# M32 — Consolidated usability acceptance candidate

Based on M31; no new format or engine features and no merges.

## Verified baseline
M31 Linux run 45855021335 built Studio and passed 39/40 CTest
cases. Intel macOS run 45855021375 built Studio and passed 41/42.
Both failed the Unified Stage collapsed-group visibility review.
M30 had the same single failure. macOS debug logs also include JUCE
assertions; they are not claimed resolved. This is not a clean release.

## M32 changes
The pinned arrangement uses AsyncUpdater for row rebuilding. The Stage
review now waits for the real message-loop update, checks the expected
open count (hidden return plus visible folder), identities of both
children and folder, audio-track count/identity, parent submix, fader and
room send. Tests undo/redo, then expand and capture actual screenshots.
No alteration to the audio graph or hiding/deleting tracks to satisfy
the test. Timing hypothesis and correction remain unverified until CI.

Guided builder form scrolls independently of its fixed footer; narrow
forms stack fields and alternate controls. Acceptance opens the real
builder, reviews normal/largest text, prepares a fixture recording,
changes the name (invalidates old proposal), previews again and creates
through the actual sampler editor callback. Checks the resulting
instrument name, sample and Play page.

Sampler UI review captures Play, Sound and Build at 1024x640 and
1280x800, text 100/130, checks navigation and keyboard bounds,
non-overlap and body control bounds, and verifies appearance changes
do not alter instrument state. Existing dock/float, audition and
project restore reviews remain. Screenshots are actual component
renders; they do not replace human visual review.

Visual library UI review uses a temporary catalog file (not the user's
catalog), exercises search, favorite assignment, selected-row retention,
favorites filtering and browse/manage visibility. Captures normal and
largest text for both modes. No real user data modified.

Linux now exports sampler, workspace and Stage galleries; macOS
uploads UI galleries even after failing tests. Prior M31 failure skipped
the sampler gallery upload, so those screenshots were not available
for inspection. New captures are not yet generated/reviewed at this
checkpoint. M32 build, tests and actual MacBook acceptance pending.

## Mac acceptance
Use the existing checkout with no local changes. Close Studio, fetch,
switch to feature/studio-usability-acceptance-m32, pull --ff-only,
configure with BUILD_TESTING=ON, build Studio and full test targets
using existing Tracktion root. Run full CTest. Do not promote or merge
until review. Check real MIDI controller, ARIA/GM, import one known
instrument from each owned library format, stage folder/mixer, native
library and artwork, builder, saved/reopened projects, scrolling,
docking, largest text and scoring to picture. Use copies of projects.

No assertion that all visual or playback issues are resolved. No
branches merged or new dependencies.
