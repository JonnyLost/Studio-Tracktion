// SPDX-License-Identifier: GPL-3.0-or-later
#include "ComposerBounceUI.h"
#include "SpatialRouting.h"
#include "StudioUI.h"

namespace studio::bounce
{
namespace te = tracktion;
using namespace juce;

namespace
{
class OptionsPanel final : public Component
{
public:
    OptionsPanel (te::Edit& edit, const Array<te::EditItemID>& selection)
        : whole ({ te::TimePosition(), te::toPosition (edit.getLength()) }),
          cycle (edit.getTransport().getLoopRange())
    {
        addAndMakeVisible (kind);
        addAndMakeVisible (range);
        addAndMakeVisible (channels);
        addAndMakeVisible (rate);
        addAndMakeVisible (depth);
        addAndMakeVisible (trackList);
        addAndMakeVisible (room);
        addAndMakeVisible (begin);
        addAndMakeVisible (end);
        addAndMakeVisible (tail);
        kind.addItemList ({ "Full mix", "Individual track stems" }, 1);
        range.addItemList ({ "Whole project", "Cycle range", "Custom time" }, 1);
        range.setItemEnabled (2, ! cycle.isEmpty());
        channels.addItemList ({ "Stereo", "Mono" }, 1);
        rate.addItemList ({ "48 kHz", "44.1 kHz" }, 1);
        depth.addItemList ({ "24-bit PCM", "16-bit PCM" }, 1);
        for (auto* choice : { &kind, &range, &channels, &rate, &depth })
            choice->setSelectedId (1, dontSendNotification);
        kind.setTitle ("Export content");
        range.setTitle ("Export range");
        channels.setTitle ("Channel layout");
        rate.setTitle ("Sample rate");
        depth.setTitle ("Bit depth");

        const auto initialiseTime = [] (Label& label, const char* title, String value)
        {
            label.setText (value, dontSendNotification);
            label.setTitle (title);
            label.setEditable (true, true);
            label.setColour (Label::outlineColourId, colours::divider);
            label.setColour (Label::backgroundColourId, colours::control);
        };
        initialiseTime (begin, "Start (seconds)", "0.000");
        initialiseTime (end, "End (seconds)", String (whole.getEnd().inSeconds(), 3));
        initialiseTime (tail, "Tail (seconds)", "2.000");
        trackList.setViewedComponent (&trackChoices, false);
        trackList.setScrollBarsShown (true, false);
        for (auto* track : te::getAudioTracks (edit))
        {
            if (spatial::isRoomReturn (*track) || track->getParentTrack() != nullptr
                || ! track->getOutput().usesDefaultAudioOut())
                continue;
            auto* button = toggles.add (new ToggleButton (track->getName()));
            button->setToggleState (selection.isEmpty() || selection.contains (track->itemID), dontSendNotification);
            trackIds.add (track->itemID);
            trackChoices.addAndMakeVisible (*button);
        }
        room.setToggleState (spatial::roomReturnTrack (edit) != nullptr, dontSendNotification);
        roomAvailable = room.getToggleState();
        kind.setItemEnabled (2, ! trackIds.isEmpty());
        kind.onChange = [this] { refresh(); };
        range.onChange = [this] { refresh(); };
        setSize (appearance.span (460), appearance.px (280));
        refresh();
    }

    Result read (Options& options)
    {
        auto seconds = [] (const Label& label)
        {
            const auto text = label.getText().trim();
            return text.isNotEmpty() && text.containsOnly ("0123456789.") && text != "."
                && text.indexOfChar ('.') == text.lastIndexOfChar ('.')
                ? text.getDoubleValue() : std::numeric_limits<double>::quiet_NaN();
        };
        options.range = range.getSelectedId() == 2 ? cycle : whole;
        if (range.getSelectedId() == 3)
        {
            const auto first = seconds (begin), last = seconds (end);
            if (! std::isfinite (first) || ! std::isfinite (last) || first >= last)
                return Result::fail ("Enter a valid start and end in seconds.");
            options.range = { te::TimePosition::fromSeconds (first), te::TimePosition::fromSeconds (last) };
        }
        options.tail = seconds (tail);
        if (options.range.isEmpty() || ! std::isfinite (options.tail) || options.tail > 30)
            return Result::fail ("Choose a nonempty range and a tail from 0 to 30 seconds.");
        options.sampleRate = rate.getSelectedId() == 1 ? 48000 : 44100;
        options.bitDepth = depth.getSelectedId() == 1 ? 24 : 16;
        options.mono = channels.getSelectedId() == 2;
        options.stems = kind.getSelectedId() == 2;
        options.room = room.getToggleState();
        if (options.stems)
        {
            for (int i = 0; i < toggles.size(); ++i)
                if (toggles[i]->getToggleState())
                    options.tracks.add (trackIds[i]);
            if (options.tracks.isEmpty()) return Result::fail ("Choose at least one stem track.");
        }
        return Result::ok();
    }

