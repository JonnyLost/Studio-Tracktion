// SPDX-License-Identifier: GPL-3.0-or-later
#include "../SamplerEngine.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
using namespace studio::sampler;
int main()
{
    std::cout << "rate\tframes\tvoices\tunique PCM bytes\tmean us\tp95 us\tmax us\tdeadline misses\n";
    for (int rate : { 44100, 48000 }) for (int frames : { 128, 256, 512 })
    {
        auto pcm = std::make_shared<Sample>(); pcm->rate = 48000; pcm->left.resize (48000);
        for (int i = 0; i < 48000; ++i) pcm->left[static_cast<std::size_t> (i)] = .1f * std::sin (i * .034f);
        Bank bank; bank.settings.polyphony = maxVoices; bank.settings.attack = 0; bank.sampleRevision = 1;
        Group group; bank.groups.push_back (group);
        Zone zone; zone.sample = pcm; zone.loop = true; zone.loopStart = 1000; zone.loopEnd = 40000; zone.loopCrossfade = 256;
        zone.soft = 1; zone.hard = 127; zone.fadeIn = 16; zone.fadeOut = 16;
        bank.zones.assign (maxVoices, zone);
        Engine engine; engine.prepare (rate); engine.use (bank); engine.midi (0x90, 60, 90);
        std::array<float, 512> left {}, right {};
        std::vector<double> times; times.reserve (2000);
        int misses = 0; const double budget = frames * 1.0e6 / rate;
        for (int block = 0; block < 2000; ++block)
        {
            std::fill (left.begin(), left.end(), 0); std::fill (right.begin(), right.end(), 0);
            const auto start = std::chrono::steady_clock::now();
            engine.use (bank); engine.render (left.data(), right.data(), frames); engine.publishMeters();
            const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - start).count();
            if (block >= 100) { times.push_back (us); if (us > budget) ++misses; }
        }
        double mean = 0; for (double t : times) mean += t; mean /= times.size(); std::sort (times.begin(), times.end());
        std::cout << rate << '\t' << frames << '\t' << engine.activeVoices() << '\t' << pcm->bytes() << '\t'
                  << mean << '\t' << times[times.size() * 95 / 100] << '\t' << times.back() << '\t' << misses << '\n';
    }
}
