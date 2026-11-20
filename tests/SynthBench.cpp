// SPDX-License-Identifier: GPL-3.0-or-later
#include "../SynthEngine.h"
#include "../SamplerEngine.h"
#include "StudioAnalogDSP.h"
#include "StudioFMDSP.h"
#include "StudioWavetableDSP.h"
#include "StudioAdditiveDSP.h"
#include "StudioGranularDSP.h"
#include "StudioPhysicalDSP.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string_view>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <vector>

namespace
{
using namespace studio::synth;
namespace sample = studio::sampler;
template <auto Create, auto Get, auto Destroy>
void defaults (Layer& layer, double rate)
{
    auto* instance = Create (rate, 1);
    if (!instance) std::exit (1);
    for (int i = 0; i < parameterCounts[static_cast<int> (layer.family)]; ++i)
        layer.values[i] = Get (instance, i);
    Destroy (instance);
}

Layer layerAt (int family, double rate)
{
    Layer layer;
    layer.family = static_cast<Family> (family);
    switch (layer.family)
    {
        case Family::analog: defaults<sa_create, sa_get_parameter, sa_destroy> (layer, rate); break;
        case Family::fm: defaults<sfm_create, sfm_get_parameter, sfm_destroy> (layer, rate); break;
        case Family::wavetable: defaults<sws_create, sws_get_parameter, sws_destroy> (layer, rate); break;
        case Family::additive: defaults<sadd_create, sadd_get_parameter, sadd_destroy> (layer, rate); break;
        case Family::granular: defaults<sgr_create, sgr_get_parameter, sgr_destroy> (layer, rate); break;
        case Family::physical: defaults<spm_create, spm_get_parameter, spm_destroy> (layer, rate); break;
        case Family::hybrid: break;
    }
    return layer;
}

void run (const char* label, int rate, int blockSize, const std::vector<Patch>& patches, bool sampler, int seconds)
{
    std::vector<std::unique_ptr<Engine>> synths;
    unsigned layers = 0;
    for (const auto& patch : patches)
    {
        auto& engine = *synths.emplace_back (std::make_unique<Engine> (patch, rate));
        if (! engine.valid()) std::exit (1);
        layers += engine.layersPlaying();
    }
    sample::Bank bank;
    std::shared_ptr<sample::Sample> pcm;
    sample::Engine sampling;
    sampling.prepare (rate);
    if (sampler)
    {
        pcm = std::make_shared<sample::Sample>();
        pcm->rate = rate;
        pcm->left.resize (48000);
        for (std::size_t i = 0; i < pcm->left.size(); ++i)
            pcm->left[i] = static_cast<float> (.2 * std::sin (i * .031));
        bank.zones = { { .loop = true, .loopStart = 200, .loopEnd = 30000, .sample = pcm } };
    }

    std::vector<float> left (blockSize), right (blockSize);
    std::vector<double> timings;
    const auto deadline = 1e6 * blockSize / rate;
    unsigned misses = 0;
    float checksum = 0;
    for (int n = 0; n < seconds * rate / blockSize; ++n)
    {
        const auto begin = std::chrono::steady_clock::now();
        std::fill (left.begin(), left.end(), 0);
        std::fill (right.begin(), right.end(), 0);
        if (sampler) sampling.use (bank);
        if (n % 32 == 0)
            for (int note = 54; note < 62; ++note)
            {
                for (auto& synth : synths) synth->midi (0x90, note, 87);
                if (sampler) sampling.midi (0x90, note, 87);
            }
        if (n % 32 == 25)
            for (int note = 54; note < 62; ++note)
            {
                for (auto& synth : synths) synth->midi (0x80, note, 0);
                if (sampler) sampling.midi (0x80, note, 0);
            }
        for (auto& synth : synths) synth->render (left.data(), right.data(), blockSize);
        if (sampler) { sampling.render (left.data(), right.data(), blockSize); sampling.publishMeters(); }
        const auto duration = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - begin).count();
        if (n >= 32)
        {
            timings.push_back (duration);
            if (duration > deadline) ++misses;
        }
        checksum += left.back();
    }
    if (! std::isfinite (checksum)) std::exit (1);
    std::sort (timings.begin(), timings.end());
    const auto mean = std::accumulate (timings.begin(), timings.end(), 0.0) / timings.size();
    std::cout << label << '\t' << rate << '\t' << blockSize << '\t' << layers << '\t' << layers * voicesPerLayer
              << '\t' << (pcm ? pcm->bytes() : 0) << '\t' << std::fixed << std::setprecision (2)
              << mean << '\t' << timings[timings.size() * 95 / 100] << '\t' << 100 * mean / deadline << '\t' << misses << '\n';
}
}

int main (int argc, char* argv[])
{
    const bool matrix = argc > 1 && std::string_view (argv[1]) == "--matrix";
    constexpr std::array names { "Analog", "FM", "Wavetable", "Additive", "Granular", "Physical" };
    std::cout << "case\trate\tframes\tlayers\tvoice capacity\tsampler PCM bytes\tmean us\tp95 us\tbudget %\tdeadline misses\n";
    for (int rate : { 44100, 48000 })
        for (int block : { 128, 256, 512 })
        {
            if (! matrix && (rate != 48000 || block != 256)) continue;
            std::vector<Patch> tracks;
            for (int i = 0; i < 6; ++i)
            {
                Patch patch;
                patch.count = 1;
                patch.layers[0] = layerAt (i, rate);
                tracks.push_back (patch);
                run (names[static_cast<std::size_t> (i)], rate, block, { patch }, false, 2);
            }
            Patch hybrid;
            hybrid.count = 4;
            for (int i = 0; i < 4; ++i) hybrid.layers[i] = tracks[static_cast<std::size_t> (i)].layers[0];
            run ("Four-layer Hybrid", rate, block, { hybrid }, false, 2);
            run ("Six Synth tracks", rate, block, tracks, false, 2);
            run ("Hybrid + Sampler", rate, block, { hybrid }, true, 2);
            run ("Offline four-layer Hybrid", rate, block, { hybrid }, false, 10);
        }
}
