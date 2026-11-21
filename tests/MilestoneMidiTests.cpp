// SPDX-License-Identifier: GPL-3.0-or-later
#include "../ArticulationMappings.h"
#include "../MidiExpressionValues.h"
#include "../ResourceSample.h"
#include <cassert>
#include <iostream>
#include <limits>

int main()
{
    using namespace studio;
    for (int v = 0; v <= 127; ++v)
    {
        assert (encodeExpressionValue (ExpressionValue::sevenBit, v) == v << 7);
        assert (decodeExpressionValue (ExpressionValue::sevenBit, (v << 7) | 0x7f) == v);
        assert (encodeExpressionValue (ExpressionValue::switch64, v) == (v < 64 ? 0 : 127 << 7));
    }
    for (int v = 0; v <= 16383; ++v)
        assert (decodeExpressionValue (ExpressionValue::pitch14, encodeExpressionValue (ExpressionValue::pitch14, v)) == v);
    assert (encodeExpressionValue (ExpressionValue::pitch14, -1) == 0);
    assert (encodeExpressionValue (ExpressionValue::pitch14, 20000) == 16383);
    for (int source = 0; source <= 17; ++source)
        for (int target = 0; target <= 17; ++target)
            assert (matchingClipChannel (source, target) == (source == target && source >= 1 && source <= 16));

    auto doc = std::make_unique<juce::XmlElement> ("StudioArticulations");
    auto* flute = doc->createNewChildElement ("Instrument");
    flute->setAttribute ("instance", "101");
    auto* horn = doc->createNewChildElement ("Instrument");
    horn->setAttribute ("instance", "102");
    const std::vector<ArticulationMapping> original {
        { "one", "Sustain & Legato", ArticulationMessage::keyswitch, 4, 24, 100, 9 },
        { "two", "Staccato", ArticulationMessage::controller, 16, 32, 0, 0 }
    };
    writeArticulations (*flute, original);
    writeArticulations (*horn, { { "three", "Stopped", ArticulationMessage::controller, 3, 11, 72, 0 } });
    const auto wire = doc->toString (juce::XmlElement::TextFormat().singleLine());
    const auto reopened = juce::parseXML (wire);
    assert (reopened != nullptr);
    const auto* savedFlute = reopened->getChildByAttribute ("instance", "101");
    const auto* savedHorn = reopened->getChildByAttribute ("instance", "102");
    assert (savedFlute != nullptr && savedHorn != nullptr);
    auto restored = readArticulations (*savedFlute);
    assert (restored.size() == original.size() && readArticulations (*savedHorn).size() == 1);
    for (size_t i = 0; i < restored.size(); ++i)
    {
        const auto& a = restored[i];
        const auto& b = original[i];
        assert (a.id == b.id && a.name == b.name && a.message == b.message && a.channel == b.channel
                && a.number == b.number && a.value == b.value && a.shortcut == b.shortcut);
    }
    savedFlute->getFirstChildElement()->setAttribute ("channel", 17);
    savedFlute->getFirstChildElement()->getNextElement()->setAttribute ("type", "unrecognised");
    assert (readArticulations (*savedFlute).empty());
    assert (readArticulations (*savedHorn).size() == 1);

    assert (processCPUFraction (15.0, 16.25, 0.5) == 2.5f);
    assert (processCPUFraction (5.0, 5.0, 0.5) == 0.0f);
    assert (! processCPUFraction (5.0, 5.0, 0.0));
    assert (! processCPUFraction (6.0, 5.0, 1.0));
    assert (! processCPUFraction (1.0, std::numeric_limits<double>::infinity(), 0.5));
    std::cout << "MIDI values, articulation mapping round-trip, and resource sample tests passed.\n";
}
