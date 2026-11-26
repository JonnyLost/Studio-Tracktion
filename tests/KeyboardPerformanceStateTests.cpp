// SPDX-License-Identifier: GPL-3.0-or-later
#include "../StudioKeyboardPerformanceState.h"
#include <cassert>

int main()
{
    using Values = studio::KeyboardPerformanceValues;
    studio::KeyboardPerformanceState state;
    const auto initial = state.current();
    assert (initial.changed == 0 && initial.bend == 8192 && initial.mod == 0);
    assert (initial.expr == 127 && initial.vol == 100);
    assert (initial.tone == 64 && initial.reverb == 40);
    assert (state.consume().changed == 0);

    state.receiveController (1, 54);
    state.receiveController (11, 88);
    state.receiveController (7, 74);
    state.receiveController (74, 106);
    state.receiveController (91, 29);
    state.receivePitchBend (12000);
    const auto changed = state.consume();
    const auto allBits = Values::bit (Values::pitch)
        | Values::bit (Values::modulation)
        | Values::bit (Values::expression)
        | Values::bit (Values::volume)
        | Values::bit (Values::brightness)
        | Values::bit (Values::room);
    assert (changed.changed == allBits);
    assert (changed.bend == 12000 && changed.mod == 54);
    assert (changed.expr == 88 && changed.vol == 74);
    assert (changed.tone == 106 && changed.reverb == 29);
    assert (state.consume().changed == 0); // do not re-emit stale changes
    assert (state.current().mod == 54); // value persists between messages

    state.receiveController (10, 99);
    assert (state.consume().changed == 0); // unrelated MIDI CC stays untouched
    state.receivePitchBend (-400);
    assert (state.consume().bend == 0);
    state.receivePitchBend (20000);
    assert (state.consume().bend == 16383);
    state.receiveController (1, -3);
    assert (state.consume().mod == 0);
    state.receiveController (1, 200);
    assert (state.consume().mod == 127);
    state.reset();
    const auto reset = state.current();
    assert (reset.changed == 0 && reset.bend == 8192 && reset.mod == 0);
    assert (reset.expr == 127 && reset.vol == 100);
}
