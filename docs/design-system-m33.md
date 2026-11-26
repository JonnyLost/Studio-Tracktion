# M33 — Composer Experience: shared design system

Experimental child of M32 commit 71c047d. Do not merge or promote until M32 and M33 acceptance are green.

## Implemented
- Central opaque ARGB palette, restrained warm charcoal console surfaces, champagne controls and brass Atelier identity. Recording red, signal green, keyboard focus blue, clip and instrument-family colours stay semantically distinct.
- Shared vector rotary renderer used by Studio's default LookAndFeel and the floating Atelier editor. Preserves slider values, ranges, gestures and callbacks; honors configured thumb colours. No bitmap or new dependency.
- Active-button outline and stronger keyboard-focus outline; explicit slider value/text backgrounds. Inspector headings use common typography tokens and brass identity.
- Contrast regression checks integrated into the existing appearance metrics suite; tests remain active in Release. Existing scale/layout checks unchanged.

## Acceptance
Build and run the full Linux and Intel macOS suites. Capture existing UI galleries and inspect at 1024x640 and 1280x800 with standard and largest text. Verify keyboard focus, active/disabled states, rotary position at min/mid/max, legible numeric values and unchanged audition, automation, project data and plug-in UI. Review contrast and color semantics in actual captures; numerical token checks do not certify accessibility.

M32's corrected build compiled on both platforms but failed StudioSamplerIntegration and StudioUnifiedStage (38/40 Linux, 40/42 Intel macOS); JUCE assertions also remain. Those failures are not waived or fixed by this appearance-only change. No claim of completed M33 acceptance or human screenshot review. Existing fixed-size floating Atelier layout and native hosted plug-in appearance are unchanged; adaptive layouts and artwork belong to later milestones.
