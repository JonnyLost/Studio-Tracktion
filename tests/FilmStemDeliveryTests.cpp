// SPDX-License-Identifier: GPL-3.0-or-later
#include "../FilmStemDelivery.h"
#include <cassert>
#include <iostream>

using namespace studio::film;

int main()
{
    std::vector<StemAssignment> assignments;
    assert (setStemFamily (assignments, "1001", StemFamily::strings));
    assert (setStemFamily (assignments, "1002", StemFamily::strings));
    assert (setStemFamily (assignments, "1003", StemFamily::brass));
    assert (setStemFamily (assignments, "1004", StemFamily::percussion));
    assert (! setStemFamily (assignments, "../1005", StemFamily::strings));
    assert (! setStemFamily (assignments, "0", StemFamily::brass));
    assert (! setStemFamily (assignments, "1005", (StemFamily) 500));

    const std::vector<std::string> live { "1004", "1001", "1003", "1002", "1004", "1999" };
    const auto groups = planFamilyStems (assignments, live);
    assert (groups.size() == 3);
    assert (groups[0].family == StemFamily::strings);
    assert ((groups[0].trackIds == std::vector<std::string> { "1001", "1002" }));
    assert (groups[1].family == StemFamily::brass && groups[1].trackIds.size() == 1);
    assert (groups[2].family == StemFamily::percussion && groups[2].trackIds.size() == 1);
    assert (groups[2].trackIds[0] == "1004"); // Ignore duplicate live IDs.

    // Assignments survive track renaming because they use the stable EditItemID.
    assert (familyForTrack (assignments, "1001") == StemFamily::strings);
    assert (setStemFamily (assignments, "1001", StemFamily::woodwinds));
    assert (familyForTrack (assignments, "1001") == StemFamily::woodwinds);
    assert (planFamilyStems (assignments, { "1001" }).size() == 1);
    assert (planFamilyStems (assignments, { "9999" }).empty()); // Stale IDs ignored.

    // Removing assignments never accidentally makes an all-tracks render.
    assert (setStemFamily (assignments, "1001", StemFamily::unassigned));
    assert (familyForTrack (assignments, "1001") == StemFamily::unassigned);
    assert (planFamilyStems (assignments, { "1001" }).empty());

    for (auto family : stemFamilies)
    {
        assert (validStemFamily ((int) family));
        assert (stemFamilyFileName (family)[0] != '\0');
    }
    std::cout << "Explicit family stems, stable track IDs, omissions and overlap checks passed\n";
}
