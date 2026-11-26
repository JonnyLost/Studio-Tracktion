// SPDX-License-Identifier: GPL-3.0-or-later
#include "../EnvironmentSelection.h"
#include "../EnvironmentTemplates.h"

#include <cassert>
#include <iostream>

int main()
{
    using namespace studio::environments;
    static_assert (sceneCount == 5);
    static_assert (eligibleMask (0) == allScenesMask);
    static_assert (eligibleMask (-1) == allScenesMask);
    static_assert (eligibleMask (1 << 7) == allScenesMask);
    static_assert (nextScene (-1, allScenesMask, Rotation::sequential, 0) == 0);
    static_assert (nextScene (0, allScenesMask, Rotation::sequential, 0) == 1);
    static_assert (nextScene (4, allScenesMask, Rotation::sequential, 0) == 0);
    static_assert (nextScene (1, 0b10100, Rotation::sequential, 0) == 2);
    static_assert (nextScene (2, 0b10100, Rotation::sequential, 0) == 4);
    static_assert (nextScene (4, 0b10100, Rotation::sequential, 0) == 2);
    static_assert (nextScene (3, allScenesMask, Rotation::fixed, 0) == 3);
    static_assert (nextScene (-1, allScenesMask, Rotation::fixed, 0) == 0);
    static_assert (nextScene (0, 1, Rotation::random, 123) == 0);

    for (int previous = 0; previous < sceneCount; ++previous)
        for (std::uint32_t draw = 0; draw < 100; ++draw)
        {
            const auto selected = nextScene (previous, allScenesMask, Rotation::random, draw);
            assert (selected >= 0 && selected < sceneCount && selected != previous);
        }
    for (std::uint32_t draw = 0; draw < 100; ++draw)
    {
        const auto scene = nextScene (2, 0b10100, Rotation::random, draw);
        assert (scene == 4);
    }
    for (const auto& p : templates)
    {
        if (p.count <= 0 || p.count > 16 || p.bpm < 40 || p.bpm > 200
            || p.room < -1 || p.room >= (int) studio::spatial::factoryRooms.size()
            || &plan (p.id) != &p) return 1;
        for (int i = 0; i < p.count; ++i)
        {
            if (std::string_view (p.tracks[i].name).empty() || (p.tracks[i].stage && p.room < 0)) return 1;
            for (int j = 0; j < i; ++j)
                if (std::string_view (p.tracks[i].name) == p.tracks[j].name) return 1;
        }
    }
    if (templates.size() != 6 || plan (TemplateId::film).activity != studio::Activity::score
        || plan (TemplateId::empty).count != 1 || plan (TemplateId::chamber).count != 4) return 1;
    std::cout << "Studio Environments rotation and templates passed.\n";
}
