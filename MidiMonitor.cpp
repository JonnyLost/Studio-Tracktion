// SPDX-License-Identifier: GPL-3.0-or-later
#include "MidiMonitor.h"
#include "StudioUI.h"
using namespace juce;

StudioMidiMonitor::StudioMidiMonitor()
{
    for (auto* box : { &tap, &device, &channel, &type })
    {
        addAndMakeVisible (*box);
        box->onChange = [this] { refreshRows(); };
    }
    tap.setName ("Monitor point");
    tap.addItem ("MIDI Input", 1);
    tap.addItem ("Track Output (unavailable)", 2);
    tap.addItem ("Instrument Input (unavailable)", 3);
    tap.setItemEnabled (2, false);
    tap.setItemEnabled (3, false);
    tap.setSelectedId (1, dontSendNotification);
    device.setName ("MIDI device");
    device.addItem ("All devices", 1);
    device.setSelectedId (1, dontSendNotification);
    channel.setName ("MIDI channel");
    channel.addItem ("All channels", 1);
    for (int ch = 1; ch <= 16; ++ch)
        channel.addItem ("Channel " + String (ch), ch + 1);
    channel.setSelectedId (1, dontSendNotification);
    type.setName ("Event type");
    type.addItem ("All events", 1);
    for (int kind = noteOn; kind <= other; ++kind)
        type.addItem (eventName (static_cast<EventType> (kind)), kind + 1);
    type.setSelectedId (1, dontSendNotification);

    for (auto* button : { &pause, &clear, &dock })
        addAndMakeVisible (*button);
    pause.onClick = [this]
    {
        const bool next = ! paused.load();
        paused.store (next);
        pause.setButtonText (next ? "Resume" : "Pause");
        if (next)
        {
            while (queueLock.test_and_set (std::memory_order_acquire)) {}
            tail = head;
            queueLock.clear (std::memory_order_release);
        }
    };
    clear.onClick = [this] { resetLog(); };
    dock.onClick = [this] { if (onToggleDock) onToggleDock(); };

    addAndMakeVisible (status);
    addAndMakeVisible (table);
    table.setModel (this);
    auto& header = table.getHeader();
    header.addColumn ("Time (s)", 1, 87);
    header.addColumn ("Device", 2, 160);
    header.addColumn ("Event", 3, 150);
    header.addColumn ("Ch", 4, 46);
    header.addColumn ("Data", 5, 225);
    header.setStretchToFitActive (false);
    status.setFont (FontOptions (13.0f));
    appearanceChanged();
    startTime = Time::getMillisecondCounterHiRes();
}

StudioMidiMonitor::~StudioMidiMonitor()
{
    setCaptureOpen (false);
    table.setModel (nullptr);
}

void StudioMidiMonitor::setCaptureOpen (bool open)
{
    if (captureOpen == open)
        return;

    captureOpen = open;
    if (open)
    {
        refreshDevices();
        refreshTicks = 0;
        startTimerHz (20);
    }
    else
    {
        stopTimer();
        inputs.clear();
        pendingActivity.store (false);
        recentTicks = 0;
        // Closing the ports has stopped callbacks before resetting the queue.
        tail = head;
    }
}

void StudioMidiMonitor::refreshDevices()
{
    auto available = MidiInput::getAvailableDevices();
    if (available == devices && ! inputs.empty())
        return;

    auto previousId = device.getSelectedId() - 2;
    auto previous = isPositiveAndBelow (previousId, devices.size())
                        ? devices[previousId].identifier : String();
    const bool changed = available != devices;
    inputs.clear();
    if (changed)
        resetLog();

    devices = std::move (available);
    device.clear (dontSendNotification);
    device.addItem ("All devices", 1);
    int selected = 1;
    for (int index = 0; index < devices.size(); ++index)
    {
        const auto& info = devices.getReference (index);
        auto input = std::make_unique<Input> (*this, index, info);
        auto connected = input->port != nullptr;
        device.addItem (connected ? info.name : info.name + " (unavailable)", index + 2);
        if (connected)
            input->port->start();
        if (info.identifier == previous)
            selected = index + 2;
        inputs.push_back (std::move (input));
    }
    device.setSelectedId (selected, dontSendNotification);
    refreshRows();
}

