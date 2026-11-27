// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "StudioUI.h"
#include <functional>

namespace studio::designer
{
class TranslationReview final : public juce::Component
{
public:
    TranslationReview(const juce::String& text,bool ready,std::function<void()> apply)
    {
        report.setMultiLine(true);report.setReadOnly(true);report.setScrollbarsShown(true);
        report.setCaretVisible(false);report.setFont(uiFont(13));report.setText(text,false);
        addAndMakeVisible(report);addAndMakeVisible(confirm);addAndMakeVisible(cancel);
        confirm.setEnabled(ready);
        confirm.onClick=[this,apply=std::move(apply)]{
            auto callback=apply;
            if(auto* window=findParentComponentOfClass<juce::DialogWindow>())window->exitModalState(1);
            if(callback)juce::MessageManager::callAsync(std::move(callback));
        };
        cancel.onClick=[this]{if(auto* window=findParentComponentOfClass<juce::DialogWindow>())window->exitModalState(0);};
    }
    void resized() override
    {
        auto r=getLocalBounds().reduced(12);
        auto buttons=r.removeFromBottom(appearance.control(34));
        confirm.setBounds(buttons.removeFromRight(appearance.span(170)).reduced(2));
        cancel.setBounds(buttons.removeFromRight(appearance.span(100)).reduced(2));
        report.setBounds(r.reduced(2));
    }
private:
    juce::TextEditor report;
    juce::TextButton confirm{"Translate MIDI"},cancel{"Close"};
};
}
