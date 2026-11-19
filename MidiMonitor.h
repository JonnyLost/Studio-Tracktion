// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <deque>
#include <vector>

class StudioMidiMonitor final : public juce::Component,
                                private juce::Timer,
                                private juce::TableListBoxModel
{
public:
    StudioMidiMonitor();
    ~StudioMidiMonitor() override;

    void setCaptureOpen (bool);
    void resized() override;
    bool isActive() const noexcept { return recentTicks > 0; }
    void setFloating (bool floating) { dock.setButtonText (floating ? "Dock" : "Float"); }
    std::function<void()> onToggleDock;

private:
    enum class Tap { input, trackOutput, instrumentInput };
    enum EventType { noteOn = 1, noteOff, control, bend, channelPressure, polyPressure,
                     program, timing, other };

    struct Event
    {
        Tap tap;
        int source;
        double milliseconds;
        int channel;
        EventType type;
        int first;
        int second;
    };

    struct Input final : juce::MidiInputCallback
    {
        Input (StudioMidiMonitor& monitor, int index, const juce::MidiDeviceInfo& info)
            : owner (monitor), source (index), port (juce::MidiInput::openDevice (info.identifier, this)) {}

        ~Input() override
        {
            if (port != nullptr)
                port->stop();
        }

        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

        StudioMidiMonitor& owner;
        const int source;
        std::unique_ptr<juce::MidiInput> port;
    };

    // Multiple input callbacks may run concurrently. Producers try once and
    // drop on contention or overflow. The callback never waits or allocates.
    static constexpr unsigned capacity = 1024;
    std::array<Event, capacity> queue {};
    unsigned head = 0, tail = 0;
    std::atomic_flag queueLock = ATOMIC_FLAG_INIT;
    std::atomic<unsigned> dropped { 0 };
    std::atomic<bool> pendingActivity { false };
    std::atomic<bool> paused { false };

    bool captureOpen = false;
    int recentTicks = 0;
    int refreshTicks = 0;
    double startTime = 0.0;
    juce::Array<juce::MidiDeviceInfo> devices;
    std::vector<std::unique_ptr<Input>> inputs;
    std::deque<Event> history;
    std::vector<size_t> visible;

    juce::ComboBox tap, device, channel, type;
    juce::Label status;
    juce::TextButton pause { "Pause" }, clear { "Clear" }, dock { "Float" };
    juce::TableListBox table;

    void capture (int source, const juce::MidiMessage&) noexcept;
    void refreshDevices();
    void resetLog();
    void refreshRows();
    void timerCallback() override;
    int getNumRows() override { return static_cast<int> (visible.size()); }
    void paintRowBackground (juce::Graphics&, int, int, int, bool) override;
    void paintCell (juce::Graphics&, int, int, int, int, bool) override;
    static juce::String eventName (EventType);
    static juce::String eventData (const Event&);
};
