// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>

namespace studio
{
enum class WorkflowCommand { browser, piano, mixer, stage, rooms, microphones, picture,
                             inspector, instrument, keyboard, resources, settings, environments };
struct WorkflowEntry { WorkflowCommand command; const char* label; const char* keywords; };
inline constexpr std::array workflowCommands {
    WorkflowEntry { WorkflowCommand::browser, "Instrument Browser", "library patches sounds sampler synth Command-5" },
    WorkflowEntry { WorkflowCommand::piano, "Piano Roll", "compose notes midi Command-1" },
    WorkflowEntry { WorkflowCommand::mixer, "Mixer", "mix volume pan faders Command-2" },
    WorkflowEntry { WorkflowCommand::stage, "Orchestral Stage", "seating orchestra Command-3" },
    WorkflowEntry { WorkflowCommand::rooms, "Rooms", "reverb hall acoustics" },
    WorkflowEntry { WorkflowCommand::microphones, "Virtual Microphones", "stage spatial mic" },
    WorkflowEntry { WorkflowCommand::picture, "Film Scoring", "score video picture cue Command-4" },
    WorkflowEntry { WorkflowCommand::inspector, "Show / Hide Inspector", "track settings Command-I" },
    WorkflowEntry { WorkflowCommand::instrument, "Open Selected Instrument", "original plugin AU sampler synth Command-6" },
    WorkflowEntry { WorkflowCommand::keyboard, "On-Screen MIDI Keyboard", "perform audition Shift-Command-K" },
    WorkflowEntry { WorkflowCommand::resources, "Resource Inspector", "CPU RAM performance diagnostics" },
    WorkflowEntry { WorkflowCommand::settings, "Studio Settings", "audio preferences plugin manager" },
    WorkflowEntry { WorkflowCommand::environments, "Studio Environments", "welcome project templates" }
};
}
