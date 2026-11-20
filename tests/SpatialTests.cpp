// SPDX-License-Identifier: GPL-3.0-or-later
#include "../SpatialReverb.h"
#include "../SpatialState.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <numeric>

using namespace studio::spatial;

static void models()
{
    assert ((validPosition ({ -8, 2 }) == Position { -1, 1 }));
    assert (validPosition ({ std::numeric_limits<float>::quiet_NaN(), 0 }).x == 0);
    assert (seat (Seating::concert, Section::strings, 0, 4).x < seat (Seating::concert, Section::strings, 3, 4).x);
    assert (seat (Seating::concert, Section::percussion, 0, 1).depth > seat (Seating::concert, Section::strings, 0, 1).depth);
    assert (seat (Seating::chamber, Section::brass, 0, 2).depth < seat (Seating::concert, Section::brass, 0, 2).depth);
    assert (sendDb ({ 0, 0 }, -12) == -22 && sendDb ({ 0, 1 }, -12) == -12);

    Room r;
    r.width = -1;
    r.height = 200;
    r.early = std::numeric_limits<float>::quiet_NaN();
    r.decayTrim[3] = -8;
    auto corrected = validate (r);
    assert (corrected.width == 3 && corrected.height == 35 && corrected.early == Room().early
            && corrected.decayTrim[3] == 0.25f);
    auto dry = Room(); dry.treatment = 1.0f;
    auto live = dry; live.treatment = 0.0f;
    assert (predictDecay (dry)[4] < predictDecay (live)[4]);
    assert (predictDecay (dry)[0] > predictDecay (dry)[5]);
    assert (stereoPairs[1].spacingMetres == 0.17f && stereoPairs[1].angleDegrees == 110.0f);
}

static void stateRoundTrip()
{
    juce::ValueTree edit ("EDIT"), a ("TRACK"), b ("TRACK"), root (sceneType);
    a.setProperty ("id", "11", nullptr).setProperty ("name", "Violin", nullptr);
    b.setProperty ("id", "12", nullptr).setProperty ("name", "Violin", nullptr);
    edit.addChild (a, -1, nullptr);
    edit.addChild (b, -1, nullptr);
    edit.addChild (root, -1, nullptr);
    root.setProperty ("version", version, nullptr);
    const Seat first { { -0.32f, 0.2f }, Section::strings, -18 }, second { { 0.42f, 0.3f }, Section::strings, -20 };
    a.addChild (writeSeat ("11", first), -1, nullptr);
    b.addChild (writeSeat ("12", second), -1, nullptr);
    edit.moveChild (0, 1, nullptr);
    a.setProperty ("name", "Solo Violin", nullptr);
    auto savedRoom = factoryRooms[5].room;
    savedRoom.decayTrim[2] = 1.25f;
    savedRoom.gain = -19;
    auto custom = writeRoom (savedRoom, "Stone & strings");
    juce::ValueTree library (libraryType);
    library.addChild (custom, -1, nullptr);
    root.addChild (library, -1, nullptr);
    juce::ValueTree capsules (microphonesType), setups (setupsType), arrangement ("MIC_SETUP");
    Mic mic;
    mic.position = { -0.23f, 0.10f }; mic.azimuth = -55.0f; mic.delay = 1.2f;
    mic.pattern = Pattern::supercardioid; mic.gain = -5; mic.pan = -0.7f; mic.solo = true;
    capsules.addChild (writeMic ("mic-1", "Near L", mic), -1, nullptr);
    arrangement.setProperty ("name", "Scoring pair", nullptr);
    arrangement.addChild (capsules.createCopy(), -1, nullptr);
    setups.addChild (arrangement, -1, nullptr);
    root.addChild (setups, -1, nullptr);
    root.addChild (capsules, -1, nullptr);

    juce::UndoManager undo;
    undo.beginNewTransaction ("Move seat");
    replaceChild (a, a.getChild (0), writeSeat ("11", second), undo);
    assert (readSeat (a.getChild (0)) == second);
    assert (undo.undo() && readSeat (a.getChild (0)) == first);
    assert (undo.redo() && readSeat (a.getChild (0)) == second);
    assert (undo.undo());

    const auto reopened = juce::ValueTree::fromXml (*edit.createXml());
    assert (reopened.getChild (0).getProperty ("id").toString() == "12");
    assert (readSeat (reopened.getChildWithProperty ("id", "11").getChild (0)) == first);
    assert (readSeat (reopened.getChildWithProperty ("id", "12").getChild (0)) == second);
    const auto stored = reopened.getChildWithName (sceneType);
    assert ((int) stored.getProperty ("version") == version);
    auto room = stored.getChildWithName (libraryType).getChildWithProperty ("name", "Stone & strings");
    assert (readRoom (room) == savedRoom);
    assert (readMic (stored.getChildWithName (microphonesType).getChild (0)) == mic);
    assert (readMic (stored.getChildWithName (setupsType).getChild (0).getChildWithName (microphonesType).getChild (0)) == mic);
    assert (readRoom ({}) == Room());
}

static void audio()
{
    for (double sampleRate : { 44100, 48000, 96000 })
    {
        constexpr int frames = 131072;
        std::vector<float> left ((size_t) frames), right ((size_t) frames);
        RoomReverb render;
        render.prepare (sampleRate, {});
        const auto memory = render.allocatedBytes();
        assert (memory > 0 && memory < 2u * 1024 * 1024);
        left[0] = 1.0f;
        for (int i = 0; i < frames; i += 137)
            render.process (left.data() + i, right.data() + i, std::min (137, frames - i));
        assert (render.allocatedBytes() == memory);
        assert (left.front() == 0 && right.front() == 0);
        assert (left != right);
        assert (std::all_of (left.begin(), left.end(), [] (float x) { return std::isfinite (x) && std::abs (x) < 1.0f; }));
        assert (std::inner_product (left.begin(), left.end(), left.begin(), 0.0) > 1.0e-5);
        assert (std::inner_product (right.begin(), right.end(), right.begin(), 0.0) > 1.0e-5);
        render.reset();
        std::fill (left.begin(), left.end(), 0.0f);
        std::fill (right.begin(), right.end(), 0.0f);
        render.process (left.data(), right.data(), frames);
        assert (std::all_of (left.begin(), left.end(), [] (float x) { return x == 0; }));

        auto bypass = factoryRooms[5].room;
        bypass.bypass = true;
        render.prepare (sampleRate, bypass);
        left[0] = 1;
        render.process (left.data(), right.data(), frames);
        assert (std::all_of (left.begin(), left.end(), [] (float x) { return x == 0; }));
        bypass.bypass = false;
        bypass.decayTrim = { 2, 0.25f, 2, 0.25f, 2, 0.25f };
        render.setRoom (bypass);
        left[0] = 1;
        render.process (left.data(), right.data(), frames);
        assert (std::all_of (left.begin(), left.end(), [] (float x) { return std::isfinite (x); }));
        assert (std::inner_product (left.begin(), left.end(), left.begin(), 0.0) > 1.0e-5);
        assert (render.allocatedBytes() == memory);
    }
}

int main()
{
    juce::MessageManager::getInstance();
    models();
    stateRoundTrip();
    audio();
    juce::MessageManager::deleteInstance();
    std::cout << "Spatial validation, state/undo, and deterministic stereo reverb tests passed.\n";
}
