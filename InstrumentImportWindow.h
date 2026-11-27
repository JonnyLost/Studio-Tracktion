// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "InstrumentLibrary.h"
#include <thread>

namespace studio::instrumentimport
{
class ImportWindow final : public juce::DocumentWindow, private juce::Timer
{
public:
    explicit ImportWindow (juce::File catalogDirectory);
    ~ImportWindow() override;
    std::function<void()> onCatalogChanged;
    void closeButtonPressed() override;

private:
    struct Task
    {
        enum class Kind { scan, convert, relink };
        Progress progress;
        std::thread worker;
        std::atomic<bool> done { false };
        Inventory inventory;
        juce::File report;
        juce::String error;
        juce::StringArray failures;
        int imported = 0, total = 0;
        Kind kind = Kind::scan;
        ~Task();
    };

    void choosePath (const juce::String& title, bool folder, std::function<void (juce::File)>);
    void startScan (const juce::File& source, bool build = false, const juce::File& sampleRoot = {});
    void startConversion (bool batch);
    void startRelink (const juce::File& preset, const juce::File& newRoot);
    void timerCallback() override;
    void refreshButtons();
    void showInventory();
    void showSelection();
    const Definition* selected() const;

    juce::File catalog, report, lastSource, lastSamples;
    bool lastWasBuilder = false;
    Inventory inventory;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<Task> task;
    juce::Component content;
    juce::Label status;
    juce::TextEditor results;
    juce::ComboBox instruments;
    juce::TextButton choose { "Import Library Folder" }, chooseInstrument { "Import Instrument" },
        builder { "Build from Samples" }, resume { "Rescan / Resume" }, locate { "Locate Missing Samples" },
        relink { "Relink Studio Preset" }, convertOne { "Convert Selected" }, batchConvert { "Batch Convert" },
        cancel { "Cancel" }, reveal { "View Compatibility Report" };
    juce::ToggleButton partial { "Approve partial and inferred mappings" };
};
}
