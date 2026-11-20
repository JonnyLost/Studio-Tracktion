// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpatialReverb.h"

#include <array>
#include <chrono>
#include <complex>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <numeric>
#include <string_view>

namespace
{
using namespace studio::spatial;
constexpr float pi = std::numbers::pi_v<float>;
struct Stereo { std::vector<float> left, right; };

Stereo render (Stereo input, Room room, int rate)
{
    RoomReverb reverb;
    reverb.prepare (rate, room);
    for (size_t i = 0; i < input.left.size(); i += 256)
        reverb.process (input.left.data() + i, input.right.data() + i, (int) std::min<size_t> (256, input.left.size() - i));
    return input;
}

double correlation (const std::vector<float>& a, const std::vector<float>& b, int first, int last, int lag = 0)
{
    double aa = 0, bb = 0, ab = 0;
    for (int i = first; i < last; i += 4)
    {
        const double x = a[(size_t) i], y = b[(size_t) (i + lag)];
        aa += x * x; bb += y * y; ab += x * y;
    }
    return aa > 0 && bb > 0 ? ab / std::sqrt (aa * bb) : 0;
}

double density (const std::vector<float>& x, int rate, double at)
{
    const auto start = x.begin() + (int) ((at - 0.015) * rate);
    const auto end = x.begin() + (int) ((at + 0.015) * rate);
    const double rms = std::sqrt (std::inner_product (start, end, start, 0.0) / (end - start));
    if (rms == 0) return 0;
    const auto count = std::count_if (start, end, [rms] (float n) { return std::abs (n) > rms; });
    return (double) count / (end - start) / 0.3173105;
}

double octaveT20 (const std::vector<float>& x, int rate, float center)
{
    // Two cascaded, one-octave bandpass sections. Integrate the filtered
    // squared IR backwards and extrapolate the -5 to -25 dB slope to -60 dB.
    const double phase = 2.0 * pi * center / rate;
    const double a = std::sin (phase) / (2.0 * std::sqrt (2.0));
    const double b0 = a / (1.0 + a), a1 = -2 * std::cos (phase) / (1 + a), a2 = (1 - a) / (1 + a);
    std::array<double, 2> z1 {}, z2 {};
    std::vector<double> integrated (x.size());
    for (size_t i = 0; i < x.size(); ++i)
    {
        double sample = x[i];
        for (size_t order = 0; order < z1.size(); ++order)
        {
            const double y = sample * b0 + z1[order];
            z1[order] = -a1 * y + z2[order];
            z2[order] = -sample * b0 - a2 * y;
            sample = y;
        }
        integrated[i] = sample * sample;
    }
    std::partial_sum (integrated.rbegin(), integrated.rend(), integrated.rbegin());
    if (integrated.front() == 0) return 0;
    const auto at = [&] (double db)
    {
        return std::lower_bound (integrated.begin(), integrated.end(), integrated.front() * std::pow (10.0, db / 10.0),
                                std::greater<double>()) - integrated.begin();
    };
    return 3.0 * (at (-25) - at (-5)) / rate;
}

double spectralPeak (const std::vector<float>& x, int rate)
{
    // 16k-point, Hann-windowed late spectrum around 90–430 ms at 48 kHz.
    // Compare each bin with the local mean power across ±1/8 octave.
    constexpr size_t bins = 16384;
    std::vector<std::complex<double>> fft (bins);
    for (size_t i = 0, j = 0; i < bins; ++i)
    {
        const auto t = (size_t) (0.09 * rate) + i * (size_t) rate / 48000;
        fft[j] = (double) x[t] * (0.5 - 0.5 * std::cos (2.0 * pi * i / (bins - 1)));
        size_t bit = bins / 2;
        while (bit && (j & bit)) { j ^= bit; bit >>= 1; }
        j ^= bit;
    }
    for (size_t width = 2; width <= bins; width *= 2)
    {
        const auto step = std::polar (1.0, -2.0 * pi / width);
        for (size_t start = 0; start < bins; start += width)
        {
            std::complex<double> phase (1);
            for (size_t i = 0; i < width / 2; ++i)
            {
                const auto j = start + i, k = j + width / 2;
                const auto a = fft[j], b = phase * fft[k];
                fft[j] = a + b; fft[k] = a - b; phase *= step;
            }
        }
    }
    std::vector<double> db;
    for (size_t bin = 60; bin <= 2048; ++bin)
    {
        const auto radius = std::max<size_t> (8, bin / 12);
        double mean = 0.0;
        for (size_t i = bin - radius; i <= bin + radius; ++i) mean += std::norm (fft[i]);
        db.push_back (10.0 * std::log10 ((std::norm (fft[bin]) + 1e-24) / (mean / (2 * radius + 1) + 1e-24)));
    }
    std::sort (db.begin(), db.end());
    return db[db.size() * 99 / 100];
}

Stereo source (int rate, int kind)
{
    const auto frames = (size_t) rate * 6;
    Stereo s { std::vector<float> (frames), std::vector<float> (frames) };
    s.left[0] = s.right[0] = kind == 0 ? 0.3f : 0.0f;
    for (size_t i = 0; kind != 0 && i < frames; ++i)
    {
        const double time = (double) i / rate;
        double sample = 0;
        if (kind == 1)
        {
            for (int note = 0; note < 4; ++note)
            {
                const double t = time - 0.4 * note;
                if (t < 0 || t >= 0.55) continue;
                const double phase = 2.0 * pi * 220 * std::pow (2.0, note / 4.0) * t;
                sample += 0.15 * std::min (1.0, t * 800) * std::min (1.0, (0.55 - t) * 80) * std::exp (-7 * t)
                    * (std::sin (phase) + 0.35 * std::sin (2 * phase) + 0.15 * std::sin (4 * phase));
            }
        }
        else if (time < 2.2)
        {
            const double phase = 2.0 * pi * time;
            const double envelope = std::min ({ 1.0, time * 2, (2.2 - time) * 12 });
            sample = 0.09 * envelope * (std::sin (phase * 196) + std::sin (phase * 293.7) + 0.7 * std::sin (phase * 395));
        }
        s.left[i] = s.right[i] = (float) sample;
    }
    return s;
}

void writeWave (const std::string& path, const Stereo& wet, const Stereo* dry, int rate)
{
    std::ofstream file (path, std::ios::binary);
    const auto write = [&] (uint32_t n, int bytes)
    {
        for (int i = 0; i < bytes; ++i) file.put ((char) ((n >> (8 * i)) & 255));
    };
    const auto bytes = (uint32_t) wet.left.size() * 4;
    file << "RIFF"; write (36 + bytes, 4); file << "WAVEfmt "; write (16, 4);
    write (1, 2); write (2, 2); write ((uint32_t) rate, 4); write ((uint32_t) rate * 4, 4);
    write (4, 2); write (16, 2); file << "data"; write (bytes, 4);
    for (size_t i = 0; i < wet.left.size(); ++i)
        for (int ch = 0; ch < 2; ++ch)
        {
            const float wetSample = ch == 0 ? wet.left[i] : wet.right[i];
            const float direct = dry == nullptr ? 0.0f : ch == 0 ? dry->left[i] : dry->right[i];
            write ((uint16_t) (int16_t) std::lround (32767 * std::clamp (direct + wetSample, -1.0f, 1.0f)), 2);
        }
    if (! file) throw std::runtime_error ("Writing " + path);
}

struct Metrics
{
    double density80, density150, density300, repeat, prominence, stereo;
    double t125, t500, t2000, t4000, wetEnergy;
};

Metrics measure (const Stereo& ir, int rate)
{
    double repeat = 0;
    for (int lag = rate * 21 / 1000; lag < rate * 85 / 1000; lag += 2)
        repeat = std::max (repeat, std::abs (correlation (ir.left, ir.left, rate / 8, rate / 2, lag)));
    const double leftPower = std::inner_product (ir.left.begin(), ir.left.end(), ir.left.begin(), 0.0);
    return { density (ir.left, rate, .08), density (ir.left, rate, .15), density (ir.left, rate, .30), repeat,
             spectralPeak (ir.left, rate), correlation (ir.left, ir.right, rate / 10, rate),
             octaveT20 (ir.left, rate, 125), octaveT20 (ir.left, rate, 500), octaveT20 (ir.left, rate, 2000),
             octaveT20 (ir.left, rate, 4000), 10.0 * std::log10 (leftPower / .09) };
}

void analyse (int rate, size_t index, const std::string& prefix)
{
    const auto room = factoryRooms[index].room;
    auto ir = render (source (rate, 0), room, rate);
    const auto m = measure (ir, rate);
    std::cout << rate << ',' << factoryRooms[index].name << ',' << m.density80 << ','
        << m.density150 << ',' << m.density300 << ',' << m.repeat << ',' << m.prominence << ','
        << m.stereo << ',' << m.t125 << ',' << m.t500 << ',' << m.t2000 << ',' << m.t4000 << ',' << m.wetEnergy << '\n';
    if (prefix.empty() || rate != 48000) return;
    const auto base = prefix + (index == 0 ? "-hall" : index == 1 ? "-chamber" : index == 3 ? "-scoring" : "-nave");
    writeWave (base + "-impulse-wet.wav", ir, nullptr, rate);
    for (int kind : { 1, 2 })
    {
        const auto dry = source (rate, kind);
        auto send = dry;
        for (auto* side : { &send.left, &send.right })
            for (float& sample : *side) sample *= 0.19952623f; // Rear, -14 dB Stage send.
        writeWave (base + (kind == 1 ? "-piano.wav" : "-sustained.wav"),
                   render (std::move (send), room, rate), &dry, rate);
    }
}

void benchmark (int rate)
{
    using Clock = std::chrono::steady_clock;
    constexpr int frames = 256, blocks = 2048;
    std::array<float, frames> input, l, r;
    for (int i = 0; i < frames; ++i) input[(size_t) i] = 0.1f * std::sin (i * 0.07f);
    RoomReverb room;
    room.prepare (rate, {});
    double checksum = 0;
    const auto budget = [&] (bool driven)
    {
        std::array<double, 5> times {};
        for (auto& time : times)
        {
            const auto start = Clock::now();
            for (int i = 0; i < blocks; ++i)
            {
                if (driven) l = r = input;
                else { l.fill (0); r.fill (0); }
                room.process (l.data(), r.data(), frames);
                checksum += l[(size_t) i % frames] + r[(size_t) ((i * 3) % frames)];
            }
            time = std::chrono::duration<double> (Clock::now() - start).count();
        }
        std::sort (times.begin(), times.end());
        return 100 * times[2] * rate / (frames * blocks);
    };
    const auto active = budget (true);
    room.reset();
    const auto idle = budget (false);
    std::cout << "CPU," << rate << ',' << active << ',' << idle << ',' << room.allocatedBytes() << ',' << checksum << '\n';
}

bool verify()
{
    bool passed = true;
    for (int rate : { 44100, 48000, 96000 })
    {
        std::array<double, 4> decay {};
        int i = 0;
        for (size_t room : { 0, 1, 3, 5 })
        {
            auto ir = render (source (rate, 0), factoryRooms[room].room, rate);
            const auto m = measure (ir, rate);
            decay[(size_t) i++] = m.t500;
            if (! (m.density150 > .72 && m.density300 > .86 && m.repeat < .29 && m.prominence < 8.0
                   && std::abs (m.stereo) < .20 && m.wetEnergy > -12 && m.wetEnergy < 1
                   && m.t125 > m.t500 && m.t500 > m.t2000 && m.t2000 > m.t4000))
            {
                std::cerr << "Acoustic bounds failed at " << rate << " Hz for " << factoryRooms[room].name << '\n';
                passed = false;
            }
        }
        if (! (decay[2] < .75 * decay[0] && decay[0] < .75 * decay[3]))
        {
            std::cerr << "Scoring, Hall and Nave decay separation failed at " << rate << " Hz\n";
            passed = false;
        }
    }
    return passed;
}
}

int main (int argc, char** argv)
{
    if (argc == 2 && std::string_view (argv[1]) == "--verify") return verify() ? 0 : 1;
    const std::string output = argc == 2 ? argv[1] : "";
    std::cout << std::fixed << std::setprecision (3)
              << "Hz,preset,NED_80ms,NED_150ms,NED_300ms,repeat_21_to_85ms,spectral_P99_dB,late_LR,T20_125Hz,T20_500Hz,T20_2kHz,T20_4kHz,IR_energy_dB\n";
    for (int rate : { 44100, 48000, 96000 })
        for (size_t room : { 0, 1, 3, 5 }) analyse (rate, room, output);
    std::cout << "CPU,Hz,single_core_active_percent,single_core_idle_percent,delay_bytes,checksum\n";
    for (int rate : { 44100, 48000, 96000 }) benchmark (rate);
}