    void paint (Graphics& g) override
    {
        g.setColour (colours::text);
        g.setFont (uiFont (12));
        const auto header = timeHeader;
        const int column = header.getWidth() / 3;
        g.drawText ("Start (s)", header.withWidth (column), Justification::centredLeft);
        g.drawText ("End (s)", header.withTrimmedLeft (column).withWidth (column), Justification::centredLeft);
        g.drawText ("Tail (s)", header.withTrimmedLeft (2 * column), Justification::centredLeft);
        g.drawText (kind.getSelectedId() == 2 ? "Track stems · without shared room or master inserts"
                                             : "Mix uses the project’s mixer, master, and shared room.",
                    explanation, Justification::centredLeft, true);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (appearance.px (4));
        const int row = appearance.control (26);
        auto first = bounds.removeFromTop (row);
        kind.setBounds (first.removeFromLeft (first.getWidth() / 2).reduced (2));
        range.setBounds (first.reduced (2));
        auto formats = bounds.removeFromTop (row);
        const int column = formats.getWidth() / 3;
        channels.setBounds (formats.removeFromLeft (column).reduced (2));
        rate.setBounds (formats.removeFromLeft (column).reduced (2));
        depth.setBounds (formats.reduced (2));
        timeHeader = bounds.removeFromTop (appearance.line (12, 2)).reduced (2, 0);
        auto times = bounds.removeFromTop (row);
        begin.setBounds (times.removeFromLeft (column).reduced (2));
        end.setBounds (times.removeFromLeft (column).reduced (2));
        tail.setBounds (times.reduced (2));
        explanation = bounds.removeFromTop (row);
        room.setBounds (bounds.removeFromBottom (row));
        trackList.setBounds (bounds);
        trackChoices.setSize (jmax (1, bounds.getWidth() - trackList.getScrollBarThickness()), row * toggles.size());
        for (int i = 0; i < toggles.size(); ++i)
            toggles[i]->setBounds (0, row * i, trackChoices.getWidth(), row);
    }

private:
    void refresh()
    {
        const bool custom = range.getSelectedId() == 3;
        begin.setEnabled (custom);
        end.setEnabled (custom);
        const bool stems = kind.getSelectedId() == 2;
        trackList.setEnabled (stems);
        room.setEnabled (stems && roomAvailable);
        repaint();
    }

    const te::TimeRange whole, cycle;
    ComboBox kind, range, channels, rate, depth;
    Label begin, end, tail;
    Component trackChoices;
    OwnedArray<ToggleButton> toggles;
    Viewport trackList;
    Array<te::EditItemID> trackIds;
    ToggleButton room { "Include one shared Room Return stem" };
    Rectangle<int> timeHeader, explanation;
    bool roomAvailable = false;
};
}

void showOptions (Component& parent, te::Edit& edit, const Array<te::EditItemID>& selected,
                  std::function<void (Result, Options)> accepted)
{
    auto* window = new AlertWindow ("Export WAV", {}, MessageBoxIconType::NoIcon, &parent);
    const auto panel = std::make_shared<OptionsPanel> (edit, selected);
    window->addCustomComponent (panel.get());
    window->addButton ("Choose destination...", 1);
    window->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    window->enterModalState (true, ModalCallbackFunction::create ([panel, window, accepted = std::move (accepted)] (int answer)
    {
        if (answer == 1)
        {
            Options options;
            const auto result = panel->read (options);
            accepted (result, std::move (options));
        }
        window->removeCustomComponent (0);
    }), true);
}
}
