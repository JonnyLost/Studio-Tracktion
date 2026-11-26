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
}
