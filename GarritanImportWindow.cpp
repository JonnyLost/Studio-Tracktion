// SPDX-License-Identifier: GPL-3.0-or-later
#include "GarritanImportWindow.h"
#include "StudioUI.h"

namespace studio::garritan
{
using namespace juce;

ImportWindow::Task::~Task()
{
    progress.cancelled.store (true);
    if (worker.joinable()) worker.join();
}

ImportWindow::ImportWindow (File catalogDirectory)
    : DocumentWindow ("Import Garritan Library", colours::canvas, closeButton),
      catalog (std::move (catalogDirectory))
{
    setUsingNativeTitleBar (true);
    for (Component* component : std::initializer_list<Component*> { &status, &results, &choose, &import, &cancel, &reveal, &partial })
        content.addAndMakeVisible (*component);
    choose.setBounds (14, 14, 186, 30);
    cancel.setBounds (208, 14, 80, 30);
    reveal.setBounds (512, 14, 154, 30);
    status.setBounds (14, 52, 652, 30);
    results.setBounds (14, 90, 652, 310);
    partial.setBounds (14, 410, 250, 30);
    import.setBounds (480, 410, 186, 30);
    content.setSize (680, 454);
    results.setMultiLine (true);
    results.setReadOnly (true);
    results.setScrollbarsShown (true);
    results.setText ("Choose a library folder to inspect readable definitions and sample references.");
    choose.onClick = [this] { chooseFolder(); };
    reveal.onClick = [this] { report.revealToUser(); };
    cancel.onClick = [this] { if (task) task->progress.cancelled.store (true); };
    import.onClick = [this] { importBatch(); };
    partial.onClick = [this] { refreshButtons(); };
    setContentNonOwned (&content, true);
    centreWithSize (680, 454);
    refreshButtons();
}

ImportWindow::~ImportWindow()
{
    stopTimer();
    task.reset();
    clearContentComponent();
}

void ImportWindow::closeButtonPressed()
{
    if (task) task->progress.cancelled.store (true);
    setVisible (false);
}

void ImportWindow::chooseFolder()
{
    if (task || chooser) return;
    chooser = std::make_unique<FileChooser> ("Choose a Garritan library folder", inventory.root);
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
        [safe = Component::SafePointer<ImportWindow> (this)] (const FileChooser& dialog)
        {
            if (safe == nullptr) return;
            const auto selected = dialog.getResult();
            MessageManager::callAsync ([safe, selected]
            {
                if (safe == nullptr) return;
                safe->chooser.reset();
                if (selected != File() && safe->isVisible()) safe->startScan (selected);
            });
        });
}

void ImportWindow::startScan (const File& root)
{
    inventory = {};
    report = File();
    results.clear();
    task = std::make_unique<Task>();
    auto* job = task.get();
    const auto reports = catalog.getSiblingFile ("Garritan Inventories");
    job->worker = std::thread ([job, root, reports]
    {
        try
        {
            job->inventory = scan (root, job->progress);
            if (! job->progress.cancelled.load() && job->inventory.root != File())
                job->error = saveInventory (job->inventory, reports, job->report).getErrorMessage();
        }
        catch (const std::exception& e) { job->error = String::fromUTF8 (e.what()); }
        job->done.store (true, std::memory_order_release);
    });
    status.setText ("Scanning library...", dontSendNotification);
    refreshButtons();
    startTimer (100);
}

void ImportWindow::importBatch()
{
    if (task) return;
    task = std::make_unique<Task>();
    auto* job = task.get();
    job->inventory = std::move (inventory);
    job->scanning = false;
    const auto allowPartial = partial.getToggleState();
    const auto eligible = [allowPartial] (const Definition& item)
    {
        return item.compatibility == Compatibility::supported
            || (allowPartial && item.compatibility == Compatibility::partial);
    };
    job->total = static_cast<int> (std::count_if (job->inventory.definitions.begin(), job->inventory.definitions.end(), eligible));
    job->worker = std::thread ([job, destination = catalog, allowPartial, eligible]
    {
        try
        {
            for (const auto& item : job->inventory.definitions)
            {
                if (job->progress.cancelled.load()) break;
                if (! eligible (item)) continue;
                File output;
                auto result = convert (job->inventory, item, destination, allowPartial, job->progress, output);
                if (result.wasOk()) ++job->imported;
                else if (! job->progress.cancelled.load())
                    job->failures.add (item.instrument.name + ": " + result.getErrorMessage());
                ++job->progress.completed;
            }
        }
        catch (const std::exception& e) { job->error = String::fromUTF8 (e.what()); }
        job->done.store (true, std::memory_order_release);
    });
    refreshButtons();
    startTimer (100);
}

void ImportWindow::showInventory()
{
    String text = inventory.issues.joinIntoString ("\n");
    for (const auto& item : inventory.definitions)
    {
        text << "\n" << item.instrument.name << " | " << statusName (item.compatibility);
        text << "\n  " << item.source.getRelativePathFrom (inventory.root);
        for (const auto& issue : item.issues) text << "\n  " << issue;
        text << "\n";
    }
    if (text.isEmpty()) text = "No recognized instrument definitions found.";
    results.setText (text.trimStart(), false);
    results.setCaretPosition (0);
}

void ImportWindow::timerCallback()
{
    if (! task) return;
    if (! task->done.load (std::memory_order_acquire))
    {
        const auto completed = String (task->progress.completed.load());
        status.setText (task->progress.cancelled.load() ? "Cancelling..."
            : task->scanning ? "Scanned " + completed + " entries"
                             : "Processed " + completed + " of " + String (task->total) + " instruments", dontSendNotification);
        return;
    }
    stopTimer();
    auto finished = std::move (task);
    const auto cancelled = finished->progress.cancelled.load();
    if (! finished->scanning || ! cancelled) inventory = std::move (finished->inventory);
    if (finished->scanning)
    {
        report = finished->report;
        if (! cancelled) showInventory();
        status.setText (cancelled ? "Scan cancelled."
            : String (inventory.definitions.size()) + " definitions, " + String (inventory.samples.size()) + " sample files",
            dontSendNotification);
    }
    else
    {
        showInventory();
        const auto summary = String (finished->imported) + " imported, " + String (finished->failures.size()) + " failed.";
        status.setText (cancelled ? "Cancelled. " + summary : summary, dontSendNotification);
        if (! finished->failures.isEmpty())
            results.setText (finished->failures.joinIntoString ("\n") + "\n\n" + results.getText(), false);
        if (finished->imported && onCatalogChanged) onCatalogChanged();
    }
    if (finished->error.isNotEmpty()) results.setText (finished->error + "\n\n" + results.getText(), false);
    refreshButtons();
}

void ImportWindow::refreshButtons()
{
    choose.setEnabled (! task);
    cancel.setEnabled (task != nullptr);
    reveal.setEnabled (report != File() && report.existsAsFile());
    partial.setEnabled (! task && std::any_of (inventory.definitions.begin(), inventory.definitions.end(),
        [] (const auto& item) { return item.compatibility == Compatibility::partial; }));
    import.setButtonText (partial.getToggleState() ? "Import Supported & Partial" : "Import Supported");
    import.setEnabled (! task && std::any_of (inventory.definitions.begin(), inventory.definitions.end(),
        [this] (const auto& item)
        {
            return item.compatibility == Compatibility::supported
                || (partial.getToggleState() && item.compatibility == Compatibility::partial);
        }));
}
}
