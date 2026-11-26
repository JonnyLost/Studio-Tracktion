// SPDX-License-Identifier: GPL-3.0-or-later
#include "../SamplerEngine.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace studio::sampler;

int main()
{
    auto sample = std::make_shared<Sample>();
    sample->rate = 48000;
    sample->left.resize (4096, 0.5f);
    for (int i = 128; i < 4096; ++i) sample->left[static_cast<std::size_t> (i)] = 0.25f;
    Bank bank;
    bank.sampleRevision = 1;
    bank.settings.gainDb = 0;
    Zone z;
    z.sample = sample;
    z.sfz.enabled = true;
    z.sfz.attack = z.sfz.decay = 0;
    z.sfz.release = 0.05f;
    z.sfz.velocityTrack = 0;
    z.sfz.pan = -100;
    z.sfz.fadeInLow = 20; z.sfz.fadeInHigh = 80; z.sfz.powerFade = false;
    assert (velocityWeight (z, 20) == 0.0f && velocityWeight (z, 50) == 0.5f && velocityWeight (z, 80) == 1.0f);
    z.sfz.powerFade = true;
    assert (std::abs (velocityWeight (z, 50) - std::sqrt (0.5f)) < 1e-6f);
    z.sfz.fadeInLow = z.sfz.fadeInHigh = 0;
    z.loop = true; z.loopStart = 0; z.loopEnd = 128; z.loopCrossfade = 0;
    z.sfz.sustainLoop = true;
    bank.zones.push_back (z);
    Group group; group.trigger = GroupTrigger::keyswitch; group.key = 24; group.switchDefault = -1;
    bank.groups.push_back (group);
    Engine engine;
    engine.prepare (48000);
    std::array<float, 256> left {}, right {};
    auto render = [&]
    {
        left.fill (0); right.fill (0);
        engine.render (left.data(), right.data(), 256);
        engine.publishMeters();
    };
    engine.use (bank); engine.midi (0x90, 60, 70); render();
    assert (left[100] == 0.0f);
    engine.use (bank); engine.midi (0x90, 24, 100); engine.midi (0x90, 60, 70); render();
    assert (std::abs (left[200] - 0.5f) < 1e-6f && right[200] == 0.0f);
    engine.use (bank); engine.midi (0x80, 60, 0); render();
    assert (left[200] > 0.1f && left[200] < 0.25f); // sustain loop exits into quieter tail
    engine.panic();
    bank.groups[0].switchDefault = 24;
    bank.zones[0].sfz.velocityTrack = 100;
    engine.use (bank); engine.midi (0x90, 60, 64); render();
    const auto expected = 0.5f * (64.0f / 127.0f) * (64.0f / 127.0f);
    assert (std::abs (left[200] - expected) < 1e-6f);
    std::puts ("SFZ playback: velocity curves, keyswitch defaults, pan, and sustain-loop release passed.");
}
