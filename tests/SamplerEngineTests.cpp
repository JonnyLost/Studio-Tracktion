// SPDX-License-Identifier: GPL-3.0-or-later
#include "../SamplerEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numbers>
#include <string_view>

using namespace studio::sampler;

namespace
{
void require (bool passed, std::string_view description)
{
    if (! passed)
    {
        std::cerr << description << '\n';
        std::exit (1);
    }
}

std::shared_ptr<Sample> tone (float gain = 1, double rate = 48000, bool sine = false)
{
    auto sample = std::make_shared<Sample>();
    sample->rate = rate;
    sample->left.resize (24000);
    for (std::size_t i = 0; i < sample->left.size(); ++i)
        sample->left[i] = sine ? gain * std::sin (2.0 * std::numbers::pi * 440 * i / rate) : gain;
    return sample;
}

struct Harness
{
    Engine engine;
    Bank bank;
    Harness()
    {
        engine.prepare (48000);
        bank.settings = { .attack = 0, .decay = 0, .sustain = 1, .release = .01f, .gainDb = 0 };
        bank.zones = { { .sample = tone() } };
    }

    void on (int note = 60, int velocity = 127, int channel = 0) { engine.midi (0x90 | channel, note, velocity); }
    void off (int note = 60, int channel = 0) { engine.midi (0x80 | channel, note, 0); }
    void cc (int control, int value, int channel = 0) { engine.midi (0xB0 | channel, control, value); }
    void use() { engine.use (bank); }
    std::vector<float> render (int frames)
    {
        std::vector<float> out (static_cast<std::size_t> (frames));
        engine.render (out.data(), nullptr, frames);
        engine.publishMeters();
        require (std::ranges::all_of (out, [] (float n) { return std::isfinite (n); }), "finite audio");
        use();
        return out;
    }
};

double frequency (const std::vector<float>& samples)
{
    int positiveCrossings = 0;
    for (std::size_t i = 1; i < samples.size(); ++i)
        if (samples[i - 1] < 0 && samples[i] >= 0) ++positiveCrossings;
    return 48000.0 * positiveCrossings / samples.size();
}

void notesAndExpression()
{
    Harness h;
    h.use();
    h.on();
    require (h.render (32).back() == 1 && h.engine.noteActive (60), "single note and key meter");
    h.on (64, 64, 4);
    require (std::abs (h.render (32).back() - (1 + 64.0 / 127)) < .0001 && h.engine.activeVoices() == 2,
             "polyphony and velocity");
    h.cc (7, 64, 4);
    h.cc (11, 64, 4);
    require (std::abs (h.render (32).back() - (1 + std::pow (64.0 / 127, 3))) < .0001,
             "channel volume and expression");
    h.off (60, 4);
    require (h.engine.noteActive (60), "other MIDI channel cannot release note");
    h.cc (64, 127);
    h.off();
    require (h.render (600).back() > 1, "sustain holds released note");
    h.cc (64, 0);
    auto release = h.render (640);
    require (release.front() > 1 && release.back() < .2 && h.engine.activeVoices() == 1, "pedal up releases");
    h.cc (123, 0, 4);
    require (h.render (640).back() == 0, "channel all notes off");
    h.on();
    h.engine.panic();
    require (h.render (32).back() == 0 && h.engine.activeVoices() == 0, "transport panic");
}

void envelopeAndPitch()
{
    Harness h;
    h.bank.settings.attack = .01f;
    h.bank.settings.decay = .01f;
    h.bank.settings.sustain = .5f;
    h.use();
    h.on();
    auto env = h.render (1100);
    require (env[0] < .01 && env[230] > .4 && env[230] < .6 && env[480] > .98
             && std::abs (env.back() - .5f) < .0001f, "ADSR attack, decay, sustain");
    h.off();
    auto release = h.render (640);
    require (release[0] > .49 && release[230] > .2 && release[230] < .3 && release.back() == 0,
             "ADSR release");

    ++h.bank.sampleRevision;
    h.bank.settings.attack = h.bank.settings.decay = 0;
    h.bank.zones[0].sample = tone (1, 24000, true);
    h.use();
    const auto measure = [&] (int note, bool bend)
    {
        h.engine.panic();
        if (bend) h.engine.midi (0xe0, 127, 127);
        h.on (note);
        return frequency (h.render (4800));
    };
    require (std::abs (measure (60, false) - 440) <= 10, "native root and 24 to 48 kHz conversion");
    require (std::abs (measure (72, false) - 880) <= 10, "octave transposition");
    require (std::abs (measure (60, true) - 493.88) <= 10, "two semitone pitch bend");

    auto stereo = tone (.3f);
    stereo->right.assign (stereo->left.size(), -.4f);
    h.bank.zones[0].sample = std::move (stereo);
    ++h.bank.sampleRevision;
    h.use();
    h.on();
    float left = 0, right = 0;
    h.engine.render (&left, &right, 1);
    require (left > .14f && right < -.19f, "stereo channel separation");
}

void mappingAndStealing()
{
    Harness h;
    h.bank.zones = { { .low = 40, .high = 60, .soft = 10, .hard = 80, .sample = tone (.25f) },
                     { .low = 60, .high = 76, .soft = 80, .hard = 100, .sample = tone (.5f) } };
    h.use();
    const auto level = [&] (int note, int velocity)
    {
        h.engine.panic();
        h.on (note, velocity);
        return h.render (32).back();
    };
    require (level (39, 80) == 0 && level (40, 9) == 0 && level (40, 10) > 0, "lower key and velocity boundaries");
    require (level (76, 100) > 0 && level (77, 100) == 0 && level (76, 101) == 0, "upper key and velocity boundaries");
    require (std::abs (level (60, 80) - .75 * 80 / 127) < .0001, "overlapping zones layer");
    for (auto& z : h.bank.zones) z.group = 1;
    const auto first = level (60, 80);
    h.off();
    h.render (600);
    h.on (60, 80);
    require (std::abs (h.render (32).back() - 2 * first) < .0001, "round robin cycles matching zones");
    auto invalid = h.bank.zones.front();
    invalid.low = 100;
    require (! validMapping (invalid), "reject reversed key range");
    invalid.low = 40;
    invalid.soft = 0;
    require (! validMapping (invalid), "reject velocity zero");
    invalid.loopEnd = std::numeric_limits<int>::min();
    require (! validLoop (invalid, 32), "reject overflowing loop range");

    h.bank.zones = { { .sample = tone() } };
    ++h.bank.sampleRevision;
    h.bank.settings.polyphony = 2;
    h.use();
    h.on (60);
    h.on (61);
    h.off (61);
    h.on (62);
    h.render (8);
    require (h.engine.noteActive (60) && h.engine.noteActive (62), "steal releasing voice first");
    h.on (63);
    h.render (8);
    require (! h.engine.noteActive (60) && h.engine.noteActive (62) && h.engine.noteActive (63)
             && h.engine.activeVoices() == 2, "then steal oldest active voice");
}

void loopsAndOneShots()
{
    Harness h;
    auto shortSample = tone();
    shortSample->left.resize (200);
    h.bank.zones = { { .loop = true, .loopStart = 30, .loopEnd = 99, .sample = shortSample } };
    h.use();
    h.on (120);
    require (h.render (1800).back() == 1, "loop wraps even when playback crosses more than one frame per output");
    h.off (120);
    require (h.render (640).back() == 0, "loop uses release envelope");
    ++h.bank.sampleRevision;
    h.bank.zones[0].oneShot = true;
    h.use();
    h.on();
    h.off();
    const auto shot = h.render (240);
    require (shot[100] == 1 && shot[190] > 0 && shot.back() == 0, "one shot ignores note off and loop, reaches sample end");
    h.on();
    h.cc (120, 0);
    require (h.render (32).back() == 0, "all sound off silences a one shot");
}
}

int main()
{
    notesAndExpression();
    envelopeAndPitch();
    mappingAndStealing();
    loopsAndOneShots();
    {
        Harness h;
        auto sample = tone();
        std::fill (sample->left.begin(), sample->left.begin() + 100, 0.0f);
        Zone z; z.sample = sample; z.start = 100; z.end = 300;
        h.bank.zones = { z }; h.use(); h.on();
        const auto audio = h.render (400);
        require (audio[50] > 0 && audio[250] == 0, "trim begins at selected frame and stops at exclusive end");
    }
    std::cout << "Sampler playback, expression, mapping and offline render passed.\n";
}
