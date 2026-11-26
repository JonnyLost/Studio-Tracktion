// SPDX-License-Identifier: GPL-3.0-or-later
#include "../StudioKeyboardNoteState.h"
#include <cassert>

int main()
{
    studio::KeyboardNoteState state;
    assert (! state.snapshot().any());

    state.noteOn (1, 60);
    assert (state.snapshot().test (60));
    state.noteOff (1, 60);
    assert (state.snapshot().test (60)); // short notes remain visible
    assert (! state.snapshot().test (60)); // flash expires

    // Repeated notes on the same pitch are counted, not prematurely released.
    state.noteOn (1, 60);
    state.noteOn (1, 60);
    state.noteOff (1, 60);
    state.snapshot(); state.snapshot();
    assert (state.snapshot().test (60));
    state.noteOff (1, 60);
    state.snapshot(); state.snapshot();
    assert (! state.snapshot().test (60));

    // Sustain keeps released notes lit until pedal-up, by channel.
    state.noteOn (1, 64);
    state.sustain (1, true);
    state.noteOff (1, 64);
    state.snapshot(); state.snapshot();
    assert (state.snapshot().test (64));
    state.sustain (1, false);
    assert (! state.snapshot().test (64));

    state.noteOn (2, 67);
    state.snapshot(); state.snapshot();
    assert (state.snapshot().test (67));
    state.allNotesOff (2);
    assert (! state.snapshot().test (67));
    state.noteOn (1, 72);
    state.noteOn (2, 75);
    state.clear();
    assert (! state.snapshot().any());

    // Invalid MIDI channels and pitches must never access an array.
    state.noteOn (0, 64);
    state.noteOn (17, 64);
    state.noteOn (1, 128);
    state.noteOff (0, -1);
    assert (! state.snapshot().any());
}
