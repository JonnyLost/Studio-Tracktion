// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GarritanLibrary.h"
#include <thread>

namespace studio::garritan
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
        Progress progress;
        std::thread worker;
        std::atomic<bool> done { false };
        Inventory inventory;
        juce::File report;
        juce::String error;
        juce::StringArray failures;
        int imported = 0, total = 0;
        bool scanning = true;
        ~Task();
    };

    void chooseFolder();
    void startScan (const juce::File&);
    void importBatch();
    void timerCallback() override;
    void refreshButtons();
    void showInventory();

    juce::File catalog, report;
    Inventory inventory;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<Task> task;
    juce::Component content;
    juce::Label status;
    juce::TextEditor results;
    juce::TextButton choose { "Choose Library Folder..." }, import { "Import Supported" },
                     cancel { "Cancel" }, reveal { "Show JSON Report" };
    juce::ToggleButton partial { "Include partial conversions" };
};
}
