// SPDX-License-Identifier: GPL-3.0-or-later
#include "../SamplerEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
using namespace studio::sampler;
namespace { bool guard = false; unsigned allocations = 0; }
void* operator new (std::size_t bytes) { if (guard) ++allocations; if (auto* p = std::malloc (bytes ? bytes : 1)) return p; throw std::bad_alloc(); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void check (bool ok, const char* message) { if (! ok) { std::cerr << message << '\n'; std::exit (1); } }
std::shared_ptr<Sample> sample (float value)
{
    auto p = std::make_shared<Sample>(); p->rate = 48000; p->left.assign (48000, value); return p;
}
struct Rig
{
    Engine e; Bank b; float audio[2048] {};
    Rig() { e.prepare (48000); b.settings.attack = b.settings.decay = b.settings.release = 0; b.settings.gainDb = 0; b.sampleRevision = 1; }
    void use() { e.use (b); }
    float render (int count = 64) { std::fill_n (audio, count, 0); e.render (audio, nullptr, count); e.publishMeters(); use(); return audio[count - 1]; }
    void on (int note = 60, int velocity = 127, int ch = 0) { e.midi (0x90 | ch, note, velocity); }
    void off (int note = 60, int ch = 0) { e.midi (0x80 | ch, note, 0); }
    void cc (int number, int value, int ch = 0) { e.midi (0xb0 | ch, number, value); }
};
int main()
{
    static_assert (std::atomic<std::uint64_t>::is_always_lock_free);
    {
        Rig tail; tail.b.groups.push_back ({}); Zone z; z.sample = sample (1); tail.b.zones = { z }; tail.b.settings.release = 1;
        tail.use(); tail.on(); tail.render(); tail.off(); check (tail.render() > .9f, "long release is audible");
        tail.b.groups[0].mute = true; tail.use(); check (tail.render() == 0, "mute fades voices already in release");
    }
    Rig r;
    for (int i = 0; i < 3; ++i)
    {
        Group g; g.id = i; g.trigger = GroupTrigger::keyswitch; g.key = 24 + i;
        r.b.groups.push_back (g);
        Zone z; z.folder = i; z.sample = sample (float (i + 1) / 4); r.b.zones.push_back (z);
    }
    r.use();
    r.on(); check (r.render() == .25f, "first keyswitch group is the default"); r.off(); r.render();
    r.on (25); check (r.render() == 0, "keyswitch cannot sound even inside mapped key range");
    r.off (25); r.on(); check (r.render() == .5f, "latched staccato after switch note-off");
    r.off(); r.render(); r.on (26, 127, 1); r.on (60, 127, 1);
    check (r.render() == .75f, "per-channel articulation selection"); r.off (60, 1); r.render();
    r.b.groups[1].mute = true; r.use(); r.on(); check (r.render() == 0, "muted selected articulation is silent");
    r.b.groups[1].mute = false; r.b.groups[0].solo = true; r.use(); r.on(); check (r.render() == 0, "solo excludes other groups");
    r.on (24); r.on(); check (r.render() == .25f, "solo group plays"); r.off(); r.render();
    r.b.groups[0].solo = false;
    for (auto& g : r.b.groups) { g.trigger = GroupTrigger::controller; g.controller = 20; g.ccLow = g.id * 40; g.ccHigh = g.id == 2 ? 127 : g.ccLow + 39; }
    r.use(); r.cc (20, 90); r.on(); check (r.render() == .75f, "CC value range selects group"); r.off(); r.render();
    r.cc (20, 12); r.on(); check (r.render() == .25f, "CC switching changes next strike"); r.off(); r.render();
    // Release trigger follows the original strike even if articulation changes while held.
    auto release = r.b.groups[0]; release.id = 3; release.releaseTrigger = true; r.b.groups.push_back (release);
    Zone releaseZone; releaseZone.folder = 3; releaseZone.sample = sample (.125f); r.b.zones.push_back (releaseZone);
    ++r.b.sampleRevision; r.use(); r.cc (20, 12); r.on(); r.render(); r.cc (20, 90); r.off();
    check (r.render() == .125f, "release uses original group activation");
    r.e.panic(); r.cc (20, 12); r.cc (64, 127); r.on(); r.render(); r.off();
    check (r.render() == .25f, "pedal delays release sample"); r.cc (64, 0); check (r.render() == .125f, "pedal lift fires release sample");
    r.e.panic(); r.on(); r.render(); r.cc (120, 0); check (r.render() == 0, "all sound off clears release history"); r.off(); check (r.render() == 0, "no release resurrection after panic");
    // Complementary linear velocity ramps sum to one across their overlap.
    Zone soft, loud; soft.soft = 1; soft.hard = 95; soft.fadeOut = 32; loud.soft = 64; loud.hard = 127; loud.fadeIn = 32;
    for (int v = 64; v <= 95; ++v) check (std::abs (velocityWeight (soft, v) + velocityWeight (loud, v) - 1) < .00001f, "complementary velocity crossfade");
    r.b.groups.clear(); soft.sample = sample (1); loud.sample = soft.sample; r.b.zones = { soft, loud }; ++r.b.sampleRevision; r.use();
    r.on (60, 80); check (std::abs (r.render() - 80.0f / 127) < .00001f, "crossfade affects rendered audio");
    r.e.panic(); r.e.preview (3, 0, 60, 100, 0); check (r.render() > 0, "selected-zone audition bypasses velocity limits");
    // Adjustable loop blend produces a real, measurable change in the tail.
    auto wave = sample (0); std::fill (wave->left.begin() + 100, wave->left.begin() + 200, 1);
    Zone loop; loop.sample = wave; loop.loop = true; loop.loopStart = 0; loop.loopEnd = 200; loop.loopCrossfade = 0;
    r.e.panic(); r.b.zones = { loop }; ++r.b.sampleRevision; r.use(); r.on(); const float hard = r.render (190);
    r.e.panic(); r.b.zones[0].loopCrossfade = 40; ++r.b.sampleRevision; r.use(); r.on(); const float blended = r.render (190);
    check (hard == 1 && blended < .5f, "loop crossfade has an audible effect");
    // Exercise publication, controllers, note/release triggers and rendering while global new is guarded.
    r.e.panic(); r.b.zones.assign (maxZones, loop); r.b.settings.polyphony = maxVoices;
    Group attackGroup; attackGroup.trigger = GroupTrigger::keyswitch; attackGroup.key = 24;
    Group releaseGroup; releaseGroup.id = 1; releaseGroup.releaseTrigger = true;
    r.b.groups = { attackGroup, releaseGroup };
    for (int i = 64; i < maxZones; ++i) r.b.zones[static_cast<std::size_t> (i)].folder = 1;
    r.use();
    guard = true;
    for (int i = 0; i < 100; ++i) { r.use(); r.on (24); r.on (60 + i % 12); r.cc (1, i % 128); r.render(); r.off (60 + i % 12); }
    r.e.panic(); guard = false;
    check (allocations == 0, "audio operations must not allocate");
    std::cout << "Keyswitch/CC groups, mute/solo, releases, crossfades and allocation-free rendering passed.\n";
}
