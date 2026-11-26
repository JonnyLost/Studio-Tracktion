// SPDX-License-Identifier: GPL-3.0-or-later
#include "../StudioGeneralMidi.h"
#include <cassert>
#include <cstring>

int main()
{
    static_assert (studio::gm::names.size() == 128);
    static_assert (studio::gm::families.size() == 16);
    static_assert (studio::gm::valid (0) && studio::gm::valid (127));
    static_assert (! studio::gm::valid (-1) && ! studio::gm::valid (128));
    static_assert (studio::gm::displayNumber (0) == 1);
    static_assert (studio::gm::displayNumber (127) == 128);

    assert (std::strcmp (studio::gm::name (0), "Acoustic Grand Piano") == 0);
    assert (std::strcmp (studio::gm::name (40), "Violin") == 0);
    assert (std::strcmp (studio::gm::name (56), "Trumpet") == 0);
    assert (std::strcmp (studio::gm::name (73), "Flute") == 0);
    assert (std::strcmp (studio::gm::name (127), "Gunshot") == 0);
    assert (std::strcmp (studio::gm::family (40), "Orchestral Strings") == 0);
    assert (std::strcmp (studio::gm::family (56), "Brass") == 0);
    assert (std::strcmp (studio::gm::family (112), "Percussion") == 0);

    for (int i = 0; i < 128; ++i)
    {
        assert (studio::gm::name (i)[0] != '\0');
        assert (studio::gm::family (i)[0] != '\0');
        assert (studio::gm::displayNumber (i) == i + 1);
    }
    assert (std::strcmp (studio::gm::name (128), "") == 0);

    // Regression for M18R: selected program bytes must be sent through the
    // audio graph on every melodic channel, in the proper order. No drum
    // channel is modified and neutral pitch is the exact 14-bit midpoint.
    for (int program = 0; program < 128; ++program)
    {
        std::array<studio::gm::SelectionMessage, 90> packets {};
        int count = 0;
        assert (studio::gm::emitSelection (program, [&] (auto packet)
        {
            assert (count < static_cast<int> (packets.size()));
            packets[static_cast<unsigned> (count++)] = packet;
        }));
        assert (count == 90);
        for (int group = 0; group < 15; ++group)
        {
            const int channel = group < 9 ? group : group + 1;
            const auto* p = packets.data() + group * 6;
            assert (p[0].status == (0xb0 | channel) && p[0].data1 == 64 && p[0].data2 == 0 && p[0].size == 3);
            assert (p[1].status == (0xb0 | channel) && p[1].data1 == 123 && p[1].data2 == 0 && p[1].size == 3);
            assert (p[2].status == (0xe0 | channel) && p[2].data1 == 0 && p[2].data2 == 64 && p[2].size == 3);
            assert (p[3].status == (0xb0 | channel) && p[3].data1 == 0 && p[3].data2 == 0 && p[3].size == 3);
            assert (p[4].status == (0xb0 | channel) && p[4].data1 == 32 && p[4].data2 == 0 && p[4].size == 3);
            assert (p[5].status == (0xc0 | channel) && p[5].data1 == program && p[5].size == 2);
        }
    }
    int invalidPackets = 0;
    assert (! studio::gm::emitSelection (-1, [&] (auto) { ++invalidPackets; }));
    assert (! studio::gm::emitSelection (128, [&] (auto) { ++invalidPackets; }));
    assert (invalidPackets == 0);
}
