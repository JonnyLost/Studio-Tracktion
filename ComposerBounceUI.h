// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ComposerBounce.h"

namespace studio::bounce
{
void showOptions (juce::Component&, tracktion::Edit&, const juce::Array<tracktion::EditItemID>& selected,
                  std::function<void (juce::Result, Options)>);
}
