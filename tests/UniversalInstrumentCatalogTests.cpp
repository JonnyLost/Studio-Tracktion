// SPDX-License-Identifier: GPL-3.0-or-later
#include "UniversalInstrumentCatalog.h"
#include "StudioInstrumentCatalogData.h"
#include <iostream>
int main()
{
    studio::designer::Catalog c;
    int size = 0;
    const auto* data = StudioInstrumentCatalogData::getNamedResource(
        StudioInstrumentCatalogData::namedResourceList[0], size);
    if (data == nullptr || size <= 0) return 1;
    const auto json = juce::String::fromUTF8(data, size);
    const auto status = c.load(json);
    if (status.failed()) { std::cerr << status.getErrorMessage() << '\n'; return 2; }
    if (c.instrumentCount() < 559 || c.techniqueCount() < 432 || c.controlCount() < 61) return 3;
    if (!c.searchInstruments("cor anglais").contains("double-reeds.english-horn")) return 4;
    auto flute = c.candidateTechniques("flutes.concert-flute");
    if (flute.isEmpty() || flute[0] != "phrasing.sustain" || !flute.contains("wind.flutter-tongue")) return 5;
    if (!c.candidateTechniques("not-a-real-id").isEmpty()) return 6;
    if (c.load("{}").wasOk() || c.instrumentCount() < 559) return 7;
    auto duplicate = json.replace("\"flutes.concert-flute\"", "\"flutes.piccolo\"");
    if (c.load(duplicate).wasOk() || c.instrumentCount() < 559) return 8;
    std::cout << c.instrumentCount() << " instruments, " << c.techniqueCount()
              << " techniques, " << c.controlCount() << " controls\n";
    return 0;
}
