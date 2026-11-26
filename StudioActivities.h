// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AppearanceMetrics.h"
#include <optional>

namespace studio
{
enum class Activity { compose, perform, record, design, mix, score };
struct ActivityEntry { Activity activity; const char* label; const char* help; };
inline constexpr std::array activities {
    ActivityEntry { Activity::compose, "Compose", "Arrange regions and edit notes in the Piano Roll." },
    ActivityEntry { Activity::perform, "Perform", "Choose an instrument or play the selected instrument using its Inspector." },
    ActivityEntry { Activity::record, "Record", "Focus on the arrangement and input controls. Does not start recording." },
    ActivityEntry { Activity::design, "Design", "Open the selected native instrument editor, or browse instruments." },
    ActivityEntry { Activity::mix, "Mix", "Open the Mixer; Stage and Rooms remain available." },
    ActivityEntry { Activity::score, "Score", "Open the picture and film scoring tools." }
};

inline constexpr std::optional<Activity> activityForWorkspace (Workspace workspace)
{
    switch (workspace)
    {
        case Workspace::closed: return Activity::compose;
        case Workspace::piano: return Activity::compose;
        case Workspace::instruments: case Workspace::articulations: return Activity::perform;
        case Workspace::instrumentEditor: return Activity::design;
        case Workspace::mix: case Workspace::stage: case Workspace::rooms:
        case Workspace::microphones: case Workspace::orchestraSeating: case Workspace::channel: return Activity::mix;
        case Workspace::filmScoring: return Activity::score;
        default: return std::nullopt;
    }
}

inline bool activitiesFit (int available, const AppearanceMetrics& m)
{
    return available >= (int) activities.size() * m.span (79) + m.span (125);
}
}