void StudioMidiMonitor::Input::handleIncomingMidiMessage (MidiInput*, const MidiMessage& message)
{
    owner.capture (source, message);
}

void StudioMidiMonitor::capture (int source, const MidiMessage& message) noexcept
{
    pendingActivity.store (true, std::memory_order_relaxed);
    if (paused.load (std::memory_order_relaxed))
        return;

    const auto* bytes = message.getRawData();
    const auto size = message.getRawDataSize();
    if (size < 1)
        return;

    const int statusByte = bytes[0];
    const int first = size > 1 ? bytes[1] : 0;
    const int second = size > 2 ? bytes[2] : 0;
    Event event { Tap::input, source, Time::getMillisecondCounterHiRes(),
                  statusByte >= 0x80 && statusByte < 0xf0 ? (statusByte & 15) + 1 : 0,
                  other, first, second };
    switch (statusByte & 0xf0)
    {
        case 0x80: event.type = noteOff; break;
        case 0x90: event.type = second == 0 ? noteOff : noteOn; break;
        case 0xa0: event.type = polyPressure; break;
        case 0xb0: event.type = control; break;
        case 0xc0: event.type = program; break;
        case 0xd0: event.type = channelPressure; break;
        case 0xe0: event.type = bend; break;
        default:
            event.type = statusByte == 0xf8 || statusByte == 0xfa
                      || statusByte == 0xfb || statusByte == 0xfc ? timing : other;
            event.first = statusByte;
            event.second = size;
            break;
    }

    if (queueLock.test_and_set (std::memory_order_acquire))
    {
        dropped.fetch_add (1, std::memory_order_relaxed);
        return;
    }

    const auto next = (head + 1) % capacity;
    if (next == tail)
        dropped.fetch_add (1, std::memory_order_relaxed);
    else if (! paused.load (std::memory_order_relaxed))
    {
        queue[head] = event;
        head = next;
    }
    queueLock.clear (std::memory_order_release);
}

void StudioMidiMonitor::resetLog()
{
    while (queueLock.test_and_set (std::memory_order_acquire)) {}
    tail = head;
    startTime = Time::getMillisecondCounterHiRes();
    dropped.store (0, std::memory_order_relaxed);
    queueLock.clear (std::memory_order_release);
    history.clear();
    refreshRows();
}

void StudioMidiMonitor::refreshRows()
{
    visible.clear();
    for (size_t index = 0; index < history.size(); ++index)
    {
        const auto& event = history[index];
        if (event.tap == Tap::input
            && (device.getSelectedId() <= 1 || device.getSelectedId() == event.source + 2)
            && (channel.getSelectedId() <= 1 || channel.getSelectedId() == event.channel + 1)
            && (type.getSelectedId() <= 1 || type.getSelectedId() == event.type + 1))
            visible.push_back (index);
    }
    table.updateContent();
    table.repaint();
}

void StudioMidiMonitor::timerCallback()
{
    if (++refreshTicks == 40)
    {
        refreshTicks = 0;
        refreshDevices();
    }

    if (pendingActivity.exchange (false, std::memory_order_relaxed))
        recentTicks = 6;
    else if (recentTicks > 0)
        --recentTicks;

    std::array<Event, 128> batch;
    size_t count = 0;
    if (! queueLock.test_and_set (std::memory_order_acquire))
    {
        while (tail != head && count < batch.size())
        {
            batch[count++] = queue[tail];
            tail = (tail + 1) % capacity;
        }
        queueLock.clear (std::memory_order_release);
    }
    if (count > 0 && ! paused.load())
    {
        for (size_t index = 0; index < count; ++index)
        {
            if (batch[index].milliseconds < startTime)
                continue;
            if (history.size() == 512)
                history.pop_back();
            history.push_front (batch[index]);
        }
        refreshRows();
    }

    int connected = 0;
    for (const auto& input : inputs)
        connected += input->port != nullptr ? 1 : 0;
    const auto lost = dropped.load (std::memory_order_relaxed);
    auto description = connected == 0 ? String ("No MIDI inputs connected")
                                     : String (connected) + (connected == 1 ? " MIDI input" : " MIDI inputs");
    if (paused.load())
        description += "  |  Paused";
    if (lost > 0)
        description += "  |  Dropped " + String (lost);
    status.setText (description, dontSendNotification);
}

