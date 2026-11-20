# Unified instrument workspace

Milestone 6.5 starts at `0b2919f083d8851b0393d596a0e719cc7c6b2805` on `feature/studio-unified-integration-m6` in `JonnyLost/Studio-Tracktion`. That integration contains Studio Environments, both native instruments, the original Synth catalog and the Intel regression workflow.

The workspace uses Studio’s existing dock, viewport and arrangement divider. A message-thread host owns one native editor and holds its associated Tracktion plug-in reference. The chooser and navigation use the Tracktion plug-in ID within the current Edit. Floating transfers the same editor from the workspace to a resizable JUCE window. Dock reverses the transfer. Page changes and presentation changes stay outside the audio graph. Tracktion keeps ownership of MIDI, audio, automation and the instrument state.

Selecting another track resolves its native instrument instances and retains the previous editor’s navigation separately from its sound. The host releases the editor before releasing its plug-in reference, and resets before the Edit is destroyed. Studio’s Tracktion window adapter handles native editor requests from the arrangement footer. Hosted Audio Units continue to use Tracktion’s existing floating windows, including the ARIA restoration path.

Application preferences hold floating geometry and project-scoped editor navigation and presentation. Project and preset formats continue to carry the actual instrument settings.
