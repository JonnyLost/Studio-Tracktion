// SPDX-License-Identifier: GPL-3.0-or-later
#include "../SpatialStage.h"
#include "../SpatialReverb.h"
#include <chrono>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <numbers>
#include <string_view>

namespace
{
using namespace studio::spatial;
using Stereo = std::array<std::vector<float>, 2>;

void require (bool ok, const char* message)
{
    if (! ok) { std::fprintf (stderr, "%s\n", message); std::exit (1); }
}

Stereo signal (int n)
{
    return { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
}

double energy (const std::vector<float>& signal, size_t start = 0, size_t end = SIZE_MAX)
{
    double sum = 0;
    for (size_t i = start; i < std::min (signal.size(), end); ++i)
    {
        require (std::isfinite (signal[i]), "non-finite render");
        sum += double (signal[i]) * signal[i];
    }
    return sum;
}

Stereo impulse (stage::Settings setup, int rate = 48000)
{
    auto audio = signal (rate / 2);
    audio[0][0] = audio[1][0] = .5f;
    stage::Renderer renderer;
    require (renderer.prepare (rate, stage::makePlan (rate, setup)), "source preparation");
    for (int i = 0; i < rate / 2; i += 128)
        renderer.process (audio[0].data() + i, audio[1].data() + i, std::min (128, rate / 2 - i));
    return audio;
}

void testStage()
{
    stage::Settings setup;
    require (stage::atlasToSeat (0, 1) == Position { -1, 0 } && stage::atlasToSeat (1, 0) == Position { 1, 1 },
             "atlas conductor coordinates");
    setup.seat = { -.5f, .45f };
    const auto l = impulse (setup);
    setup.seat.x *= -1;
    const auto r = impulse (setup);
    double mismatch = 0;
    for (size_t i = 0; i < l[0].size(); ++i) mismatch += std::abs (l[0][i] - r[1][i]);
    require (mismatch < 1e-4 && energy (l[0]) > energy (l[1]), "left/right mirror and audibility");
    setup.seat = { 0, 0 };
    const auto front = impulse (setup);
    setup.seat.depth = 1;
    const auto rear = impulse (setup);
    require (front[0] == front[1] && rear[0] == rear[1], "centred image");
    require (energy (rear[0], 0, 20) < .4 * energy (front[0], 0, 20), "physical distance attenuation");
    const auto rearPlan = stage::makePlan (48000, setup);
    require (rearPlan.earlyField.taps[0].delay > 10, "geometric sidewall reflection after direct signal");
    const auto s = stage::sourcePoint (setup.room, setup.seat, setup.source.height);
    const auto p = stage::receiverPoint (setup.room, setup.listener, setup.listenerHeight);
    const auto image = stage::imagePoints (setup.room, s)[0];
    const auto samples = (early::length (image, p) - early::length (s, p)) / stage::speed * 48000;
    require (std::abs (samples - rearPlan.earlyField.taps[0].delay) < .01, "image-source arrival formula");
    setup.room.wall = Surface::absorber;
    const auto treated = stage::makePlan (48000, setup);
    require (treated.earlyField.taps[0].pole < rearPlan.earlyField.taps[0].pole, "surface-dependent HF damping");
    setup.seat.depth = 0;
    const auto close = stage::makePlan (48000, setup);
    require (close.direct.airMix < rearPlan.direct.airMix && close.direct.airPole > rearPlan.direct.airPole,
             "distance-dependent HF damping");
    setup.source.family = stage::Family::brass;
    setup.source.direction = 1;
    auto aimed = stage::makePlan (48000, setup);
    setup.source.orientation = 0;
    require (stage::makePlan (48000, setup).direct.airMix > aimed.direct.airMix, "brass directivity");
    setup.source = {};
    setup.room.early = 0;
    stage::Renderer renderer;
    require (renderer.prepare (48000, stage::makePlan (48000, setup)), "stereo source preparation");
    float stereoL = .4f, stereoR = -.4f;
    renderer.process (&stereoL, &stereoR, 1);
    require (stereoL > .3f && stereoR < -.3f, "preserve stereo source");
    float monoSample = .3f;
    renderer.process (&monoSample, nullptr, 1);
    require (monoSample > .2f, "mono buffer compatibility");

    setup.microphoneCount = 2;
    setup.microphoneMix = true;
    setup.microphones[0].pan = -1;
    setup.microphones[1].pan = 1;
    setup.microphones[1].delay = 8;
    setup.microphones[0].pattern = setup.microphones[1].pattern = Pattern::omni;
    const auto pair = impulse (setup);
    require (std::abs (pair[0][0]) > .15f && energy (pair[1], 0, 383) == 0 && std::abs (pair[1][384]) > .15f,
             "causal per-capsule timing and pan");
    setup.microphones[1].delay = -30;
    require (stage::makePlan (48000, setup).microphones.capsules[1].direct.delay == 0, "negative legacy delay bounded at zero");
    setup.microphones[0].solo = true;
    const auto solo = impulse (setup);
    require (energy (solo[0]) > 0 && energy (solo[1]) == 0, "microphone solo removes other dry paths");
    auto field = stage::diffuseFieldGain (setup.microphones, 2);
    require (field[0] > .99f && field[1] == 0, "solo shapes shared diffuse field");
    setup.microphones[0].mute = true;
    field = stage::diffuseFieldGain (setup.microphones, 2);
    require (field[0] == 0 && field[1] == 0, "muted capsules silence the diffuse return");
    const auto muted = impulse (setup);
    require (energy (muted[0]) == 0 && energy (muted[1]) == 0, "muted bank has no duplicate direct signal");
    require (stage::polarGain (Pattern::omni, -1) == 1 && stage::polarGain (Pattern::cardioid, -1) == 0
        && stage::polarGain (Pattern::figureEight, -1) == -1 && stage::polarGain (Pattern::supercardioid, -1) < 0,
        "pickup pattern and rear polarity");
    setup.microphones[0].mute = false;
    setup.room.early = .8f;
    setup.microphones[1].position = { .4f, .2f };
    setup.microphones[0].solo = false;
    const auto micPlan = stage::makePlan (48000, setup);
    require (micPlan.microphones.capsules[0].reflections[0].delay != micPlan.microphones.capsules[1].reflections[0].delay,
             "independent microphone reflection geometry");
    require (renderer.prepare (48000, micPlan), "live source preparation");
    auto motion = signal (128);
    for (int block = 0; block < 100; ++block)
    {
        setup.seat.x = std::sin (float (block) * .07f);
        setup.microphoneMix = (block / 10) % 2 == 0;
        renderer.setPlan (stage::makePlan (48000, setup));
        for (auto& channel : motion)
            for (int i = 0; i < 128; ++i) channel[(size_t) i] = .2f * std::sin (float (block * 128 + i) * .031f);
        renderer.process (motion[0].data(), motion[1].data(), 128);
        for (const auto& channel : motion)
        {
            require (energy (channel) < 4, "bounded moving-source output");
            for (size_t i = 1; i < channel.size(); ++i)
                require (std::abs (channel[i] - channel[i - 1]) < .06f, "continuous geometry transition");
        }
    }
}

double t30 (std::vector<float> x, double rate)
{
    double sum = energy (x), remaining = sum;
    if (sum <= 0) return 0;
    size_t start = 0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        remaining -= double (x[i]) * x[i];
        if (start == 0 && remaining < .316227766 * sum) start = i + 1;
        if (remaining < .000316227766 * sum) return 2. * (double (i + 1) - double (start)) / rate;
    }
    return 0;
}

double persistentPeak (const std::vector<float>& tail, int rate)
{
    constexpr size_t bins = 2048;
    std::array<double, bins / 2> averaged {};
    for (int window = 0; window < 12; ++window)
    {
        const auto offset = size_t (rate / 5 + window * rate / 15);
        std::array<std::complex<double>, bins> data;
        for (size_t i = 0, reversed = 0; i < bins; ++i)
        {
            const auto hann = .5 - .5 * std::cos (2 * std::numbers::pi * double (i) / (bins - 1));
            data[reversed] = tail[offset + i] * hann;
            for (size_t bit = bins >> 1; bit != 0; bit >>= 1)
            {
                reversed ^= bit;
                if ((reversed & bit) != 0) break;
            }
        }
        for (size_t span = 2; span <= bins; span *= 2)
        {
            const auto step = std::polar (1., -2 * std::numbers::pi / double (span));
            for (size_t start = 0; start < bins; start += span)
            {
                std::complex<double> phase (1);
                for (size_t i = start; i < start + span / 2; ++i)
                {
                    const auto a = data[i], b = phase * data[i + span / 2];
                    data[i] = a + b;
                    data[i + span / 2] = a - b;
                    phase *= step;
                }
            }
        }
        for (size_t i = 0; i < averaged.size(); ++i) averaged[i] += std::norm (data[i]);
    }
    double peak = 0;
    for (size_t i = 12; i < size_t (8000 * bins / rate); ++i)
    {
        std::array<double, 20> neighbours;
        for (size_t j = 0; j < 10; ++j)
        {
            neighbours[j] = averaged[i - 11 + j];
            neighbours[j + 10] = averaged[i + 2 + j];
        }
        std::nth_element (neighbours.begin(), neighbours.begin() + 10, neighbours.end());
        peak = std::max (peak, 10 * std::log10 (std::max (1.e-30, averaged[i]) / std::max (1.e-30, neighbours[10])));
    }
    return peak;
}

void report (const char* name, const Stereo& audio, int sampleRate, size_t bytes)
{
    const auto& l = audio[0];
    const auto& r = audio[1];
    const auto first = std::find_if (l.begin(), l.end(), [] (float sample) { return std::abs (sample) > 1e-6f; });
    const size_t offset = (size_t) (first - l.begin());
    const size_t densityCount = (size_t) (sampleRate / 20);
    const auto end = std::min (l.size(), offset + densityCount);
    const auto count = std::count_if (first, l.begin() + (ptrdiff_t) end, [] (float x) { return std::abs (x) >= 1e-5f; });
    double lr = 0;
    const auto tailStart = (size_t) (sampleRate / 5);
    for (size_t i = tailStart; i < l.size(); ++i) lr += double (l[i]) * r[i];
    const auto corr = lr / std::sqrt (energy (l, tailStart) * energy (r, tailStart));
    auto low = l, high = l;
    double lp = 0, hp = 0;
    const auto loPole = -std::expm1 (-2 * std::numbers::pi * 250 / sampleRate);
    const auto hiPole = -std::expm1 (-2 * std::numbers::pi * 3000 / sampleRate);
    for (size_t i = 0; i < l.size(); ++i)
    {
        lp += loPole * (l[i] - lp); hp += hiPole * (l[i] - hp);
        low[i] = (float) lp; high[i] = l[i] - (float) hp;
    }
    std::printf ("%s,%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%zu\n", name, 1000. * double (offset) / sampleRate,
                 t30 (l, sampleRate), t30 (low, sampleRate), t30 (high, sampleRate),
                 double (count) / double (densityCount), corr, persistentPeak (l, sampleRate), bytes);
}

void testReverb()
{
    constexpr int rate = 48000;
    auto room = factoryRooms.front().room;
    room.preDelayMs = 21;
    std::puts ("mode,first_ms,T30_s,low_T30_s,high_T30_s,occupancy_first_50ms,tail_LR_corr,local_spectral_peak_db,delay_bytes");
    for (int quality = -1; quality < 3; ++quality)
    {
        room.shape.quality = quality >= 0 ? (Quality) quality : Quality::standard;
        const auto plan = makeRoomPlan (room, quality < 0 ? RoomMode::legacy : RoomMode::effect);
        auto setup = plan;
        setup.mix = 1;
        RoomReverb dsp;
        dsp.preparePlan (rate, setup);
        auto audio = signal (rate * 5);
        audio[0][0] = audio[1][0] = .3f;
        dsp.process (audio[0].data(), audio[1].data(), (int) audio[0].size());
        report (quality < 0 ? "Legacy P4" : qualityNames[(size_t) quality], audio, rate, dsp.allocatedBytes());
        require (dsp.activeLines() == (quality == 0 ? 4 : quality == 2 ? 16 : 8), "real quality modes");
        require (energy (audio[0], 0, 1008) == 0, "reverb predelay");
        require (t30 (audio[0], rate) > .25 && energy (audio[0], rate * 4) < 1e-5, "measurable finite decay");
    }
    for (const int deviceRate : { 44100, 48000, 96000 })
    {
        RoomReverb dsp;
        Room extreme;
        extreme.shape.mix = 0;
        dsp.preparePlan (deviceRate, makeRoomPlan (extreme, RoomMode::effect));
        std::array<float, 512> l, r;
        l.fill (.4f); r.fill (-.13f);
        dsp.process (l.data(), r.data(), 512);
        require (l[300] == .4f && r[300] == -.13f, "standalone zero mix preserves dry stereo");
        extreme.shape.mix = 1;
        extreme.bypass = true;
        dsp.preparePlan (deviceRate, makeRoomPlan (extreme, RoomMode::stage));
        dsp.process (l.data(), r.data(), 512);
        require (l[300] == 0 && r[300] == 0, "stage return bypass wet only");
        extreme.bypass = false;
        extreme.width = extreme.length = 80;
        extreme.height = 35;
        extreme.shape.decay = 3;
        extreme.shape.motionDepthMs = 1.3f;
        extreme.shape.motionRate = 1.25f;
        for (int block = 0; block < 300; ++block)
        {
            if (block % 13 == 0)
            {
                extreme.shape.quality = (Quality) ((block / 13) % 3);
                extreme.shape.size = block % 2 == 0 ? .5f : 1.5f;
                extreme.preDelayMs = block % 2 == 0 ? 0 : 100;
                dsp.setPlan (makeRoomPlan (extreme, RoomMode::effect));
            }
            l.fill (block < 10 ? 2.f : 0.f); r.fill (block < 10 ? -1.f : 0.f);
            dsp.process (l.data(), r.data(), 512);
            for (auto value : l) require (std::isfinite (value) && std::abs (value) < 3, "stable quality changes and headroom");
        }
    }
}

void bench()
{
    using namespace std::chrono;
    std::puts ("rate,frames,sources,perspective,quality,wall_us,cpu_us,cpu_period_pct,delay_bytes");
    for (const int rate : { 44100, 48000, 96000 })
        for (const int block : { 64, 128, 256, 512, 1024 })
            for (const int count : { 0, 16, 32, 64, 128 })
                for (const int perspective : { 0, 2, 6 })
                    for (int quality = 0; quality < 3; ++quality)
                    {
                        if (count == 0 && perspective != 0) continue;
                        Room room;
                        room.shape.quality = (Quality) quality;
                        RoomReverb tank;
                        tank.preparePlan (rate, makeRoomPlan (room, RoomMode::stage));
                        std::vector<stage::Renderer> sources ((size_t) count);
                        size_t bytes = tank.allocatedBytes();
                        for (int i = 0; i < count; ++i)
                        {
                            stage::Settings setup;
                            setup.microphoneMix = perspective > 0;
                            setup.microphoneCount = perspective;
                            setup.seat = { float ((i % 13) - 6) / 6, float (i % 9) / 8 };
                            for (int mic = 0; mic < perspective; ++mic)
                            {
                                auto& capsule = setup.microphones[(size_t) mic];
                                capsule.position.x = capsule.pan = float (mic) / perspective - .5f;
                            }
                            require (sources[(size_t) i].prepare (rate, stage::makePlan (rate, setup)), "bench preparation");
                            bytes += sources[(size_t) i].allocatedBytes();
                        }
                        constexpr int repeats = 15;
                        std::vector<float> l ((size_t) block, .013f), r ((size_t) block, .01f), wetL (l), wetR (r);
                        const auto start = steady_clock::now();
                        const auto cpuStart = std::clock();
                        for (int run = 0; run < repeats; ++run)
                        {
                            std::fill (wetL.begin(), wetL.end(), .03f);
                            std::fill (wetR.begin(), wetR.end(), .02f);
                            for (auto& source : sources)
                            {
                                std::fill (l.begin(), l.end(), .013f);
                                std::fill (r.begin(), r.end(), .01f);
                                source.process (l.data(), r.data(), block);
                            }
                            tank.process (wetL.data(), wetR.data(), block);
                        }
                        const auto elapsed = duration<double, std::micro> (steady_clock::now() - start).count() / repeats;
                        const auto cpu = 1.e6 * double (std::clock() - cpuStart) / (double (CLOCKS_PER_SEC) * repeats);
                        std::printf ("%d,%d,%d,%d,%s,%.1f,%.1f,%.2f,%zu\n", rate, block, count, perspective,
                                     qualityNames[(size_t) quality], elapsed, cpu, cpu * rate / (10000. * block), bytes);
                    }
}

void writeWav (const std::string& path, const Stereo& audio, unsigned rate)
{
    std::ofstream stream (path, std::ios::binary);
    const auto word = [&] (uint32_t value, int bytes)
    {
        for (int byte = 0; byte < bytes; ++byte) stream.put (char ((value >> (8 * byte)) & 255u));
    };
    const auto payload = uint32_t (audio[0].size() * 4);
    stream.write ("RIFF", 4); word (36 + payload, 4); stream.write ("WAVEfmt ", 8); word (16, 4);
    word (1, 2); word (2, 2); word (rate, 4); word (4 * rate, 4); word (4, 2); word (16, 2);
    stream.write ("data", 4); word (payload, 4);
    for (size_t i = 0; i < audio[0].size(); ++i)
        for (const auto& ch : audio)
            word ((uint16_t) (int16_t) std::lround (std::clamp (ch[i], -.999f, .999f) * 32767), 2);
    require (bool (stream), "write listening WAV");
}

void listening (std::string directory)
{
    constexpr unsigned rate = 44100;
    auto dry = signal (rate * 6);
    for (size_t i = 0; i < dry[0].size(); ++i)
    {
        const double t = double (i) / rate;
        for (int note = 0; note < 9; ++note)
        {
            const double age = t - .26 * note;
            if (age < 0 || age > .9) continue;
            const auto f = 196 * std::exp2 (double (note * 7 % 19) / 12);
            const auto env = std::min (age * 240, 1.) * std::exp (-6 * age) * std::min (1., (.9 - age) * 23);
            for (int side = 0; side < 2; ++side)
                dry[(size_t) side][i] += float (.09 * env * (std::sin (2 * std::numbers::pi * f * age)
                    + .4 * std::sin (2 * std::numbers::pi * (2.01 + .003 * side) * f * age)));
        }
    }
    writeWav (directory + "/01-Dry.wav", dry, rate);
    for (int mode = 0; mode < 4; ++mode)
    {
        auto audio = dry;
        auto room = factoryRooms.front().room;
        auto plan = makeRoomPlan (room, mode == 0 ? RoomMode::legacy : RoomMode::effect);
        if (mode == 0) plan.standalone = 1, plan.mix = room.shape.mix;
        if (mode >= 2)
        {
            stage::Settings setup;
            setup.room = room;
            setup.seat = { -.62f, .72f };
            setup.microphoneMix = mode == 3;
            setup.microphoneCount = 2;
            setup.microphones[0].position.x = -.02f;
            setup.microphones[1].position.x = .02f;
            setup.microphones[0].pan = -.85f;
            setup.microphones[1].pan = .85f;
            setup.microphones[0].azimuth = -55;
            setup.microphones[1].azimuth = 55;
            stage::Renderer render;
            require (render.prepare (rate, stage::makePlan (rate, setup)), "listening renderer");
            render.process (audio[0].data(), audio[1].data(), (int) audio[0].size());
            plan = makeRoomPlan (room, RoomMode::stage);
            auto wet = dry;
            RoomReverb dsp;
            dsp.preparePlan (rate, plan);
            dsp.process (wet[0].data(), wet[1].data(), (int) wet[0].size());
            for (size_t i = 0; i < audio[0].size(); ++i)
                for (size_t ch = 0; ch < 2; ++ch) audio[ch][i] += .2f * wet[ch][i];
        }
        else
        {
            RoomReverb dsp;
            dsp.preparePlan (rate, plan);
            dsp.process (audio[0].data(), audio[1].data(), (int) audio[0].size());
        }
        constexpr std::array names { "/02-P4-Wood-Hall.wav", "/03-Studio-Reverb-Wood-Hall.wav",
            "/04-Stage-Listener.wav", "/05-Stage-ORTF.wav" };
        writeWav (directory + names[(size_t) mode], audio, rate);
    }
}
}

int main (int argc, char** argv)
{
    if (argc == 2 && std::string_view (argv[1]) == "--bench") { bench(); return 0; }
    if (argc == 3 && std::string_view (argv[1]) == "--listening") { listening (argv[2]); return 0; }
    testStage();
    testReverb();
}