void StudioMidiMonitor::appearanceChanged()
{
    const auto& m = studio::appearance;
    table.setRowHeight (m.line (13, 6));
    table.setHeaderHeight (m.control (23, 12));
    table.getViewport()->setScrollBarThickness (m.scrollbar());
    table.getViewport()->setScrollBarsShown (true, true);
    constexpr int widths[] { 87, 160, 150, 46, 225 };
    for (int i = 0; i < 5; ++i)
        table.getHeader().setColumnWidth (i + 1, m.span (widths[i]));
    resized();
    table.repaint();
}

int StudioMidiMonitor::minimumContentHeight (int width)
{
    const auto& m = studio::appearance;
    return m.px (12) + studio::toolbarHeight (jmax (1, width - m.px (12)),
         { { &tap, 116 }, { &device, 148 }, { &channel, 102 }, { &type, 128 },
           { &pause, 68 }, { &clear, 57 }, { &dock, 62 } })
         + m.line (13, 5) + table.getHeaderHeight() + 2 * table.getRowHeight() + m.scrollbar();
}

void StudioMidiMonitor::resized()
{
    const auto& m = studio::appearance;
    auto bounds = getLocalBounds().reduced (m.px (6));
    studio::layoutToolbar (bounds, { { &tap, 116 }, { &device, 148 }, { &channel, 102 }, { &type, 128 },
                                    { &pause, 68 }, { &clear, 57 }, { &dock, 62 } });
    status.setBounds (bounds.removeFromBottom (m.line (13, 5)));
    table.setBounds (bounds);
}

void StudioMidiMonitor::paintRowBackground (Graphics& g, int row, int width, int height, bool selected)
{
    if (selected || row % 2 == 0)
    {
        g.setColour (selected ? studio::colours::selected : studio::colours::panel);
        g.fillRect (0, 0, width, height);
    }
}

void StudioMidiMonitor::paintCell (Graphics& g, int row, int column, int width, int height, bool)
{
    if (! isPositiveAndBelow (row, getNumRows()))
        return;

    const auto& event = history[visible[static_cast<size_t> (row)]];
    String value;
    switch (column)
    {
        case 1: value = String ((event.milliseconds - startTime) / 1000.0, 3); break;
        case 2: value = devices[event.source].name; break;
        case 3: value = eventName (event.type); break;
        case 4: value = event.channel > 0 ? String (event.channel) : String ("-"); break;
        case 5: value = eventData (event); break;
        default: break;
    }
    g.setColour (Colours::white);
    g.setFont (studio::uiFont (13.0f));
    g.drawFittedText (value, Rectangle<int> (width, height).reduced (studio::appearance.px (5), 0),
                      Justification::centredLeft, 1, 1.0f);
}

String StudioMidiMonitor::eventName (EventType event)
{
    switch (event)
    {
        case noteOn: return "Note On";
        case noteOff: return "Note Off";
        case control: return "Control Change";
        case bend: return "Pitch Bend";
        case channelPressure: return "Channel Aftertouch";
        case polyPressure: return "Poly Aftertouch";
        case program: return "Program Change";
        case timing: return "Clock / Transport";
        case other: return "Other MIDI";
    }
    return {};
}

String StudioMidiMonitor::eventData (const Event& event)
{
    auto note = [&] { return MidiMessage::getMidiNoteName (event.first, true, true, 4); };
    switch (event.type)
    {
        case noteOn:
        case noteOff: return note() + "  |  Velocity " + String (event.second);
        case control:
        {
            String label = "CC " + String (event.first);
            if (event.first == 1) label += " Modulation";
            if (event.first == 11) label += " Expression";
            if (event.first == 64) label += event.second >= 64 ? " Sustain On" : " Sustain Off";
            return label + "  |  " + String (event.second);
        }
        case bend: return String ((event.first | (event.second << 7)) - 8192);
        case channelPressure: return "Pressure " + String (event.first);
        case polyPressure: return note() + "  |  Pressure " + String (event.second);
        case program: return "Program " + String (event.first + 1);
        case timing:
            if (event.first == 0xf8) return "Clock";
            if (event.first == 0xfa) return "Start";
            if (event.first == 0xfb) return "Continue";
            return "Stop";
        case other: return "0x" + String::toHexString (event.first)
                               + "  |  " + String (event.second) + " bytes";
    }
    return {};
}
